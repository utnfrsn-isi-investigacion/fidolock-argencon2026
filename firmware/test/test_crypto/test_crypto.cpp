/**
 * FidoLock - Crypto Utilities Unit Tests
 *
 * Tests PURE functions from CryptoUtils - no mocks needed.
 * These test the real DER parsing, COSE parsing, and UID hashing logic.
 *
 * No driven ports are involved — all functions are stateless.
 * ADR 008: pure functions in utils/ are not adapters; they can be
 * included directly without going through a port.
 */

#include <gtest/gtest.h>
#include <string.h>
#include <stdint.h>

// Real crypto utilities under test
#include "../../src/utils/crypto_utils.h"

// ============================================================
// Test Vectors
// ============================================================

// Valid DER-encoded ECDSA signature (standard 32-byte r and s)
static const uint8_t TEST_DER_SIGNATURE[] = {
    0x30, 0x44,  // SEQUENCE, 68 bytes
    0x02, 0x20,  // INTEGER, 32 bytes (r)
    0x7c, 0x76, 0xb5, 0x45, 0x7b, 0xb6, 0xd5, 0x72,
    0xf2, 0x4c, 0xdd, 0x36, 0x4d, 0x5c, 0xfe, 0x55,
    0xd3, 0x6f, 0x89, 0x9e, 0xf4, 0x9a, 0xd6, 0x0b,
    0xfa, 0x4f, 0x89, 0x3c, 0xd8, 0x7b, 0x93, 0x04,
    0x02, 0x20,  // INTEGER, 32 bytes (s)
    0x60, 0x82, 0x3b, 0xbd, 0xf8, 0xeb, 0x67, 0xdb,
    0xe8, 0x37, 0x10, 0x6c, 0x43, 0x2d, 0xc2, 0xad,
    0x6d, 0x6c, 0xe6, 0x3c, 0xd0, 0x15, 0x61, 0x89,
    0xc9, 0xb5, 0x6e, 0xfa, 0x3c, 0xfa, 0xc4, 0x4d
};

static const uint8_t EXPECTED_R[32] = {
    0x7c, 0x76, 0xb5, 0x45, 0x7b, 0xb6, 0xd5, 0x72,
    0xf2, 0x4c, 0xdd, 0x36, 0x4d, 0x5c, 0xfe, 0x55,
    0xd3, 0x6f, 0x89, 0x9e, 0xf4, 0x9a, 0xd6, 0x0b,
    0xfa, 0x4f, 0x89, 0x3c, 0xd8, 0x7b, 0x93, 0x04
};

static const uint8_t EXPECTED_S[32] = {
    0x60, 0x82, 0x3b, 0xbd, 0xf8, 0xeb, 0x67, 0xdb,
    0xe8, 0x37, 0x10, 0x6c, 0x43, 0x2d, 0xc2, 0xad,
    0x6d, 0x6c, 0xe6, 0x3c, 0xd0, 0x15, 0x61, 0x89,
    0xc9, 0xb5, 0x6e, 0xfa, 0x3c, 0xfa, 0xc4, 0x4d
};

// DER signature with leading zero (high bit set in r)
static const uint8_t TEST_DER_WITH_LEADING_ZERO[] = {
    0x30, 0x45,  // SEQUENCE, 69 bytes
    0x02, 0x21,  // INTEGER, 33 bytes (r with leading 00)
    0x00,        // Leading zero padding
    0x8c, 0x76, 0xb5, 0x45, 0x7b, 0xb6, 0xd5, 0x72,
    0xf2, 0x4c, 0xdd, 0x36, 0x4d, 0x5c, 0xfe, 0x55,
    0xd3, 0x6f, 0x89, 0x9e, 0xf4, 0x9a, 0xd6, 0x0b,
    0xfa, 0x4f, 0x89, 0x3c, 0xd8, 0x7b, 0x93, 0x04,
    0x02, 0x20,  // INTEGER, 32 bytes (s)
    0x60, 0x82, 0x3b, 0xbd, 0xf8, 0xeb, 0x67, 0xdb,
    0xe8, 0x37, 0x10, 0x6c, 0x43, 0x2d, 0xc2, 0xad,
    0x6d, 0x6c, 0xe6, 0x3c, 0xd0, 0x15, 0x61, 0x89,
    0xc9, 0xb5, 0x6e, 0xfa, 0x3c, 0xfa, 0xc4, 0x4d
};

// ============================================================
// DER Signature Parsing Tests
// ============================================================

TEST(DERParsing, ValidSignatureParsed) {
    uint8_t r[32], s[32];

    bool result = CryptoUtils::parseDERSignature(
        TEST_DER_SIGNATURE, sizeof(TEST_DER_SIGNATURE), r, s);

    EXPECT_TRUE(result);
    EXPECT_EQ(0, memcmp(EXPECTED_R, r, 32));
    EXPECT_EQ(0, memcmp(EXPECTED_S, s, 32));
}

