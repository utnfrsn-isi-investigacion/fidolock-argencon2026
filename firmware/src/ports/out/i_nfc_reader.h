/**
 * FidoLock - Driven Port: NFC Reader
 *
 * Driven port (out): the SPI the application calls out to NFC hardware.
 * Rule 3 of Ports & Adapters — Cockburn 2005.
 */

#ifndef FIDOLOCK_PORTS_OUT_I_NFC_READER_H
#define FIDOLOCK_PORTS_OUT_I_NFC_READER_H

#include <stdint.h>
#include <stddef.h>

class INFCReader {
public:
    virtual ~INFCReader() = default;

    virtual bool init() = 0;

    virtual bool detectCard(uint8_t* uid, uint8_t* uidLength) = 0;

    virtual bool transceive(uint8_t* sendBuf, uint8_t sendLen,
                            uint8_t* recvBuf, uint8_t* recvLen,
                            uint16_t timeout) = 0;

    virtual bool sendAPDU(uint8_t* apdu, size_t apduLen,
                          uint8_t* response, size_t* responseLen) = 0;

    virtual void powerDown() = 0;

    virtual void wakeUp() = 0;

    virtual bool isCardPresent() = 0;
};

#endif // FIDOLOCK_PORTS_OUT_I_NFC_READER_H
