/**
 * FidoLock - Driven Port: System Control
 *
 * Driven port (out): the SPI the application calls out for system-level
 * operations (blocking delay, restart). Abstracts Arduino/ESP32 runtime.
 * Rule 3 of Ports & Adapters — Cockburn 2005.
 */

#ifndef FIDOLOCK_PORTS_OUT_I_SYSTEM_CONTROL_H
#define FIDOLOCK_PORTS_OUT_I_SYSTEM_CONTROL_H

#include <stdint.h>

class ISystemControl {
public:
    virtual ~ISystemControl() = default;

    /** Block for the given number of milliseconds. */
    virtual void delayMs(uint32_t ms) = 0;

    /** Reboot the device. Does not return. */
    virtual void restart() = 0;
};

#endif // FIDOLOCK_PORTS_OUT_I_SYSTEM_CONTROL_H
