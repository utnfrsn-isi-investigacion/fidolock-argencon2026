/**
 * FidoLock - CTAP2 Handler Module
 *
 * Implementation of the Client to Authenticator Protocol v2
 */

#ifndef FIDOLOCK_CTAP2_HANDLER_H
#define FIDOLOCK_CTAP2_HANDLER_H

#include "../../config.h"
#include "../../ports/out/i_nfc_reader.h"
#include "../../ports/in/i_ctap2_handler.h"

// CTAP2 Commands
#define CTAP2_CMD_MAKE_CREDENTIAL   0x01
#define CTAP2_CMD_GET_ASSERTION     0x02
#define CTAP2_CMD_GET_INFO          0x04
#define CTAP2_CMD_CLIENT_PIN        0x06
#define CTAP2_CMD_RESET             0x07
#define CTAP2_CMD_GET_NEXT_ASSERTION 0x08

// FIDO2 APDU
#define FIDO2_AID_LEN               8

class CTAP2Handler : public ICTAP2Handler {
public:
    /**
     * Get authenticator information
     */
    int getInfo(INFCReader* nfc, uint8_t* info, size_t* infoLen) override;

    /**
     * Request assertion (authentication)
     */
    int getAssertion(INFCReader* nfc,
                     const char* rpId,
                     uint8_t* clientDataHash,
                     CTAP2Response* response) override;

    /**
     * Create new credential (registration)
     */
    int makeCredential(INFCReader* nfc,
                       const char* rpId,
                       const char* rpName,
                       uint8_t* userId,
                       uint8_t userIdLen,
                       const char* userName,
                       uint8_t* clientDataHash,
                       uint8_t* credentialId,
                       uint8_t* credentialIdLen,
                       uint8_t* publicKey) override;

private:
    // Select FIDO2 applet
    bool selectFIDO2Applet(INFCReader* nfc);

    // Build CBOR command.
    // Returns the number of bytes written, or 0 if the encoded request would
    // not fit within bufCap (the caller must treat 0 as an error).
    size_t buildGetAssertionCBOR(const char* rpId,
                                  uint8_t* clientDataHash,
                                  uint8_t* buffer,
                                  size_t bufCap);

    size_t buildMakeCredentialCBOR(const char* rpId,
                                    const char* rpName,
                                    uint8_t* userId,
                                    uint8_t userIdLen,
                                    const char* userName,
                                    uint8_t* clientDataHash,
                                    uint8_t* buffer);

    // Parse CBOR response
    bool parseAssertionResponse(uint8_t* data, size_t dataLen,
                                 CTAP2Response* response);

    bool parseCredentialResponse(uint8_t* data, size_t dataLen,
                                  uint8_t* credentialId,
                                  uint8_t* credentialIdLen,
                                  uint8_t* publicKey);

    // CBOR Helpers
    size_t encodeCBORString(uint8_t* buffer, const char* str);
    size_t encodeCBORBytes(uint8_t* buffer, uint8_t* data, size_t len);
    size_t encodeCBORMap(uint8_t* buffer, uint8_t numItems);
    size_t encodeCBORArray(uint8_t* buffer, uint8_t numItems);
    size_t encodeCBORUint(uint8_t* buffer, uint32_t value);
};

#endif // FIDOLOCK_CTAP2_HANDLER_H
