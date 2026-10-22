/**
 * FidoLock - Driven Port: Crypto Engine
 *
 * Driven port (out): the SPI the application calls out to cryptographic
 * infrastructure. Rule 3 of Ports & Adapters — Cockburn 2005.
 */

#ifndef FIDOLOCK_PORTS_OUT_I_CRYPTO_ENGINE_H
#define FIDOLOCK_PORTS_OUT_I_CRYPTO_ENGINE_H

#include <stdint.h>
#include <stddef.h>

class ICryptoEngine {
public:
    virtual ~ICryptoEngine() = default;

    virtual bool init() = 0;

    virtual bool generateChallenge(uint8_t* challenge, size_t len) = 0;

    virtual bool sha256(const uint8_t* data, size_t len, uint8_t* hash) = 0;

    virtual bool verifyECDSA(const uint8_t* publicKey,
                             const uint8_t* hash,
                             const uint8_t* signature,
                             size_t sigLen) = 0;

    virtual bool parseCOSEPublicKey(const uint8_t* coseKey, size_t coseLen,
                                    uint8_t* publicKey) = 0;

    virtual bool getRandomBytes(uint8_t* buffer, size_t len) = 0;

    virtual bool parseDERSignature(const uint8_t* der, size_t derLen,
                                   uint8_t* r, uint8_t* s) = 0;
};

#endif // FIDOLOCK_PORTS_OUT_I_CRYPTO_ENGINE_H
