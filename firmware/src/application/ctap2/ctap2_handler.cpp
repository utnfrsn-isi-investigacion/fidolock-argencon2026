/**
 * FidoLock - CTAP2 Handler Module
 *
 * Implementation of the Client to Authenticator Protocol v2
 * for FIDO2 authentication with YubiKey devices.
 */

#include "ctap2_handler.h"
#include "../../utils/log.h"
#include <string.h>

static const char* TAG = "CTAP2";

// FIDO2 Application Identifier
static const uint8_t FIDO2_AID[] = {0xA0, 0x00, 0x00, 0x06, 0x47, 0x2F, 0x00, 0x01};

// CBOR major types
#define CBOR_UINT       0x00
#define CBOR_NEGINT     0x20
#define CBOR_BYTES      0x40
#define CBOR_TEXT       0x60
#define CBOR_ARRAY      0x80
#define CBOR_MAP        0xA0
#define CBOR_TAG        0xC0
#define CBOR_SIMPLE     0xE0

// Deepest nesting skipCBORItem will follow; authenticator responses nest at
// most two levels (e.g. 0x04 user entity → map → text/bytes).
static constexpr uint8_t kCBORMaxSkipDepth = 4;

// Advance *offset past one complete CBOR data item (header, payload and any
// nested items). Returns false on truncated input, indefinite lengths or
// excessive nesting, so callers can reject the response instead of
// misreading nested bytes as top-level map keys.
static bool skipCBORItem(const uint8_t* data, size_t dataLen, size_t* offset,
                         uint8_t depth = 0) {
    if (depth > kCBORMaxSkipDepth || *offset >= dataLen) return false;

    uint8_t major = data[*offset] & 0xE0;
    uint8_t info  = data[*offset] & 0x1F;
    (*offset)++;

    uint64_t value = info;
    if (info >= 24 && info <= 27) {
        size_t n = (size_t)1 << (info - 24);       // 1, 2, 4 or 8 bytes
        if (n > dataLen - *offset) return false;
        value = 0;
        for (size_t i = 0; i < n; i++) value = (value << 8) | data[(*offset)++];
    } else if (info > 27) {
        return false;                              // indefinite length / reserved
    }

    switch (major) {
        case CBOR_BYTES:
        case CBOR_TEXT:
            if (value > dataLen - *offset) return false;
            *offset += (size_t)value;
            return true;
        case CBOR_ARRAY:
        case CBOR_MAP: {
            // Each item takes at least one byte, so a count beyond the
            // remaining input is malformed; checking first bounds the loop.
            if (value > dataLen - *offset) return false;
            uint64_t items = (major == CBOR_MAP) ? value * 2 : value;
            for (uint64_t i = 0; i < items; i++) {
                if (!skipCBORItem(data, dataLen, offset, depth + 1)) return false;
            }
            return true;
        }
        case CBOR_TAG:
            return skipCBORItem(data, dataLen, offset, depth + 1);
        default:                                   // uint, negint, simple/float
            return true;
    }
}

// CTAP2 command bytes
#define CTAP2_CMD_BYTE  0x80
#define CTAP2_CONT_BYTE 0x00

// Worst-case sizing for the GetAssertion CBOR request and its APDU wrapper.
// The request carries no allowList (credentials are discoverable), so its
// size is fixed regardless of how many credentials are enrolled.
static constexpr size_t kGetAssertionCborMax =
      1                            // map(2) header
    + (1 + 2 + 64)                 // rpId: key + text header + rpId (generous)
    + (1 + 2 + 32);                // clientDataHash: key + bytes header + 32
// APDU = 4-byte header + 3-byte extended Lc + 1-byte CTAP cmd + CBOR + 2-byte Le.
static constexpr size_t kGetAssertionApduMax = kGetAssertionCborMax + 16;

