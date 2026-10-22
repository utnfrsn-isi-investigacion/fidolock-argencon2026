/**
 * FidoLock - Driving Port: CTAP2 Handler
 *
 * Driving port (in): the API the application exposes for CTAP2 operations.
 * Rule 3 of Ports & Adapters — Cockburn 2005.
 */

#ifndef FIDOLOCK_PORTS_IN_I_CTAP2_HANDLER_H
#define FIDOLOCK_PORTS_IN_I_CTAP2_HANDLER_H

#include <stdint.h>
#include <stddef.h>
#include "../../domain/ctap2_types.h"
#include "../out/i_nfc_reader.h"

class ICTAP2Handler {
public:
    virtual ~ICTAP2Handler() = default;

    virtual int getInfo(INFCReader* nfc, uint8_t* info, size_t* infoLen) = 0;

    // Sends no allowList: the authenticator picks its own discoverable
    // (resident) credential for rpId and reports which one in the response.
    virtual int getAssertion(INFCReader* nfc,
                             const char* rpId,
                             uint8_t* clientDataHash,
                             CTAP2Response* response) = 0;

    virtual int makeCredential(INFCReader* nfc,
                               const char* rpId,
                               const char* rpName,
                               uint8_t* userId,
                               uint8_t userIdLen,
                               const char* userName,
                               uint8_t* clientDataHash,
                               uint8_t* credentialId,
                               uint8_t* credentialIdLen,
                               uint8_t* publicKey) = 0;
};

#endif // FIDOLOCK_PORTS_IN_I_CTAP2_HANDLER_H
