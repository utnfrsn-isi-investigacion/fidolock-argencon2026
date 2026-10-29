/**
 * FidoLock - Arduino HAL Implementation
 *
 * Real hardware implementation using Arduino functions.
 */

#ifndef FIDOLOCK_ARDUINO_HAL_H
#define FIDOLOCK_ARDUINO_HAL_H

#include "../../ports/out/i_time_provider.h"
#include "../../ports/out/i_gpio_controller.h"
#include "../../ports/out/i_system_control.h"
#include "../../ports/out/i_bt_radio.h"
#include <Arduino.h>

class ArduinoTimeProvider : public ITimeProvider {
public:
    uint32_t millis() override {
        return ::millis();
    }
};

class ArduinoGPIOController : public IGPIOController {
public:
    void pinMode(uint8_t pin, GpioMode mode) override {
        ::pinMode(pin, toArduino(mode));
    }

    void digitalWrite(uint8_t pin, GpioLevel level) override {
        ::digitalWrite(pin, level == GpioLevel::High ? HIGH : LOW);
    }

    GpioLevel digitalRead(uint8_t pin) override {
        return ::digitalRead(pin) == HIGH ? GpioLevel::High : GpioLevel::Low;
    }

private:
    static uint8_t toArduino(GpioMode mode) {
        switch (mode) {
            case GpioMode::Output:      return OUTPUT;
            case GpioMode::InputPullup: return INPUT_PULLUP;
            case GpioMode::Input:       break;
        }
        return INPUT;
    }
};

class ArduinoBtRadio : public IBtRadio {
public:
    bool isStarted() override { return ::btStarted(); }
    bool start() override     { return ::btStart(); }
    bool stop() override      { return ::btStop(); }
};

class ArduinoSystemControl : public ISystemControl {
public:
    void delayMs(uint32_t ms) override {
        ::delay(ms);
    }
    void restart() override {
        ESP.restart();
    }
};

#endif // FIDOLOCK_ARDUINO_HAL_H