bool CTAP2Handler::selectFIDO2Applet(INFCReader* nfc) {
    // Build SELECT APDU command
    // CLA INS P1  P2  Lc  DATA                                    Le
    // 00  A4  04  00  08  A0 00 00 06 47 2F 00 01                 00
    uint8_t selectCmd[] = {
        0x00, 0xA4, 0x04, 0x00,  // CLA, INS, P1, P2
        0x08,                     // Lc (length of AID)
        0xA0, 0x00, 0x00, 0x06, 0x47, 0x2F, 0x00, 0x01,  // FIDO2 AID
        0x00                      // Le (expected response length)
    };

    uint8_t response[64];
    size_t responseLen = sizeof(response);

    if (!nfc->sendAPDU(selectCmd, sizeof(selectCmd), response, &responseLen)) {
        LOG_E(TAG, "Failed to send SELECT command");
        return false;
    }

    // Check status word (last 2 bytes should be 90 00)
    if (responseLen < 2) {
        LOG_E(TAG, "Invalid SELECT response length");
        return false;
    }

    uint16_t sw = (response[responseLen - 2] << 8) | response[responseLen - 1];
    if (sw != 0x9000) {
        LOG_E(TAG, "SELECT failed with SW=%04X", sw);
        return false;
    }

    LOG_I(TAG, "FIDO2 applet selected successfully");
    return true;
}

int CTAP2Handler::getInfo(INFCReader* nfc, uint8_t* info, size_t* infoLen) {
    if (!selectFIDO2Applet(nfc)) {
        return CTAP2_ERR_INVALID_COMMAND;
    }

    // Build CTAP2 GetInfo command
    // The command is wrapped in an extended APDU
    uint8_t ctapCmd[] = {
        0x80, 0x10, 0x00, 0x00,  // CLA, INS (NFCCTAP_MSG), P1, P2
        0x00, 0x00, 0x01,        // Lc (extended length = 1)
        CTAP2_CMD_GET_INFO,      // CTAP2 command
        0x00, 0x00               // Le (extended, max response)
    };

    uint8_t response[256];
    size_t responseLen = sizeof(response);

    if (!nfc->sendAPDU(ctapCmd, sizeof(ctapCmd), response, &responseLen)) {
        LOG_E(TAG, "Failed to send GetInfo command");
        return CTAP2_ERR_TIMEOUT;
    }

    // Check CTAP2 status byte
    if (responseLen < 3) {
        return CTAP2_ERR_INVALID_LENGTH;
    }

    uint8_t ctapStatus = response[0];
    if (ctapStatus != CTAP2_OK) {
        LOG_E(TAG, "GetInfo returned status 0x%02X", ctapStatus);
        return ctapStatus;
    }

    // Copy response data (skip status byte, exclude SW)
    size_t dataLen = responseLen - 3;  // -1 status, -2 SW
    if (dataLen > *infoLen) {
        dataLen = *infoLen;
    }
    memcpy(info, &response[1], dataLen);
    *infoLen = dataLen;

    return CTAP2_OK;
}

int CTAP2Handler::getAssertion(INFCReader* nfc,
                                const char* rpId,
                                uint8_t* clientDataHash,
                                CTAP2Response* response) {
    if (!selectFIDO2Applet(nfc)) {
        return CTAP2_ERR_INVALID_COMMAND;
    }

    // Build CBOR-encoded GetAssertion request
    uint8_t cborBuffer[kGetAssertionCborMax];
    size_t cborLen = buildGetAssertionCBOR(rpId, clientDataHash, cborBuffer,
                                           sizeof(cborBuffer));
    if (cborLen == 0) {
        LOG_E(TAG, "Failed to encode GetAssertion CBOR");
        return CTAP2_ERR_INVALID_PARAMETER;
    }

    // Build APDU with CTAP2 command
    uint8_t apdu[kGetAssertionApduMax];
    size_t apduLen = 0;

    // APDU header for extended length
    apdu[apduLen++] = 0x80;  // CLA
    apdu[apduLen++] = 0x10;  // INS (NFCCTAP_MSG)
    apdu[apduLen++] = 0x00;  // P1
    apdu[apduLen++] = 0x00;  // P2

    // Extended length Lc
    uint16_t cmdLen = cborLen + 1;  // +1 for CTAP2 command byte
    apdu[apduLen++] = 0x00;
    apdu[apduLen++] = (cmdLen >> 8) & 0xFF;
    apdu[apduLen++] = cmdLen & 0xFF;

    // CTAP2 command
    apdu[apduLen++] = CTAP2_CMD_GET_ASSERTION;

    // CBOR data
    memcpy(&apdu[apduLen], cborBuffer, cborLen);
    apduLen += cborLen;

    // Le (extended, expect response)
    apdu[apduLen++] = 0x00;
    apdu[apduLen++] = 0x00;

    // Send command
    uint8_t respBuffer[256];
    size_t respLen = sizeof(respBuffer);

    LOG_I(TAG, "Sending GetAssertion command...");

    if (!nfc->sendAPDU(apdu, apduLen, respBuffer, &respLen)) {
        LOG_E(TAG, "Failed to send GetAssertion");
        return CTAP2_ERR_TIMEOUT;
    }

    // Parse response
    if (respLen < 3) {
        LOG_E(TAG, "Response too short");
        return CTAP2_ERR_INVALID_LENGTH;
    }

    // Check status word
    uint16_t sw = (respBuffer[respLen - 2] << 8) | respBuffer[respLen - 1];
    if (sw != 0x9000) {
        LOG_E(TAG, "GetAssertion SW=%04X", sw);
        return CTAP2_ERR_NOT_ALLOWED;
    }

    // Check CTAP2 status
    response->status = respBuffer[0];
    if (response->status != CTAP2_OK) {
        LOG_E(TAG, "GetAssertion status=0x%02X", response->status);
        return response->status;
    }

    // Parse CBOR response
    if (!parseAssertionResponse(&respBuffer[1], respLen - 3, response)) {
        LOG_E(TAG, "Failed to parse assertion response");
        return CTAP2_ERR_INVALID_PARAMETER;
    }

    LOG_I(TAG, "GetAssertion successful");
    return CTAP2_OK;
}

