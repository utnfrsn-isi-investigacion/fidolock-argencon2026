/**
 * FidoLock - Driven Port: Bluetooth Radio
 *
 * Driven port (out): the SPI the application calls out to the Bluetooth
 * RF subsystem. Rule 3 of Ports & Adapters — Cockburn 2005.
 *
 * Sole consumer today is CryptoEngine::init(), which opens a brief RF
 * window while seeding its CTR-DRBG so the RF subsystem's high-speed ADC
 * feeds the ESP32 hardware RNG (ESP32 TRM v5.8 §18).
 */

#ifndef FIDOLOCK_PORTS_OUT_I_BT_RADIO_H
#define FIDOLOCK_PORTS_OUT_I_BT_RADIO_H

class IBtRadio {
public:
    virtual ~IBtRadio() = default;

    /** True if the radio is currently up. */
    virtual bool isStarted() = 0;

    /** Bring the radio up. Returns false on failure. */
    virtual bool start() = 0;

    /** Shut the radio down. Returns false on failure. */
    virtual bool stop() = 0;
};

#endif // FIDOLOCK_PORTS_OUT_I_BT_RADIO_H
