/**
 * FidoLock - Credential Store Module
 *
 * Credential storage and management using LittleFS.
 * Implements ICredentialStore interface.
 */

#ifndef FIDOLOCK_CREDENTIAL_STORE_H
#define FIDOLOCK_CREDENTIAL_STORE_H

#include <Arduino.h>
#include <functional>
#include "../../config.h"
#include "../../ports/out/i_credential_store.h"

class CredentialStore : public ICredentialStore {
public:
    CredentialStore() : _cachedCredential{}, _cacheValid(false) {}

    bool init() override;
    StoredCredential* findByCredentialId(const uint8_t* credentialId,
                                         uint8_t credentialIdLen) override;
    bool save(const StoredCredential* credential) override;
    bool remove(const uint8_t* credentialId, uint8_t credentialIdLen) override;
    uint32_t getCount() override;
    void listAll(std::function<void(StoredCredential*)> callback) override;
    bool format() override;
    uint32_t getFreeSpace() override;
    bool updateCounter(const uint8_t* credentialId, uint8_t credentialIdLen,
                       uint32_t newCounter) override;

    /**
     * FNV-1a hash of credential ID bytes.
     * Public so that pure-logic unit tests can verify filename generation
     * without LittleFS. See ADR 004.
     */
    uint32_t hashCredentialId(const uint8_t* credentialId, uint8_t credentialIdLen);

    // Convenience alias (keeps call-sites in main.cpp unchanged)
    bool deleteByCredentialId(const uint8_t* credentialId, uint8_t credentialIdLen) {
        return remove(credentialId, credentialIdLen);
    }

private:
    StoredCredential _cachedCredential;
    bool _cacheValid;

    void getFilename(const uint8_t* credentialId, uint8_t credentialIdLen,
                     char* filename, size_t maxLen);
    bool loadFromFile(const char* filename, StoredCredential* credential);
    bool saveToFile(const char* filename, const StoredCredential* credential);
};

#endif // FIDOLOCK_CREDENTIAL_STORE_H