int CTAP2Handler::makeCredential(INFCReader* nfc,
                                  const char* rpId,
                                  const char* rpName,
                                  uint8_t* userId,
                                  uint8_t userIdLen,
                                  const char* userName,
                                  uint8_t* clientDataHash,
                                  uint8_t* credentialId,
                                  uint8_t* credentialIdLen,
                                  uint8_t* publicKey) {
    if (!selectFIDO2Applet(nfc)) {
        return CTAP2_ERR_INVALID_COMMAND;
    }

    // Build CBOR-encoded MakeCredential request
    uint8_t cborBuffer[320];
    size_t cborLen = buildMakeCredentialCBOR(rpId, rpName, userId, userIdLen,
                                              userName, clientDataHash, cborBuffer);

    // Build APDU
    uint8_t apdu[350];
    size_t apduLen = 0;

    apdu[apduLen++] = 0x80;  // CLA
    apdu[apduLen++] = 0x10;  // INS
    apdu[apduLen++] = 0x00;  // P1
    apdu[apduLen++] = 0x00;  // P2

    uint16_t cmdLen = cborLen + 1;
    apdu[apduLen++] = 0x00;
    apdu[apduLen++] = (cmdLen >> 8) & 0xFF;
    apdu[apduLen++] = cmdLen & 0xFF;

    apdu[apduLen++] = CTAP2_CMD_MAKE_CREDENTIAL;
    memcpy(&apdu[apduLen], cborBuffer, cborLen);
    apduLen += cborLen;

    apdu[apduLen++] = 0x00;
    apdu[apduLen++] = 0x00;

    // Send command
    uint8_t respBuffer[512];
    size_t respLen = sizeof(respBuffer);

    LOG_I(TAG, "Sending MakeCredential command...");
    LOG_I(TAG, "Touch your YubiKey to confirm...");

    if (!nfc->sendAPDU(apdu, apduLen, respBuffer, &respLen)) {
        LOG_E(TAG, "Failed to send MakeCredential");
        return CTAP2_ERR_TIMEOUT;
    }

    // Check status
    if (respLen < 3) {
        return CTAP2_ERR_INVALID_LENGTH;
    }

    uint16_t sw = (respBuffer[respLen - 2] << 8) | respBuffer[respLen - 1];
    if (sw != 0x9000) {
        LOG_E(TAG, "MakeCredential SW=%04X", sw);
        return CTAP2_ERR_NOT_ALLOWED;
    }

    uint8_t ctapStatus = respBuffer[0];
    if (ctapStatus != CTAP2_OK) {
        LOG_E(TAG, "MakeCredential status=0x%02X", ctapStatus);
        return ctapStatus;
    }

    // Parse response to extract credential ID and public key
    if (!parseCredentialResponse(&respBuffer[1], respLen - 3,
                                  credentialId, credentialIdLen, publicKey)) {
        LOG_E(TAG, "Failed to parse credential response");
        return CTAP2_ERR_INVALID_PARAMETER;
    }

    LOG_I(TAG, "MakeCredential successful");
    return CTAP2_OK;
}

