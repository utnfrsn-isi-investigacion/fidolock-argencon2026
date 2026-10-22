/**
 * FidoLock - NFC Reader Module
 *
 * SPI implementation for PN532 NFC reader communication.
 */

#include "nfc_reader.h"
#include "../../utils/log.h"

static const char* TAG = "NFC";

// PN532 SPI commands
#define PN532_SPI_STATREAD      0x02
#define PN532_SPI_DATAWRITE     0x01
#define PN532_SPI_DATAREAD      0x03
#define PN532_SPI_READY         0x01

// PN532 Frame markers
#define PN532_PREAMBLE          0x00
#define PN532_STARTCODE1        0x00
#define PN532_STARTCODE2        0xFF
#define PN532_POSTAMBLE         0x00
#define PN532_HOSTTOPN532       0xD4
#define PN532_PN532TOHOST       0xD5

// PN532 Commands
#define PN532_COMMAND_GETFIRMWAREVERSION    0x02
#define PN532_COMMAND_SAMCONFIGURATION      0x14
#define PN532_COMMAND_INLISTPASSIVETARGET   0x4A
#define PN532_COMMAND_INDATAEXCHANGE        0x40
#define PN532_COMMAND_POWERDOWN             0x16

// PN532 Responses
#define PN532_RESPONSE_INDATAEXCHANGE       0x41
#define PN532_RESPONSE_INLISTPASSIVETARGET  0x4B

bool NFCReader::init() {
    // Initialize SPI
    _spi = new SPIClass(VSPI);
    _spi->begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_SS);

    // Configure chip select pin
    pinMode(PIN_SPI_SS, OUTPUT);
    digitalWrite(PIN_SPI_SS, HIGH);  // Deselect

    // Optional IRQ pin
    pinMode(PIN_PN532_IRQ, INPUT);

    // The PN532 requires ≥ 400 ms from SPI bus init before it is ready to
    // accept commands (internal oscillator start-up + firmware boot).
    delay(500);

    // Wake up PN532 and synchronise its SPI state machine
    wakeUp();

    // Retry GetFirmwareVersion — SPI sometimes needs an extra settling cycle
    uint32_t version = 0;
    for (uint8_t attempt = 1; attempt <= 3 && version == 0; attempt++) {
        if (attempt > 1) {
            LOG_W(TAG, "GetFirmwareVersion retry %d/3", attempt);
            delay(200);
            wakeUp();
        }
        version = getFirmwareVersion();
    }

    if (version == 0) {
        LOG_E(TAG, "Failed to communicate with PN532");
        LOG_E(TAG, "Check wiring (SCK=18 MISO=19 MOSI=23 SS=5)");
        LOG_E(TAG, "Verify HW-147 interface selector is set to SPI");
        spiDiagnostic();
        return false;
    }

    LOG_I(TAG, "PN532 firmware v%d.%d",
          (version >> 24) & 0xFF,
          (version >> 16) & 0xFF);

    // Configure SAM (Security Access Module)
    uint8_t samCmd[] = {PN532_COMMAND_SAMCONFIGURATION, 0x01, 0x00, 0x00};
    uint8_t response[8];
    uint8_t responseLen = sizeof(response);

    if (!writeCommand(samCmd, sizeof(samCmd))) {
        LOG_E(TAG, "Error sending SAM config");
        return false;
    }

    if (!readResponse(response, &responseLen, 1000)) {
        LOG_E(TAG, "Error in SAM config response");
        return false;
    }

    _initialized = true;
    LOG_I(TAG, "Initialized successfully");
    return true;
}

void NFCReader::spiWrite(uint8_t* data, uint8_t len) {
    _spi->beginTransaction(SPISettings(NFC_SPI_CLOCK, LSBFIRST, SPI_MODE0));
    digitalWrite(PIN_SPI_SS, LOW);
    delay(1);

    for (uint8_t i = 0; i < len; i++) {
        _spi->transfer(data[i]);
    }

    digitalWrite(PIN_SPI_SS, HIGH);
    _spi->endTransaction();
}

