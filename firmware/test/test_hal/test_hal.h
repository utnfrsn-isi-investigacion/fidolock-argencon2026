/**
 * FidoLock - Test HAL Implementations
 *
 * Minimal HAL implementations for native testing.
 * These only mock the hardware layer, not business logic.
 */

#ifndef FIDOLOCK_TEST_HAL_H
#define FIDOLOCK_TEST_HAL_H

#include "../../src/ports/out/i_time_provider.h"
#include "../../src/ports/out/i_gpio_controller.h"
#include <stdint.h>

/**
 * Controllable time provider for testing time-dependent logic.
 */
class TestTimeProvider : public ITimeProvider {
public:
    TestTimeProvider() : _currentTime(0) {}

    uint32_t millis() override {
        return _currentTime;
    }

    void setTime(uint32_t ms) {
        _currentTime = ms;
    }

    void advance(uint32_t ms) {
        _currentTime += ms;
    }

private:
    uint32_t _currentTime;
};

/**
 * Test GPIO controller that records pin states.
 */
class TestGPIOController : public IGPIOController {
public:
    static const int MAX_PINS = 40;

    TestGPIOController() {
        reset();
    }

    void reset() {
        for (int i = 0; i < MAX_PINS; i++) {
            _pinModes[i] = GpioMode::Input;
            _pinStates[i] = GpioLevel::Low;
        }
    }

    void pinMode(uint8_t pin, GpioMode mode) override {
        if (pin < MAX_PINS) {
            _pinModes[pin] = mode;
        }
    }

    void digitalWrite(uint8_t pin, GpioLevel level) override {
        if (pin < MAX_PINS) {
            _pinStates[pin] = level;
        }
    }

    GpioLevel digitalRead(uint8_t pin) override {
        if (pin < MAX_PINS) {
            return _pinStates[pin];
        }
        return GpioLevel::Low;
    }

    // Test helpers
    GpioMode getPinMode(uint8_t pin) const {
        return pin < MAX_PINS ? _pinModes[pin] : GpioMode::Input;
    }

    GpioLevel getPinState(uint8_t pin) const {
        return pin < MAX_PINS ? _pinStates[pin] : GpioLevel::Low;
    }

private:
    GpioMode  _pinModes[MAX_PINS];
    GpioLevel _pinStates[MAX_PINS];
};

#endif // FIDOLOCK_TEST_HAL_H