// CBOR encoding helpers

size_t CTAP2Handler::encodeCBORUint(uint8_t* buffer, uint32_t value) {
    if (value <= 23) {
        buffer[0] = CBOR_UINT | value;
        return 1;
    } else if (value <= 0xFF) {
        buffer[0] = CBOR_UINT | 24;
        buffer[1] = value;
        return 2;
    } else if (value <= 0xFFFF) {
        buffer[0] = CBOR_UINT | 25;
        buffer[1] = (value >> 8) & 0xFF;
        buffer[2] = value & 0xFF;
        return 3;
    } else {
        buffer[0] = CBOR_UINT | 26;
        buffer[1] = (value >> 24) & 0xFF;
        buffer[2] = (value >> 16) & 0xFF;
        buffer[3] = (value >> 8) & 0xFF;
        buffer[4] = value & 0xFF;
        return 5;
    }
}

size_t CTAP2Handler::encodeCBORString(uint8_t* buffer, const char* str) {
    size_t len = strlen(str);
    size_t offset = 0;

    if (len <= 23) {
        buffer[offset++] = CBOR_TEXT | len;
    } else if (len <= 0xFF) {
        buffer[offset++] = CBOR_TEXT | 24;
        buffer[offset++] = len;
    } else {
        buffer[offset++] = CBOR_TEXT | 25;
        buffer[offset++] = (len >> 8) & 0xFF;
        buffer[offset++] = len & 0xFF;
    }

    memcpy(&buffer[offset], str, len);
    return offset + len;
}

size_t CTAP2Handler::encodeCBORBytes(uint8_t* buffer, uint8_t* data, size_t len) {
    size_t offset = 0;

    if (len <= 23) {
        buffer[offset++] = CBOR_BYTES | len;
    } else if (len <= 0xFF) {
        buffer[offset++] = CBOR_BYTES | 24;
        buffer[offset++] = len;
    } else {
        buffer[offset++] = CBOR_BYTES | 25;
        buffer[offset++] = (len >> 8) & 0xFF;
        buffer[offset++] = len & 0xFF;
    }

    memcpy(&buffer[offset], data, len);
    return offset + len;
}

size_t CTAP2Handler::encodeCBORMap(uint8_t* buffer, uint8_t numItems) {
    if (numItems <= 23) {
        buffer[0] = CBOR_MAP | numItems;
        return 1;
    } else {
        buffer[0] = CBOR_MAP | 24;
        buffer[1] = numItems;
        return 2;
    }
}

size_t CTAP2Handler::encodeCBORArray(uint8_t* buffer, uint8_t numItems) {
    if (numItems <= 23) {
        buffer[0] = CBOR_ARRAY | numItems;
        return 1;
    } else {
        buffer[0] = CBOR_ARRAY | 24;
        buffer[1] = numItems;
        return 2;
    }
}

size_t CTAP2Handler::buildGetAssertionCBOR(const char* rpId,
                                            uint8_t* clientDataHash,
                                            uint8_t* buffer,
                                            size_t bufCap) {
    size_t offset = 0;

    // GetAssertion request is a CBOR map with:
    // 0x01: rpId (string)
    // 0x02: clientDataHash (bytes, 32)
    // No allowList (0x03): the authenticator selects its own discoverable
    // (resident) credential for rpId.

    // Bound the whole request before writing it. Each encodeCBOR* helper
    // writes at most a few header bytes plus its payload, so these
    // conservative upper bounds guarantee no write runs past bufCap.
    const size_t rpIdLen = strlen(rpId);
    const size_t requestMax = 1                 // map(2) header
                            + (1 + 3 + rpIdLen) // rpId key + text header + rpId
                            + (1 + 3 + 32);     // clientDataHash key + header + 32
    if (requestMax > bufCap) {
        return 0;
    }

    offset += encodeCBORMap(&buffer[offset], 2);

    // Key 0x01: rpId
    offset += encodeCBORUint(&buffer[offset], 0x01);
    offset += encodeCBORString(&buffer[offset], rpId);

    // Key 0x02: clientDataHash
    offset += encodeCBORUint(&buffer[offset], 0x02);
    offset += encodeCBORBytes(&buffer[offset], clientDataHash, 32);

    return offset;
}