void NFCReader::spiRead(uint8_t* data, uint8_t len) {
    _spi->beginTransaction(SPISettings(NFC_SPI_CLOCK, LSBFIRST, SPI_MODE0));
    digitalWrite(PIN_SPI_SS, LOW);
    delay(1);

    for (uint8_t i = 0; i < len; i++) {
        data[i] = _spi->transfer(0x00);
    }

    digitalWrite(PIN_SPI_SS, HIGH);
    _spi->endTransaction();
}

bool NFCReader::waitReady(uint16_t timeout) {
    uint32_t start = millis();

    while (millis() - start < timeout) {
        _spi->beginTransaction(SPISettings(NFC_SPI_CLOCK, LSBFIRST, SPI_MODE0));
        digitalWrite(PIN_SPI_SS, LOW);
        delay(1);

        _spi->transfer(PN532_SPI_STATREAD);
        uint8_t status = _spi->transfer(0x00);

        digitalWrite(PIN_SPI_SS, HIGH);
        _spi->endTransaction();

        if (status == PN532_SPI_READY) {
            return true;
        }

        delay(10);
    }

    return false;
}

void NFCReader::writeFrame(uint8_t* data, uint8_t len) {
    uint8_t frame[len + 9];
    uint8_t idx = 0;

    // SPI data write command
    frame[idx++] = PN532_SPI_DATAWRITE;

    // Preamble and start codes
    frame[idx++] = PN532_PREAMBLE;
    frame[idx++] = PN532_STARTCODE1;
    frame[idx++] = PN532_STARTCODE2;

    // Length and length checksum
    uint8_t length = len + 1;  // +1 for TFI
    frame[idx++] = length;
    frame[idx++] = ~length + 1;

    // TFI (host to PN532)
    frame[idx++] = PN532_HOSTTOPN532;

    // Data
    uint8_t dcs = PN532_HOSTTOPN532;
    for (uint8_t i = 0; i < len; i++) {
        frame[idx++] = data[i];
        dcs += data[i];
    }

    // Data checksum and postamble
    frame[idx++] = ~dcs + 1;
    frame[idx++] = PN532_POSTAMBLE;

    spiWrite(frame, idx);
}

bool NFCReader::readFrame(uint8_t* data, uint8_t* len, uint16_t timeout) {
    uint8_t bufCap = *len;  // save caller's buffer capacity
    *len = 0;               // reset so callers can distinguish timeout (0) from partial data (>0)

    if (!waitReady(timeout)) {
        return false;
    }

    _spi->beginTransaction(SPISettings(NFC_SPI_CLOCK, LSBFIRST, SPI_MODE0));
    digitalWrite(PIN_SPI_SS, LOW);
    delay(1);

    // Send read command
    _spi->transfer(PN532_SPI_DATAREAD);

    // Read preamble and start codes
    if (_spi->transfer(0x00) != PN532_PREAMBLE) goto fail;
    if (_spi->transfer(0x00) != PN532_STARTCODE1) goto fail;
    if (_spi->transfer(0x00) != PN532_STARTCODE2) goto fail;

    {
        // Read length
        uint8_t length = _spi->transfer(0x00);
        uint8_t lcs = _spi->transfer(0x00);

        if ((uint8_t)(length + lcs) != 0) goto fail;  // Length checksum failed

        // Read TFI
        uint8_t tfi = _spi->transfer(0x00);
        if (tfi != PN532_PN532TOHOST) goto fail;

        // Read data
        uint8_t dataLen = length - 1;  // -1 for TFI
        if (dataLen > bufCap) dataLen = bufCap;

        uint8_t dcs = tfi;
        for (uint8_t i = 0; i < dataLen; i++) {
            data[i] = _spi->transfer(0x00);
            dcs += data[i];
        }
        *len = dataLen;

        // Verify data checksum
        dcs += _spi->transfer(0x00);
        if (dcs != 0) goto fail;

        // Read postamble
        _spi->transfer(0x00);
    }

    digitalWrite(PIN_SPI_SS, HIGH);
    _spi->endTransaction();
    return true;

fail:
    digitalWrite(PIN_SPI_SS, HIGH);
    _spi->endTransaction();
    return false;
}

