/**
 * FidoLock - Crypto Engine Module
 *
 * Cryptographic functions for signature verification.
 * Implements ICryptoEngine interface.
 */

#ifndef FIDOLOCK_CRYPTO_ENGINE_H
#define FIDOLOCK_CRYPTO_ENGINE_H

#include "../../config.h"
#include "../../ports/out/i_crypto_engine.h"
#include "../../ports/out/i_bt_radio.h"
#include "../../ports/out/i_system_control.h"

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

class CryptoEngine : public ICryptoEngine {
public:
    /**
     * @param btRadio  RF window for DRBG seeding (see init()).
     * @param system   Blocking delay for the RF settle time.
     */
    CryptoEngine(IBtRadio& btRadio, ISystemControl& system);
    ~CryptoEngine() override;

    // mbedTLS contexts are not trivially relocatable; prevent copy and move.
    CryptoEngine(const CryptoEngine&) = delete;
    CryptoEngine& operator=(const CryptoEngine&) = delete;
    CryptoEngine(CryptoEngine&&) = delete;
    CryptoEngine& operator=(CryptoEngine&&) = delete;

    bool init() override;
    bool generateChallenge(uint8_t* challenge, size_t len) override;
    bool sha256(const uint8_t* data, size_t len, uint8_t* hash) override;
    bool verifyECDSA(const uint8_t* publicKey,
                     const uint8_t* hash,
                     const uint8_t* signature,
                     size_t sigLen) override;
    bool parseCOSEPublicKey(const uint8_t* coseKey, size_t coseLen,
                            uint8_t* publicKey) override;
    bool getRandomBytes(uint8_t* buffer, size_t len) override;
    bool parseDERSignature(const uint8_t* der, size_t derLen,
                           uint8_t* r, uint8_t* s) override;

private:
    IBtRadio&        _btRadio;
    ISystemControl&  _system;
    bool _initialized = false;
    mbedtls_ctr_drbg_context _drbg;
    mbedtls_entropy_context  _entropy;
};

#endif // FIDOLOCK_CRYPTO_ENGINE_H
