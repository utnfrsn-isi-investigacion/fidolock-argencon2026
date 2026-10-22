/**
 * FidoLock - NFC Reader Module
 *
 * PN532 NFC reader implementation via SPI.
 * Implements INFCReader interface.
 */

#ifndef FIDOLOCK_NFC_READER_H
#define FIDOLOCK_NFC_READER_H

#include <Arduino.h>
#include <SPI.h>
#include "../../config.h"
#include "../../ports/out/i_nfc_reader.h"

class NFCReader : public INFCReader {
public:
    bool init() override;
    bool detectCard(uint8_t* uid, uint8_t* uidLength) override;
    bool transceive(uint8_t* sendBuf, uint8_t sendLen,
                    uint8_t* recvBuf, uint8_t* recvLen,
                    uint16_t timeout) override;
    bool sendAPDU(uint8_t* apdu, size_t apduLen,
                  uint8_t* response, size_t* responseLen) override;
    void powerDown() override;
    void wakeUp() override;
    bool isCardPresent() override;

    // Additional hardware-specific methods (not in interface)
    uint32_t getFirmwareVersion();

    /// Enable or disable the SPI diagnostic routine at runtime.  The
    /// diagnostic is always compiled in when DEBUG_NFC is set, but can be
    /// turned off in production builds or overridden by the caller.
    void setDiagnosticEnabled(bool enabled);
    bool diagnosticEnabled() const { return _diagEnabled; }

private:
    bool _initialized = false;
    SPIClass* _spi = nullptr;
    bool _diagEnabled =
#if DEBUG_NFC
        true;
#else
        false;
#endif

    // SPI communication with PN532
    void spiWrite(uint8_t* data, uint8_t len);
    void spiRead(uint8_t* data, uint8_t len);
    bool writeCommand(uint8_t* cmd, uint8_t cmdLen);
    bool readResponse(uint8_t* response, uint8_t* responseLen, uint16_t timeout);
    bool waitReady(uint16_t timeout);

    // SPI frame helpers
    void writeFrame(uint8_t* data, uint8_t len);
    bool readFrame(uint8_t* data, uint8_t* len, uint16_t timeout);
    bool readACK();

    // Checksum calculation
    uint8_t calculateDCS(uint8_t* data, uint8_t len);

    // Raw SPI diagnostic — call after all init retries fail to dump the
    // status and data bytes visible on the bus without any framing logic.
    void spiDiagnostic();
};

#endif // FIDOLOCK_NFC_READER_H