size_t CTAP2Handler::buildMakeCredentialCBOR(const char* rpId,
                                              const char* rpName,
                                              uint8_t* userId,
                                              uint8_t userIdLen,
                                              const char* userName,
                                              uint8_t* clientDataHash,
                                              uint8_t* buffer) {
    size_t offset = 0;

    // MakeCredential request is a CBOR map with:
    // 0x01: clientDataHash (bytes, 32)
    // 0x02: rp (map with id and name)
    // 0x03: user (map with id, name, displayName)
    // 0x04: pubKeyCredParams (array of algorithms)

    // Map with 4 items
    offset += encodeCBORMap(&buffer[offset], 4);

    // Key 0x01: clientDataHash
    offset += encodeCBORUint(&buffer[offset], 0x01);
    offset += encodeCBORBytes(&buffer[offset], clientDataHash, 32);

    // Key 0x02: rp
    offset += encodeCBORUint(&buffer[offset], 0x02);
    offset += encodeCBORMap(&buffer[offset], 2);
    offset += encodeCBORString(&buffer[offset], "id");
    offset += encodeCBORString(&buffer[offset], rpId);
    offset += encodeCBORString(&buffer[offset], "name");
    offset += encodeCBORString(&buffer[offset], rpName);

    // Key 0x03: user
    offset += encodeCBORUint(&buffer[offset], 0x03);
    offset += encodeCBORMap(&buffer[offset], 3);
    offset += encodeCBORString(&buffer[offset], "id");
    offset += encodeCBORBytes(&buffer[offset], userId, userIdLen);
    offset += encodeCBORString(&buffer[offset], "name");
    offset += encodeCBORString(&buffer[offset], userName);
    offset += encodeCBORString(&buffer[offset], "displayName");
    offset += encodeCBORString(&buffer[offset], userName);

    // Key 0x04: pubKeyCredParams - request ES256 algorithm
    offset += encodeCBORUint(&buffer[offset], 0x04);
    offset += encodeCBORArray(&buffer[offset], 1);
    offset += encodeCBORMap(&buffer[offset], 2);
    offset += encodeCBORString(&buffer[offset], "type");
    offset += encodeCBORString(&buffer[offset], "public-key");
    offset += encodeCBORString(&buffer[offset], "alg");
    // ES256 = -7 (CBOR negative integer)
    buffer[offset++] = CBOR_NEGINT | 6;  // -7 encoded as 0x26

    return offset;
}

