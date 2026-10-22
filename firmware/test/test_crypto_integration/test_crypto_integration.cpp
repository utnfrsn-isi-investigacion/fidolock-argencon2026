/**
 * FidoLock - Crypto Engine Integration Tests (real mbedTLS)
 *
 * Unlike test_crypto/ (which exercises the pure CryptoUtils byte-parsers with
 * no crypto backend), this suite drives the REAL CryptoEngine adapter against
 * a real mbedTLS instance — the same primitives that run on the ESP32. It is
 * the machine-checkable evidence behind security claims in the paper:
 *
 *   - Forged ECDSA signature  -> verifyECDSA() returns false
 *   - rpId hash integrity      -> sha256() is the real SHA-256
 *   - Challenge uniqueness     -> generateChallenge() uses CTR-DRBG
 *
 * Test vectors are produced in-process with mbedTLS (genkey + sign), so the
 * {public key, digest, signature} triple is always internally consistent and
 * nothing is hand-transcribed.
 *
 * NOTE ON PORTABILITY: the host links mbedTLS 3.6 (Homebrew mbedtls@3, or
 * built from source in CI), while the device runs ESP-IDF v4.4.7's mbedTLS
 * 2.28.7. CryptoEngine compiles against both, but the helpers below use the
 * 3.x API, so the 2.28 build is checked only by compiling the firmware. This
 * validates FidoLock's *usage* of the ECDSA/SHA-256 API; the cryptographic
 * correctness of the primitives themselves is the responsibility of the
 * vetted mbedTLS build that ships on-target.
 *
 * The three mandated categories (project guidelines) are covered:
 *   1. Valid input        -> accepted
 *   2. Adversarial input  -> clean rejection, no crash
 *   3. Boundary condition  -> malformed key / zero-length signature
 */

#include <gtest/gtest.h>
#include <string.h>
#include <stdint.h>

#include "../../src/adapters/crypto/crypto_engine.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

// Pre-computed SHA-256("fidolock.local") — the rpId hash the firmware compares
// authData[0..31] against (CTAP2 spec §6.1 step 19). Authoritative vector,
// identical to the one asserted in test_ctap2/.
// Regenerate with: printf '%s' 'fidolock.local' | openssl dgst -sha256 -hex
static const uint8_t EXPECTED_RP_ID_HASH[32] = {
    0xb1, 0x01, 0xb1, 0x20, 0x76, 0x19, 0x9d, 0x81,
    0x4c, 0x84, 0xba, 0x33, 0x2e, 0xdc, 0x92, 0x8e,
    0xea, 0xb6, 0xe4, 0x02, 0x6f, 0x81, 0xc0, 0xe8,
    0x53, 0x13, 0xac, 0xad, 0x3d, 0x88, 0xb5, 0x91
};

// SHA-256("") — empty-input known-answer vector.
static const uint8_t SHA256_EMPTY[32] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
    0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};

namespace {

// Call-recording fake for the IBtRadio port: lets the RF-window tests below
// assert that CryptoEngine::init() seeds the DRBG inside a Bluetooth window.
class FakeBtRadio : public IBtRadio {
public:
    bool on         = false;
    int  startCalls = 0;
    int  stopCalls  = 0;

    bool isStarted() override { return on; }
    bool start() override     { ++startCalls; on = true;  return true; }
    bool stop() override      { ++stopCalls;  on = false; return true; }
};

// Time does not need to pass in native tests.
class NoopSystemControl : public ISystemControl {
public:
    void delayMs(uint32_t) override {}
    void restart() override {}
};

// Shared port fakes for the tests that exercise crypto behaviour and do not
// care about the RF window; the RF-window tests construct their own.
FakeBtRadio       g_btRadio;
NoopSystemControl g_system;

// A real P-256 key, a 32-byte digest, and a valid DER signature over it.
struct SignedAssertion {
    uint8_t publicKey[65];                // 0x04 || X || Y (uncompressed)
    uint8_t digest[32];                   // value passed to verifyECDSA()
    uint8_t sig[MBEDTLS_ECDSA_MAX_LEN];   // DER-encoded signature
    size_t  sigLen = 0;
};

// Generate a fresh, self-consistent signing vector via mbedTLS.
SignedAssertion makeSignedAssertion() {
    SignedAssertion a{};

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_ecdsa_context key;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_ecdsa_init(&key);

    const char* pers = "fidolock-crypto-itest";
    EXPECT_EQ(0, mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                       (const unsigned char*)pers, strlen(pers)));
    EXPECT_EQ(0, mbedtls_ecdsa_genkey(&key, MBEDTLS_ECP_DP_SECP256R1,
                                      mbedtls_ctr_drbg_random, &drbg));