bool NFCReader::readACK() {
    // PN532 SPI protocol requires the host to read the 6-byte ACK frame
    // (00 00 FF 00 FF 00) after every command before the response is ready.
    // Failing to consume it leaves the READY flag asserted, so readFrame
    // always sees the ACK instead of the real response and fails.
    if (!waitReady(100)) {
        return false;
    }

    _spi->beginTransaction(SPISettings(NFC_SPI_CLOCK, LSBFIRST, SPI_MODE0));
    digitalWrite(PIN_SPI_SS, LOW);
    delay(1);

    _spi->transfer(PN532_SPI_DATAREAD);
    uint8_t ack[6];
    for (uint8_t i = 0; i < 6; i++) {
        ack[i] = _spi->transfer(0x00);
    }

    digitalWrite(PIN_SPI_SS, HIGH);
    _spi->endTransaction();

    // Validate ACK frame: preamble + start codes + 00 FF + postamble.
    // Some clone PN532 modules emit a slightly non-spec ACK; log a warning
    // but continue — the response-phase frame validation will catch real errors.
    bool valid = (ack[0] == PN532_PREAMBLE   &&
                  ack[1] == PN532_STARTCODE1 &&
                  ack[2] == PN532_STARTCODE2 &&
                  ack[3] == 0x00             &&
                  ack[4] == 0xFF             &&
                  ack[5] == PN532_POSTAMBLE);
    if (!valid) {
        LOG_W(TAG, "Unexpected ACK bytes: %02X %02X %02X %02X %02X %02X",
              ack[0], ack[1], ack[2], ack[3], ack[4], ack[5]);
        // Do not hard-fail — proceed and let readResponse validate the frame.
    }
    return true;
}

bool NFCReader::writeCommand(uint8_t* cmd, uint8_t cmdLen) {
    writeFrame(cmd, cmdLen);

    // Read and consume the ACK frame — this clears the READY flag so that
    // the subsequent readResponse call sees the actual command response.
    delay(1);
    return readACK();
}

bool NFCReader::readResponse(uint8_t* response, uint8_t* responseLen, uint16_t timeout) {
    return readFrame(response, responseLen, timeout);
}

bool NFCReader::detectCard(uint8_t* uid, uint8_t* uidLength) {
    if (!_initialized) {
        LOG_W(TAG, "detectCard called but PN532 not initialized");
        return false;
    }

    LOG_D(TAG, "detectCard(): issuing INLISTPASSIVETARGET");

    // InListPassiveTarget command
    // Baud rate 106 kbps, type A (ISO14443A)
    uint8_t cmd[] = {PN532_COMMAND_INLISTPASSIVETARGET, 0x01, 0x00};

    if (!writeCommand(cmd, sizeof(cmd))) {
        LOG_E(TAG, "detectCard: writeCommand failed");
        spiDiagnostic();
        return false;
    }
    LOG_D(TAG, "detectCard: command sent successfully");

    uint8_t response[40];  // Must be large enough for 7-byte UID + ATS (YubiKey adds ~8 ATS bytes)
    uint8_t responseLen = sizeof(response);

    if (!readResponse(response, &responseLen, NFC_DETECT_TIMEOUT_MS)) {
        // Timeout with no card is the normal idle case — keep at DEBUG to avoid spam.
        // If responseLen > 0 something was partially received, which is worth seeing.
        if (responseLen > 0) {
            LOG_W(TAG, "detectCard: partial response (%u bytes) — frame/checksum error", responseLen);
        } else {
            LOG_D(TAG, "detectCard: no card in field (timeout)");
        }
        return false;
    }
    LOG_I(TAG, "detectCard: got %u bytes in response", responseLen);

    // dump raw response bytes for tracing
    {
        char buf[128] = {0};
        for (uint8_t i = 0; i < responseLen; i++) {
            char tmp[5];
            snprintf(tmp, sizeof(tmp), " %02X", response[i]);
            strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
        }
        LOG_D(TAG, "detectCard: response bytes:%s", buf);
    }

    // Verify response
    // response[0] = command response code
    // response[1] = number of targets found
    if (responseLen < 2 || response[0] != PN532_RESPONSE_INLISTPASSIVETARGET) {
        LOG_W(TAG, "detectCard: invalid response code 0x%02X len=%u", response[0], responseLen);
        return false;
    }

    if (response[1] != 1) {
        LOG_W(TAG, "detectCard: unexpected target count=%u (SAK=0x%02X)", response[1], responseLen > 5 ? response[5] : 0xFF);
        return false;  // No card found or more than one
    }

    // Check SAK (Select Acknowledge) to filter out non-FIDO2 devices.
    // FIDO2/CTAP2 requires ISO 14443-4 (T=CL) transport, indicated by SAK bit 5 (0x20).
    // YubiKey 5 NFC: SAK=0x20. MiFARE Classic: SAK=0x08 (no bit 5). MiFARE Mini: SAK=0x18.
    uint8_t sak = responseLen > 5 ? response[5] : 0x00;
    if (!(sak & 0x20)) {
        LOG_W(TAG, "detectCard: rejected non-ISO14443-4 device (SAK=0x%02X)", sak);
        return false;
    }

    // Parse UID
    // response[2] = target number
    // response[3] = SENS_RES byte 1
    // response[4] = SENS_RES byte 2
    // response[5] = SEL_RES
    // response[6] = UID length
    // response[7...] = UID

    *uidLength = response[6];
    if (*uidLength > 10) *uidLength = 10;  // Limit

    memcpy(uid, &response[7], *uidLength);

    // log UID as hex
    {
        char uidBuf[3 * 10 + 1] = {0};
        for (uint8_t i = 0; i < *uidLength; i++) {
            char tmp[4];
            snprintf(tmp, sizeof(tmp), "%02X", uid[i]);
            strncat(uidBuf, tmp, sizeof(uidBuf) - strlen(uidBuf) - 1);
        }
        LOG_D(TAG, "detectCard: UID len=%u uid=%s", *uidLength, uidBuf);
    }

    return true;
}

