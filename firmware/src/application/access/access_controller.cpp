/**
 * FidoLock - Access Controller Module
 *
 * Controls the electronic lock via relay (driven by 2N2222A transistor)
 * and manages access policies including lockout after failed attempts.
 */

#include "access_controller.h"
#include "../../utils/log.h"

static const char* TAG = "Access";

AccessController::AccessController(ITimeProvider& timeProvider, IGPIOController& gpio)
    : _time(timeProvider), _gpio(gpio) {
}

bool AccessController::init() {
    // Configure relay control pin (drives 2N2222A transistor base via 1k resistor)
    _gpio.pinMode(PIN_RELAY_CTRL, GpioMode::Output);
    _gpio.digitalWrite(PIN_RELAY_CTRL, GpioLevel::Low);  // Ensure lock is locked on startup

    _unlocked = false;
    _failedAttempts = 0;
    _lockedOut = false;

    LOG_I(TAG, "Controller initialized, lock secured");
    return true;
}

void AccessController::activateRelay(bool state) {
    _gpio.digitalWrite(PIN_RELAY_CTRL, state ? GpioLevel::High : GpioLevel::Low);
    _unlocked = state;

    LOG_I(TAG, "Relay %s", state ? "ON (unlocked)" : "OFF (locked)");
}

void AccessController::grantAccess(uint32_t durationMs) {
    if (_lockedOut) {
        LOG_W(TAG, "Cannot grant - system is locked out");
        return;
    }

    // Reset failed attempts on successful access
    _failedAttempts = 0;

    // Activate relay to unlock
    activateRelay(true);

    // Store unlock parameters for auto-lock
    _unlockTimestamp = _time.millis();
    _unlockDuration = durationMs;

    LOG_I(TAG, "Granted for %u ms", durationMs);
}

void AccessController::denyAccess() {
    // Make sure lock stays locked
    activateRelay(false);

    // Record failed attempt
    recordFailedAttempt();

    LOG_W(TAG, "Denied");
}

void AccessController::lock() {
    activateRelay(false);
    _unlockTimestamp = 0;
    _unlockDuration = 0;

    LOG_I(TAG, "Lock secured");
}

void AccessController::emergencyUnlock() {
    // Emergency unlock bypasses lockout but should be logged
    LOG_W(TAG, "EMERGENCY UNLOCK ACTIVATED");

    activateRelay(true);
    _unlockTimestamp = _time.millis();
    _unlockDuration = ACCESS_DURATION_MS * 2;  // Double duration for emergency
}

bool AccessController::isUnlocked() {
    return _unlocked;
}

void AccessController::setLockoutPolicy(uint8_t maxAttempts, uint32_t lockoutDuration) {
    _maxAttempts = maxAttempts;
    _lockoutDuration = lockoutDuration;

    LOG_I(TAG, "Lockout policy set - max %u attempts, %u ms lockout",
          maxAttempts, lockoutDuration);
}

void AccessController::recordFailedAttempt() {
    _failedAttempts++;

    LOG_W(TAG, "Failed attempt %u/%u", _failedAttempts, _maxAttempts);

    if (_failedAttempts >= _maxAttempts) {
        _lockedOut = true;
        _lockoutStart = _time.millis();

        LOG_W(TAG, "LOCKOUT ACTIVATED for %u ms", _lockoutDuration);
    }
}

bool AccessController::isLockedOut() {
    if (!_lockedOut) {
        return false;
    }

    // Check if lockout has expired
    if (_time.millis() - _lockoutStart >= _lockoutDuration) {
        _lockedOut = false; // TODO: Check mixed responsibilities here
        _failedAttempts = 0;
        LOG_I(TAG, "Lockout expired");
        return false;
    }

    return true;
}

uint32_t AccessController::getLockoutRemaining() {
    if (!_lockedOut) {
        return 0;
    }

    uint32_t elapsed = _time.millis() - _lockoutStart;
    if (elapsed >= _lockoutDuration) {
        return 0;
    }

    return _lockoutDuration - elapsed;
}

void AccessController::resetFailedAttempts() {
    _failedAttempts = 0;
    _lockedOut = false;
    LOG_I(TAG, "Failed attempts reset");
}

void AccessController::update() {
    // Auto-lock after unlock duration expires
    if (_unlocked && _unlockDuration > 0) {
        if (_time.millis() - _unlockTimestamp >= _unlockDuration) {
            lock();
            LOG_I(TAG, "Auto-lock triggered");
        }
    }

    // Auto-clear lockout after duration
    if (_lockedOut) {
        isLockedOut();  // This checks and clears if expired
    }
}

uint8_t AccessController::getFailedAttempts() {
    return _failedAttempts;
}