    size_t olen = 0;
    EXPECT_EQ(0, mbedtls_ecp_point_write_binary(
        &key.MBEDTLS_PRIVATE(grp), &key.MBEDTLS_PRIVATE(Q),
        MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, a.publicKey, sizeof(a.publicKey)));
    EXPECT_EQ((size_t)65, olen);

    // Arbitrary but fixed 32-byte digest (stands in for SHA-256(authData||cdh)).
    for (int i = 0; i < 32; i++) {
        a.digest[i] = (uint8_t)(i * 7 + 1);
    }

    EXPECT_EQ(0, mbedtls_ecdsa_write_signature(
        &key, MBEDTLS_MD_SHA256, a.digest, sizeof(a.digest),
        a.sig, sizeof(a.sig), &a.sigLen, mbedtls_ctr_drbg_random, &drbg));

    mbedtls_ecdsa_free(&key);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return a;
}

} // namespace

// ============================================================
// verifyECDSA — Category 1: valid input is accepted
// ============================================================

TEST(CryptoEngineVerifyECDSA, ValidSignatureAccepted) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());
    SignedAssertion a = makeSignedAssertion();

    EXPECT_TRUE(engine.verifyECDSA(a.publicKey, a.digest, a.sig, a.sigLen));
}

// ============================================================
// verifyECDSA — Category 2: adversarial input is cleanly rejected
// ============================================================

// Forged-signature invariant: substitute a random 64-byte blob.
TEST(CryptoEngineVerifyECDSA, RandomSignatureRejected) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());
    SignedAssertion a = makeSignedAssertion();

    uint8_t forged[64];
    for (int i = 0; i < 64; i++) {
        forged[i] = (uint8_t)(i * 3 + 5);
    }

    EXPECT_FALSE(engine.verifyECDSA(a.publicKey, a.digest, forged, sizeof(forged)));
}

TEST(CryptoEngineVerifyECDSA, TamperedSignatureByteRejected) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());
    SignedAssertion a = makeSignedAssertion();

    // Flip the last byte of the DER signature (low byte of s).
    a.sig[a.sigLen - 1] ^= 0x01;

    EXPECT_FALSE(engine.verifyECDSA(a.publicKey, a.digest, a.sig, a.sigLen));
}

TEST(CryptoEngineVerifyECDSA, SignatureOverDifferentDigestRejected) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());
    SignedAssertion a = makeSignedAssertion();

    // Same signature, one-bit change in the message digest.
    a.digest[0] ^= 0x01;

    EXPECT_FALSE(engine.verifyECDSA(a.publicKey, a.digest, a.sig, a.sigLen));
}

TEST(CryptoEngineVerifyECDSA, WrongPublicKeyRejected) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());
    SignedAssertion signer = makeSignedAssertion();
    SignedAssertion other  = makeSignedAssertion();

    // Valid signature verified against an unrelated (valid) public key.
    EXPECT_FALSE(engine.verifyECDSA(other.publicKey, signer.digest,
                                    signer.sig, signer.sigLen));
}

// ============================================================
// verifyECDSA — Category 3: boundary conditions
// ============================================================

TEST(CryptoEngineVerifyECDSA, MalformedPublicKeyPrefixRejected) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());
    SignedAssertion a = makeSignedAssertion();

    // Uncompressed-point marker must be 0x04; anything else is malformed.
    a.publicKey[0] = 0x05;

    EXPECT_FALSE(engine.verifyECDSA(a.publicKey, a.digest, a.sig, a.sigLen));
}

TEST(CryptoEngineVerifyECDSA, ZeroLengthSignatureRejected) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());
    SignedAssertion a = makeSignedAssertion();

    uint8_t dummy = 0;
    EXPECT_FALSE(engine.verifyECDSA(a.publicKey, a.digest, &dummy, 0));
}