bool NFCReader::transceive(uint8_t* sendBuf, uint8_t sendLen,
                           uint8_t* recvBuf, uint8_t* recvLen,
                           uint16_t timeout) {
    if (!_initialized) return false;

    // Build InDataExchange command
    uint8_t cmd[sendLen + 2];
    cmd[0] = PN532_COMMAND_INDATAEXCHANGE;
    cmd[1] = 0x01;  // Target number
    memcpy(&cmd[2], sendBuf, sendLen);

    if (!writeCommand(cmd, sendLen + 2)) {
        LOG_E(TAG, "Error in transceive write");
        return false;
    }

    // PN532 normal frame: LEN is uint8_t max 255, minus 1 for TFI = 254 data bytes.
    // FIDO2 GetAssertion responses are ~120-200 bytes, well within this limit.
    uint8_t response[254];
    uint8_t responseLen = sizeof(response);  // 254, fits in uint8_t

    if (!readResponse(response, &responseLen, timeout)) {
        LOG_E(TAG, "Error in transceive read");
        return false;
    }

    // Verify response
    if (responseLen < 2 || response[0] != PN532_RESPONSE_INDATAEXCHANGE) {
        LOG_E(TAG, "Invalid response: 0x%02X", response[0]);
        return false;
    }

    // response[1] = status (0x00 = OK)
    if (response[1] != 0x00) {
        LOG_E(TAG, "Error status: 0x%02X", response[1]);
        return false;
    }

    // Copy response data
    uint8_t dataLen = responseLen - 2;
    if (dataLen > *recvLen) dataLen = *recvLen;

    memcpy(recvBuf, &response[2], dataLen);
    *recvLen = dataLen;

    return true;
}

bool NFCReader::sendAPDU(uint8_t* apdu, size_t apduLen,
                         uint8_t* response, size_t* responseLen) {
    // Convert size_t to uint8_t for low-level transceive
    // APDU commands are typically < 255 bytes
    uint8_t recvLen = (*responseLen > 255) ? 255 : (uint8_t)*responseLen;
    bool result = transceive(apdu, (uint8_t)apduLen, response, &recvLen, NFC_COMMAND_TIMEOUT_MS);
    *responseLen = recvLen;
    return result;
}

uint32_t NFCReader::getFirmwareVersion() {
    uint8_t cmd[] = {PN532_COMMAND_GETFIRMWAREVERSION};

    if (!writeCommand(cmd, sizeof(cmd))) {
        return 0;
    }

    uint8_t response[12];
    uint8_t responseLen = sizeof(response);

    if (!readResponse(response, &responseLen, 1000)) {
        return 0;
    }

    if (responseLen < 4) {
        return 0;
    }

    // response[0] = IC
    // response[1] = Ver
    // response[2] = Rev
    // response[3] = Support
    return ((uint32_t)response[0] << 24) |
           ((uint32_t)response[1] << 16) |
           ((uint32_t)response[2] << 8) |
           response[3];
}