bool CTAP2Handler::parseAssertionResponse(uint8_t* data, size_t dataLen,
                                           CTAP2Response* response) {
    if (dataLen < 10) {
        return false;
    }

    // Response is a CBOR map with:
    // 0x01: credential (always present without an allowList)
    // 0x02: authData
    // 0x03: signature
    // 0x04: user entity (skipped)
    // 0x05: numberOfCredentials (skipped)

    size_t offset = 0;

    // Check for map
    if ((data[offset] & 0xE0) != CBOR_MAP) {
        LOG_E(TAG, "Expected CBOR map in response");
        return false;
    }

    uint8_t numItems = data[offset] & 0x1F;
    offset++;

    response->userPresent = false;
    response->counter = 0;
    response->signatureLen = 0;
    response->credentialIdLen = 0;

    for (uint8_t i = 0; i < numItems && offset < dataLen; i++) {
        // Read key
        uint8_t key = data[offset++];

        if (key == 0x01) {
            // credential descriptor (map with "type" and "id")
            if (offset >= dataLen || (data[offset] & 0xE0) != CBOR_MAP) {
                return false;
            }
            uint8_t mapItems = data[offset++] & 0x1F;
            for (uint8_t j = 0; j < mapItems && offset < dataLen; j++) {
                // key must be a text string
                if ((data[offset] & 0xE0) != CBOR_TEXT) { offset++; continue; }
                uint8_t klen = data[offset++] & 0x1F;
                if (klen > dataLen - offset) return false;
                bool isId = (klen == 2 &&
                             data[offset] == 'i' && data[offset + 1] == 'd');
                offset += klen;
                // value
                if (offset >= dataLen) return false;
                if ((data[offset] & 0xE0) == CBOR_BYTES) {
                    uint8_t vlen = data[offset] & 0x1F;
                    if (vlen == 24) {
                        if (offset + 1 >= dataLen) return false;
                        vlen = data[++offset];
                    }
                    offset++;
                    if (vlen > dataLen - offset) return false;
                    if (isId && vlen <= sizeof(response->credentialId)) {
                        memcpy(response->credentialId, &data[offset], vlen);
                        response->credentialIdLen = vlen;
                    }
                    offset += vlen;
                } else if ((data[offset] & 0xE0) == CBOR_TEXT) {
                    uint8_t vlen = data[offset++] & 0x1F;
                    if (vlen > dataLen - offset) return false;
                    offset += vlen;
                } else {
                    offset++;
                }
            }

        } else if (key == 0x02) {
            // authData (bytes)
            if (offset >= dataLen || (data[offset] & 0xE0) != CBOR_BYTES) {
                return false;
            }
            uint8_t authDataLen = data[offset] & 0x1F;
            if (authDataLen == 24) {
                if (offset + 1 >= dataLen) return false;
                authDataLen = data[++offset];
            }
            offset++;
            if (authDataLen > dataLen - offset) {
                return false;
            }

            // Copy authData (minimum 37 bytes for RP ID hash + flags + counter)
            if (authDataLen >= 37) {
                memcpy(response->authData, &data[offset], 37);

                // Extract rpId hash (bytes 0..31)
                memcpy(response->rpIdHash, &data[offset], 32);

                // Extract flags (byte 32)
                uint8_t flags = response->authData[32];
                response->userPresent = (flags & 0x01) != 0;

                // Extract counter (bytes 33-36, big-endian)
                response->counter = ((uint32_t)response->authData[33] << 24) |
                                   ((uint32_t)response->authData[34] << 16) |
                                   ((uint32_t)response->authData[35] << 8) |
                                   response->authData[36];
            }
            offset += authDataLen;

        } else if (key == 0x03) {
            // signature (bytes)
            if (offset >= dataLen || (data[offset] & 0xE0) != CBOR_BYTES) {
                return false;
            }
            uint8_t sigLen = data[offset] & 0x1F;
            if (sigLen == 24) {
                if (offset + 1 >= dataLen) return false;
                sigLen = data[++offset];
            }
            offset++;
            if (sigLen > dataLen - offset) {
                return false;
            }

            if (sigLen <= sizeof(response->signature)) {
                memcpy(response->signature, &data[offset], sigLen);
                response->signatureLen = sigLen;
            }
            offset += sigLen;

        } else {
            // Skip unknown keys whole, including nested maps such as the
            // 0x04 user entity, so their contents are never read as keys.
            if (!skipCBORItem(data, dataLen, &offset)) {
                return false;
            }
        }
    }

    return response->signatureLen > 0;
}