// ============================================================
// sha256 — real hash underpinning the rpId-hash invariant
// ============================================================

TEST(CryptoEngineSHA256, RpIdHashKnownAnswer) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());

    uint8_t hash[32];
    ASSERT_TRUE(engine.sha256((const uint8_t*)"fidolock.local", 14, hash));
    EXPECT_EQ(0, memcmp(hash, EXPECTED_RP_ID_HASH, 32));
}

TEST(CryptoEngineSHA256, AttackerRpIdProducesDifferentHash) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());

    // The orchestrator rejects an assertion whose authData rpId hash does not
    // equal SHA-256("fidolock.local"); this proves a spoofed rpId genuinely
    // hashes to a different value (no collision with the expected vector).
    uint8_t hash[32];
    ASSERT_TRUE(engine.sha256((const uint8_t*)"attacker.local", 14, hash));
    EXPECT_NE(0, memcmp(hash, EXPECTED_RP_ID_HASH, 32));
}

TEST(CryptoEngineSHA256, EmptyInputKnownAnswer) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());

    uint8_t hash[32];
    ASSERT_TRUE(engine.sha256((const uint8_t*)"", 0, hash));
    EXPECT_EQ(0, memcmp(hash, SHA256_EMPTY, 32));
}

// ============================================================
// generateChallenge — CTR-DRBG invariants
// ============================================================

// Category 1: valid input produces 32 bytes of output.
TEST(CryptoEngineChallenge, GeneratesCorrectLength) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());

    uint8_t challenge[32] = {0};
    EXPECT_TRUE(engine.generateChallenge(challenge, sizeof(challenge)));
}

// Category 2: uninitialized engine returns false cleanly.
TEST(CryptoEngineChallenge, NotInitializedReturnsError) {
    CryptoEngine engine(g_btRadio, g_system);  // init() deliberately not called
    uint8_t challenge[32];
    EXPECT_FALSE(engine.generateChallenge(challenge, sizeof(challenge)));
}

// Category 3a: output is statistically non-trivial (not all-zero).
TEST(CryptoEngineChallenge, OutputIsNonZero) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());

    uint8_t challenge[32] = {0};
    ASSERT_TRUE(engine.generateChallenge(challenge, sizeof(challenge)));

    bool anyNonZero = false;
    for (size_t i = 0; i < sizeof(challenge); i++) {
        if (challenge[i] != 0) { anyNonZero = true; break; }
    }
    EXPECT_TRUE(anyNonZero);
}

// Category 3b: consecutive calls produce distinct output (DRBG advances state).
TEST(CryptoEngineChallenge, ConsecutiveChallengesDiffer) {
    CryptoEngine engine(g_btRadio, g_system);
    ASSERT_TRUE(engine.init());

    uint8_t c1[32], c2[32];
    ASSERT_TRUE(engine.generateChallenge(c1, sizeof(c1)));
    ASSERT_TRUE(engine.generateChallenge(c2, sizeof(c2)));

    EXPECT_NE(0, memcmp(c1, c2, sizeof(c1)));
}

// ============================================================
// init() — RF-window seeding invariants
// ============================================================

// The DRBG seed must be drawn inside a Bluetooth RF window, so the RF
// subsystem's high-speed ADC feeds the hardware RNG (ESP32 TRM v5.8 §18),
// and the radio must be off again when init() returns.
TEST(CryptoEngineRfWindow, SeedingOpensAndClosesRfWindow) {
    FakeBtRadio radio;
    CryptoEngine engine(radio, g_system);
    ASSERT_TRUE(engine.init());

    EXPECT_EQ(1, radio.startCalls);
    EXPECT_EQ(1, radio.stopCalls);
    EXPECT_FALSE(radio.on);  // radio off before authentication begins
}

// If the radio is already up (e.g. a future wireless admin channel),
// init() must not start or stop it.
TEST(CryptoEngineRfWindow, AlreadyActiveRadioLeftUntouched) {
    FakeBtRadio radio;
    radio.on = true;

    CryptoEngine engine(radio, g_system);
    ASSERT_TRUE(engine.init());

    EXPECT_EQ(0, radio.startCalls);
    EXPECT_EQ(0, radio.stopCalls);
    EXPECT_TRUE(radio.on);  // still running for whoever owns it
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
