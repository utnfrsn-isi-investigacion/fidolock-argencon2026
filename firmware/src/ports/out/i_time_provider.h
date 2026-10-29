/**
 * FidoLock - Driven Port: Time Provider
 *
 * Driven port (out): the SPI the application calls out to the hardware clock.
 * Rule 3 of Ports & Adapters — Cockburn 2005.
 */

#ifndef FIDOLOCK_PORTS_OUT_I_TIME_PROVIDER_H
#define FIDOLOCK_PORTS_OUT_I_TIME_PROVIDER_H

#include <stdint.h>

class ITimeProvider {
public:
    virtual ~ITimeProvider() = default;
    virtual uint32_t millis() = 0;
};

#endif // FIDOLOCK_PORTS_OUT_I_TIME_PROVIDER_H
