/**
 * FidoLock - Driven Port: Credential Store
 *
 * Driven port (out): the SPI the application calls out to storage
 * infrastructure. Rule 3 of Ports & Adapters — Cockburn 2005.
 */

#ifndef FIDOLOCK_PORTS_OUT_I_CREDENTIAL_STORE_H
#define FIDOLOCK_PORTS_OUT_I_CREDENTIAL_STORE_H

#include <stdint.h>
#include <stddef.h>
#include <functional>
#include "../../domain/credential.h"

class ICredentialStore {
public:
    virtual ~ICredentialStore() = default;

    virtual bool init() = 0;

    virtual StoredCredential* findByCredentialId(const uint8_t* credentialId,
                                                  uint8_t credentialIdLen) = 0;

    virtual bool save(const StoredCredential* credential) = 0;

    virtual bool remove(const uint8_t* credentialId, uint8_t credentialIdLen) = 0;

    virtual uint32_t getCount() = 0;

    virtual void listAll(std::function<void(StoredCredential*)> callback) = 0;

    virtual bool format() = 0;

    virtual uint32_t getFreeSpace() = 0;

    virtual bool updateCounter(const uint8_t* credentialId, uint8_t credentialIdLen,
                               uint32_t newCounter) = 0;
};

#endif // FIDOLOCK_PORTS_OUT_I_CREDENTIAL_STORE_H
