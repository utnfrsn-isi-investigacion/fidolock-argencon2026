/**
 * FidoLock - Access Controller Module
 *
 * Controls the electronic lock via relay and manages access policies
 * including lockout after failed attempts.
 * Implements IAccessController interface with HAL dependency injection.
 */

#ifndef FIDOLOCK_ACCESS_CONTROLLER_H
#define FIDOLOCK_ACCESS_CONTROLLER_H

#include <stdint.h>
#include "../../config.h"
#include "../../ports/in/i_access_controller.h"
#include "../../ports/out/i_time_provider.h"
#include "../../ports/out/i_gpio_controller.h"

class AccessController : public IAccessController {
public:
    /**
     * Constructor with HAL dependency injection.
     * @param timeProvider Time provider for millis()
     * @param gpio GPIO controller for relay control
     */
    AccessController(ITimeProvider& timeProvider, IGPIOController& gpio);

    bool init() override;
    void grantAccess(uint32_t durationMs = ACCESS_DURATION_MS) override;
    void denyAccess() override;
    void emergencyUnlock() override;
    void lock() override;
    bool isUnlocked() override;
    void setLockoutPolicy(uint8_t maxAttempts, uint32_t lockoutDuration) override;
    void recordFailedAttempt() override;
    bool isLockedOut() override;
    uint32_t getLockoutRemaining() override;
    void resetFailedAttempts() override;
    void update() override;
    uint8_t getFailedAttempts() override;

private:
    ITimeProvider& _time;
    IGPIOController& _gpio;

    bool _unlocked = false;
    uint32_t _unlockTimestamp = 0;
    uint32_t _unlockDuration = 0;

    uint8_t _failedAttempts = 0;
    uint8_t _maxAttempts = MAX_FAILED_ATTEMPTS;
    uint32_t _lockoutDuration = LOCKOUT_DURATION_MS;
    uint32_t _lockoutStart = 0;
    bool _lockedOut = false;

    void activateRelay(bool state);
};

#endif // FIDOLOCK_ACCESS_CONTROLLER_H
