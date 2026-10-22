/**
 * FidoLock - Driven Port: GPIO Controller
 *
 * Driven port (out): the SPI the application calls out to GPIO hardware.
 * Rule 3 of Ports & Adapters — Cockburn 2005.
 *
 * The port owns its vocabulary (GpioMode/GpioLevel) instead of borrowing
 * Arduino's OUTPUT/HIGH/LOW encoding, so application code never depends on
 * the Arduino core; the Arduino adapter translates at the boundary.
 */

#ifndef FIDOLOCK_PORTS_OUT_I_GPIO_CONTROLLER_H
#define FIDOLOCK_PORTS_OUT_I_GPIO_CONTROLLER_H

#include <stdint.h>

enum class GpioMode : uint8_t {
    Input,
    Output,
    InputPullup,
};

enum class GpioLevel : uint8_t {
    Low,
    High,
};

class IGPIOController {
public:
    virtual ~IGPIOController() = default;
    virtual void pinMode(uint8_t pin, GpioMode mode) = 0;
    virtual void digitalWrite(uint8_t pin, GpioLevel level) = 0;
    virtual GpioLevel digitalRead(uint8_t pin) = 0;
};

#endif // FIDOLOCK_PORTS_OUT_I_GPIO_CONTROLLER_H