bool CTAP2Handler::parseCredentialResponse(uint8_t* data, size_t dataLen,
                                            uint8_t* credentialId,
                                            uint8_t* credentialIdLen,
                                            uint8_t* publicKey) {
    // MakeCredential response contains:
    // 0x01: fmt (attestation format string)
    // 0x02: authData (contains credential ID and public key)
    // 0x03: attStmt (attestation statement)

    if (dataLen == 0 || (data[0] & 0xE0) != CBOR_MAP) {
        return false;
    }

    size_t offset = 0;
    uint8_t numItems = data[offset] & 0x1F;
    offset++;

    for (uint8_t i = 0; i < numItems && offset < dataLen; i++) {
        uint8_t key = data[offset++];

        if (key == 0x02) {
            // authData (bytes)
            if (offset >= dataLen || (data[offset] & 0xE0) != CBOR_BYTES) {
                return false;
            }

            uint16_t authDataLen = data[offset] & 0x1F;
            if (authDataLen == 24) {
                if (offset + 1 >= dataLen) return false;
                authDataLen = data[++offset];
            } else if (authDataLen == 25) {
                if (offset + 2 >= dataLen) return false;
                authDataLen = (uint16_t)(data[offset + 1] << 8) | data[offset + 2];
                offset += 2;
            }
            offset++;
            if (authDataLen > dataLen - offset) {
                return false;
            }
            const size_t authEnd = offset + authDataLen;

            // authData structure:
            // [0-31]: RP ID hash (32 bytes)
            // [32]: flags
            // [33-36]: counter (4 bytes)
            // [37-52]: AAGUID (16 bytes)
            // [53-54]: credential ID length (2 bytes, big-endian)
            // [55-...]: credential ID
            // [...]: COSE public key
            if (authDataLen < 55) {
                return false;
            }

            uint8_t flags = data[offset + 32];
            if (!(flags & 0x40)) {  // AT (attested credential data) flag
                return false;
            }

            // Credential ID length, after the 16-byte AAGUID at offset 37.
            size_t credIdLenOffset = offset + 37 + 16;
            uint16_t credIdLen = (uint16_t)(data[credIdLenOffset] << 8) |
                                 data[credIdLenOffset + 1];

            // The credential ID (and its 2-byte length prefix) must lie within
            // authData; reject anything that would read past the buffer.
            size_t credIdStart = credIdLenOffset + 2;
            if (credIdLen > 64 || credIdStart + credIdLen > authEnd) {
                return false;
            }

            memcpy(credentialId, &data[credIdStart], credIdLen);
            *credentialIdLen = (uint8_t)credIdLen;

            // COSE_Key (EC2) follows the credential ID; convert to the 65-byte
            // uncompressed representation 0x04 || x || y.
            publicKey[0] = 0x04;
            size_t coseOffset = credIdStart + credIdLen;
            bool hasX = false, hasY = false;

            if (coseOffset < authEnd && (data[coseOffset] & 0xE0) == CBOR_MAP) {
                uint8_t mapItems = data[coseOffset] & 0x1F;
                coseOffset++;

                for (uint8_t j = 0; j < mapItems && coseOffset < authEnd; j++) {
                    int8_t coseKey;
                    if ((data[coseOffset] & 0xE0) == CBOR_NEGINT) {
                        coseKey = -(int8_t)(data[coseOffset] & 0x1F) - 1;
                    } else {
                        coseKey = (int8_t)(data[coseOffset] & 0x1F);
                    }
                    coseOffset++;
                    if (coseOffset >= authEnd) break;

                    if (coseKey == -2 || coseKey == -3) {  // x or y coordinate
                        if ((data[coseOffset] & 0xE0) != CBOR_BYTES) break;
                        uint8_t clen = data[coseOffset] & 0x1F;
                        if (clen == 24) {
                            if (coseOffset + 1 >= authEnd) break;
                            clen = data[++coseOffset];
                        }
                        coseOffset++;
                        if (clen > authEnd - coseOffset) break;
                        if (clen == 32) {
                            memcpy(&publicKey[coseKey == -2 ? 1 : 33],
                                   &data[coseOffset], 32);
                            if (coseKey == -2) hasX = true; else hasY = true;
                        }
                        coseOffset += clen;
                    } else {
                        // Skip this value
                        if ((data[coseOffset] & 0xE0) == CBOR_BYTES ||
                            (data[coseOffset] & 0xE0) == CBOR_TEXT) {
                            uint8_t len = data[coseOffset] & 0x1F;
                            if (len == 24) {
                                if (coseOffset + 1 >= authEnd) break;
                                len = data[++coseOffset];
                            }
                            coseOffset += 1 + len;
                        } else {
                            coseOffset++;
                        }
                    }
                }
            }

            // Require both coordinates: a key missing x or y is unusable.
            return hasX && hasY;

        } else {
            // Skip other top-level keys
            if (offset >= dataLen) return false;
            if ((data[offset] & 0xE0) == CBOR_BYTES ||
                (data[offset] & 0xE0) == CBOR_TEXT) {
                uint8_t len = data[offset] & 0x1F;
                if (len == 24) {
                    if (offset + 1 >= dataLen) return false;
                    len = data[++offset];
                }
                offset += 1 + len;
            } else if ((data[offset] & 0xE0) == CBOR_MAP) {
                // Skip map (simplified)
                uint8_t items = data[offset++] & 0x1F;
                for (uint16_t j = 0; j < (uint16_t)items * 2 && offset < dataLen; j++) {
                    if ((data[offset] & 0xE0) == CBOR_BYTES ||
                        (data[offset] & 0xE0) == CBOR_TEXT) {
                        uint8_t len = data[offset] & 0x1F;
                        if (len == 24) {
                            if (offset + 1 >= dataLen) return false;
                            len = data[++offset];
                        }
                        offset += 1 + len;
                    } else {
                        offset++;
                    }
                }
            } else {
                offset++;
            }
        }
    }

    return false;
}