void NFCReader::powerDown() {
    uint8_t cmd[] = {PN532_COMMAND_POWERDOWN, 0x10};  // Wake on SPI
    writeCommand(cmd, sizeof(cmd));
}

void NFCReader::wakeUp() {
    // Per PN532 UM §7.2.11 (SPI interface), the host must hold nSS LOW for at
    // least T3 (≥ 2 ms) before each transaction.  After a cold-start or
    // power-down the SPI slave state machine also needs at least one dummy
    // byte with nSS LOW to synchronise, then a pause before the real command.
    _spi->beginTransaction(SPISettings(NFC_SPI_CLOCK, LSBFIRST, SPI_MODE0));
    digitalWrite(PIN_SPI_SS, LOW);
    delay(5);                       // > T3 spec
    _spi->transfer(0x00);           // dummy byte — synchronises SPI state machine
    digitalWrite(PIN_SPI_SS, HIGH);
    _spi->endTransaction();
    delay(100);                     // PN532 settling time after wake
}

bool NFCReader::isCardPresent() {
    uint8_t uid[10];
    uint8_t uidLen;
    return detectCard(uid, &uidLen);
}

void NFCReader::spiDiagnostic() {
    if (!_diagEnabled) {
        LOG_D(TAG, "spiDiagnostic() skipped (disabled)");
        return;
    }

    LOG_D(TAG, "--- SPI diagnostic start ---");

    // 1. Read the raw status register 5 times with no prior command.
    //    0x01 = READY, 0x00 = not ready, 0xFF = MISO floating/no connection.
    //    Build result into a single buffer so LOG_D prints it on one line.
    char statBuf[48] = "STATREAD x5:";
    for (uint8_t i = 0; i < 5; i++) {
        _spi->beginTransaction(SPISettings(NFC_SPI_CLOCK, LSBFIRST, SPI_MODE0));
        digitalWrite(PIN_SPI_SS, LOW);
        delay(2);
        _spi->transfer(PN532_SPI_STATREAD);
        uint8_t s = _spi->transfer(0x00);
        digitalWrite(PIN_SPI_SS, HIGH);
        _spi->endTransaction();
        char tmp[8];
        snprintf(tmp, sizeof(tmp), " 0x%02X", s);
        strncat(statBuf, tmp, sizeof(statBuf) - strlen(statBuf) - 1);
        delay(20);
    }
    LOG_D(TAG, "%s", statBuf);

    // 2. Send a raw GetFirmwareVersion frame, wait 300 ms (no STATREAD
    //    polling), then do a DATAREAD and dump the first 12 bytes.
    //    This bypasses all handshake logic and shows what the PN532 sends back.
    uint8_t cmd[] = {PN532_COMMAND_GETFIRMWAREVERSION};
    writeFrame(cmd, sizeof(cmd));
    delay(300);

    _spi->beginTransaction(SPISettings(NFC_SPI_CLOCK, LSBFIRST, SPI_MODE0));
    digitalWrite(PIN_SPI_SS, LOW);
    delay(2);
    _spi->transfer(PN532_SPI_DATAREAD);
    char dataBuf[64] = "raw DATAREAD x12:";
    for (uint8_t i = 0; i < 12; i++) {
        uint8_t b = _spi->transfer(0x00);
        char tmp[6];
        snprintf(tmp, sizeof(tmp), " %02X", b);
        strncat(dataBuf, tmp, sizeof(dataBuf) - strlen(dataBuf) - 1);
    }
    digitalWrite(PIN_SPI_SS, HIGH);
    _spi->endTransaction();
    LOG_D(TAG, "%s", dataBuf);

    LOG_D(TAG, "--- SPI diagnostic end ---");
    LOG_D(TAG, "Interpret: STATREAD 0xFF=MISO floating | 0x00=no response | 0x01=frame error");
    LOG_D(TAG, "Interpret: DATAREAD starting 00 00 FF = valid PN532 frame prefix");
}

void NFCReader::setDiagnosticEnabled(bool enabled) {
    _diagEnabled = enabled;
}

uint8_t NFCReader::calculateDCS(uint8_t* data, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return ~sum + 1;
}
