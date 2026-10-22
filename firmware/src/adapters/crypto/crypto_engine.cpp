/**
 * FidoLock - Crypto Engine Module
 *
 * Cryptographic functions for signature verification and challenge generation.
 * Uses mbedTLS library included in ESP-IDF.
 */

#include "crypto_engine.h"
#include "../../utils/crypto_utils.h"
#include "../../utils/log.h"

#include "mbedtls/sha256.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"

static const char* TAG = "Crypto";

CryptoEngine::CryptoEngine(IBtRadio& btRadio, ISystemControl& system)
    : _btRadio(btRadio), _system(system) {
    mbedtls_ctr_drbg_init(&_drbg);
    mbedtls_entropy_init(&_entropy);
}

CryptoEngine::~CryptoEngine() {
    mbedtls_ctr_drbg_free(&_drbg);
    mbedtls_entropy_free(&_entropy);
}

bool CryptoEngine::init() {
    // The ESP32 hardware RNG is true-random only while a noise source feeds it:
    // the RF subsystem's high-speed ADC (Wi-Fi/BT on) or the SAR ADC
    // (bootloader_random_enable()); otherwise it is pseudo-random (ESP32 TRM
    // v5.8 §18). Keep the Bluetooth controller on while the CTR-DRBG seed is
    // drawn, then turn the radio back off before any authentication traffic
    // starts. If the radio is already up (e.g. a future wireless admin
    // channel), leave its state untouched. Residual risks of this window and
    // the planned SAR-ADC source: docs/security-model.md §7.
    const bool rfWasOff = !_btRadio.isStarted();
    if (rfWasOff && !_btRadio.start()) {
        LOG_W(TAG, "BT enable failed; seeding DRBG from pseudo-random RNG output");
    }
    if (_btRadio.isStarted()) {
        _system.delayMs(RNG_RF_SETTLE_MS);  // let RF noise feed the RNG before drawing the seed
    }

    // Seed a CTR-DRBG from the platform entropy pool (hardware RNG on ESP32 via
    // ESP-IDF's entropy_hardware_poll; OS entropy on native test builds).
    // CTR_DRBG is an approved NIST SP 800-90A algorithm; this mbedTLS build is
    // not a CMVP-validated module (see docs/security-model.md §7).
    static const char pers[] = "fidolock-challenge";
    int ret = mbedtls_ctr_drbg_seed(&_drbg, mbedtls_entropy_func, &_entropy,
                                    (const unsigned char*)pers, sizeof(pers) - 1);

    if (rfWasOff && _btRadio.isStarted()) {
        _btRadio.stop();
    }

    if (ret != 0) {
        LOG_E(TAG, "CTR-DRBG seed failed: %d", ret);
        return false;
    }

    LOG_I(TAG, "Engine initialized");
    _initialized = true;
    return true;
}

bool CryptoEngine::generateChallenge(uint8_t* challenge, size_t len) {
    if (!_initialized) {
        LOG_E(TAG, "Not initialized");
        return false;
    }

    int ret = mbedtls_ctr_drbg_random(&_drbg, challenge, len);
    if (ret != 0) {
        LOG_E(TAG, "DRBG random failed: %d", ret);
        return false;
    }

    #if DEBUG_CRYPTO
    {
        char buf[len * 2 + 1];
        for (size_t i = 0; i < len; i++) snprintf(&buf[i*2], 3, "%02X", challenge[i]);
        LOG_D(TAG, "Generated challenge: %s", buf);
    }
    #endif

    return true;
}

bool CryptoEngine::sha256(const uint8_t* data, size_t len, uint8_t* hash) {
    if (!_initialized) {
        return false;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);

    // Return values are ignored: mbedTLS 2.28 (device) declares these as
    // deprecated void wrappers, 3.x (host tests) returns int.
    // 0 = SHA-256 (not SHA-224)
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    #if DEBUG_CRYPTO
    {
        char buf[65];
        for (int i = 0; i < 32; i++) snprintf(&buf[i*2], 3, "%02X", hash[i]);
        LOG_D(TAG, "SHA256: %s", buf);
    }
    #endif

    return true;
}

bool CryptoEngine::verifyECDSA(const uint8_t* publicKey,
                                const uint8_t* hash,
                                const uint8_t* signature,
                                size_t sigLen) {
    if (!_initialized) {
        return false;
    }

    // Parse DER-encoded signature to get r and s values
    uint8_t r[32], s[32];
    if (!parseDERSignature(signature, sigLen, r, s)) {
        LOG_E(TAG, "Failed to parse DER signature");
        return false;
    }

    // Initialize mbedTLS structures. The group and point are kept standalone
    // rather than inside an mbedtls_ecdsa_context, whose grp/Q fields are
    // private in mbedTLS 3.x (ESP-IDF 5).
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);

    mbedtls_mpi r_mpi, s_mpi;
    mbedtls_mpi_init(&r_mpi);
    mbedtls_mpi_init(&s_mpi);

    int ret;
    bool result = false;

    // Setup the P-256 curve
    ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        LOG_E(TAG, "Failed to load P-256 curve: %d", ret);
        goto cleanup;
    }

    // Load public key (uncompressed format: 0x04 || x || y)
    if (publicKey[0] != 0x04) {
        LOG_E(TAG, "Invalid public key format (expected 0x04)");
        goto cleanup;
    }

    ret = mbedtls_ecp_point_read_binary(&grp, &Q, publicKey, 65);
    if (ret != 0) {
        LOG_E(TAG, "Failed to load public key: %d", ret);
        goto cleanup;
    }

    // Validate the point is on the curve
    ret = mbedtls_ecp_check_pubkey(&grp, &Q);
    if (ret != 0) {
        LOG_E(TAG, "Public key validation failed: %d", ret);
        goto cleanup;
    }

    // Load r and s values
    ret = mbedtls_mpi_read_binary(&r_mpi, r, 32);
    if (ret != 0) {
        LOG_E(TAG, "Failed to load r: %d", ret);
        goto cleanup;
    }

    ret = mbedtls_mpi_read_binary(&s_mpi, s, 32);
    if (ret != 0) {
        LOG_E(TAG, "Failed to load s: %d", ret);
        goto cleanup;
    }

    // Verify signature
    ret = mbedtls_ecdsa_verify(&grp, hash, 32, &Q, &r_mpi, &s_mpi);
    if (ret == 0) {
        LOG_I(TAG, "Signature verification PASSED");
        result = true;
    } else {
        LOG_E(TAG, "Signature verification FAILED: %d", ret);
        result = false;
    }

cleanup:
    mbedtls_mpi_free(&r_mpi);
    mbedtls_mpi_free(&s_mpi);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);

    return result;
}

bool CryptoEngine::parseDERSignature(const uint8_t* der, size_t derLen,
                                      uint8_t* r, uint8_t* s) {
    // Delegate to CryptoUtils for DER signature parsing
    return CryptoUtils::parseDERSignature(der, derLen, r, s);
}

bool CryptoEngine::parseCOSEPublicKey(const uint8_t* coseKey, size_t coseLen,
                                       uint8_t* publicKey) {
    // Delegate to CryptoUtils for COSE public key parsing
    return CryptoUtils::parseCOSEPublicKey(coseKey, coseLen, publicKey);
}

bool CryptoEngine::getRandomBytes(uint8_t* buffer, size_t len) {
    return generateChallenge(buffer, len);
}