TEST(DERParsing, LeadingZeroStripped) {
    uint8_t r[32], s[32];

    bool result = CryptoUtils::parseDERSignature(
        TEST_DER_WITH_LEADING_ZERO, sizeof(TEST_DER_WITH_LEADING_ZERO), r, s);

    EXPECT_TRUE(result);
    // r should have high bit set (0x8c) after stripping leading zero
    EXPECT_EQ(0x8c, r[0]);
    EXPECT_EQ(0, memcmp(EXPECTED_S, s, 32));
}

TEST(DERParsing, InvalidHeaderRejected) {
    uint8_t r[32], s[32];
    // 0x31 is SET, not SEQUENCE
    uint8_t invalid[] = {0x31, 0x44, 0x02, 0x20, 0x00, 0x00, 0x00, 0x00};

    bool result = CryptoUtils::parseDERSignature(invalid, sizeof(invalid), r, s);

    EXPECT_FALSE(result);
}

TEST(DERParsing, TooShortRejected) {
    uint8_t r[32], s[32];
    uint8_t tooShort[] = {0x30, 0x04, 0x02, 0x00};

    bool result = CryptoUtils::parseDERSignature(tooShort, sizeof(tooShort), r, s);

    EXPECT_FALSE(result);
}

TEST(DERParsing, WrongIntegerTagRejected) {
    uint8_t r[32], s[32];
    // Replace 0x02 (INTEGER) with 0x03
    uint8_t invalid[] = {0x30, 0x44, 0x03, 0x20, 0x00};

    bool result = CryptoUtils::parseDERSignature(invalid, sizeof(invalid), r, s);

    EXPECT_FALSE(result);
}

// Bounds hardening: a declared r length that runs past the buffer must be
// rejected without reading out of bounds (the buffer claims r is 0x40 bytes
// but only a handful follow).
TEST(DERParsing, DeclaredRLengthOverrunsBufferRejected) {
    uint8_t r[32], s[32];
    uint8_t malformed[] = {0x30, 0x44, 0x02, 0x40, 0x01, 0x02, 0x03, 0x04};

    bool result = CryptoUtils::parseDERSignature(malformed, sizeof(malformed), r, s);

    EXPECT_FALSE(result);
}

// Bounds hardening: truncated right after a valid r — no second INTEGER (s)
// present. Must be rejected rather than reading past the end.
TEST(DERParsing, TruncatedAfterRRejected) {
    uint8_t r[32], s[32];
    uint8_t malformed[] = {
        0x30, 0x24, 0x02, 0x20,  // SEQUENCE, INTEGER r (32 bytes)
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
        // no INTEGER s follows
    };

    bool result = CryptoUtils::parseDERSignature(malformed, sizeof(malformed), r, s);

    EXPECT_FALSE(result);
}

// Bounds hardening: a declared s length that overruns the buffer is rejected.
TEST(DERParsing, DeclaredSLengthOverrunsBufferRejected) {
    uint8_t r[32], s[32];
    uint8_t malformed[] = {
        0x30, 0x28, 0x02, 0x20,  // SEQUENCE, INTEGER r (32 bytes)
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
        0x02, 0x20, 0xAA, 0xBB   // INTEGER s claims 32 bytes, only 2 follow
    };

    bool result = CryptoUtils::parseDERSignature(malformed, sizeof(malformed), r, s);

    EXPECT_FALSE(result);
}
// ============================================================
// COSE Public Key Parsing Tests
// ============================================================

// Minimal valid COSE key (EC2, P-256)
// Map with 5 items: kty=2, alg=-7, crv=1, x=32bytes, y=32bytes
static const uint8_t TEST_COSE_KEY[] = {
    0xA5,        // Map of 5 items
    0x01, 0x02,  // kty: 2 (EC2)
    0x03, 0x26,  // alg: -7 (ES256)
    0x20, 0x01,  // crv: 1 (P-256)
    0x21, 0x58, 0x20,  // x: byte string, 32 bytes
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
    0x22, 0x58, 0x20,  // y: byte string, 32 bytes
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40
};

TEST(COSEParsing, ValidKeyParsed) {
    uint8_t pubKey[65];

    bool result = CryptoUtils::parseCOSEPublicKey(
        TEST_COSE_KEY, sizeof(TEST_COSE_KEY), pubKey);

    EXPECT_TRUE(result);
    EXPECT_EQ(0x04, pubKey[0]);   // Uncompressed point marker
    EXPECT_EQ(0x01, pubKey[1]);   // First byte of x
    EXPECT_EQ(0x21, pubKey[33]);  // First byte of y
}

TEST(COSEParsing, TooShortRejected) {
    uint8_t pubKey[65];
    uint8_t tooShort[50] = {0xA5, 0x01, 0x02};

    bool result = CryptoUtils::parseCOSEPublicKey(tooShort, sizeof(tooShort), pubKey);

    EXPECT_FALSE(result);
}

TEST(COSEParsing, NotAMapRejected) {
    uint8_t pubKey[65];
    uint8_t notAMap[70];
    memset(notAMap, 0, sizeof(notAMap));
    notAMap[0] = 0x80;  // Array instead of map

    bool result = CryptoUtils::parseCOSEPublicKey(notAMap, sizeof(notAMap), pubKey);

    EXPECT_FALSE(result);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
