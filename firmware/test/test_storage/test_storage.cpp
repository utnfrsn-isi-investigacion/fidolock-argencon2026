/**
 * FidoLock - FNV-1a Hash Utility Unit Tests
 *
 * Tests the pure fnv1a_hash() function from utils/credential_hash.h
 * (ADR 004). No LittleFS, no adapters, no Arduino runtime required.
 *
 * CredentialStore's LittleFS operations are integration-tested on device.
 */

#include <gtest/gtest.h>
#include <string.h>
#include <stdint.h>

#include "../../src/utils/credential_hash.h"

// ============================================================
// fnv1a_hash — FNV-1a hash (ADR 004)
// ============================================================

TEST(FNV1aHashTest, HashIsDeterministic) {
    uint8_t credId[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    uint32_t h1 = fnv1a_hash(credId, sizeof(credId));
    uint32_t h2 = fnv1a_hash(credId, sizeof(credId));

    EXPECT_EQ(h1, h2);
}

TEST(FNV1aHashTest, DifferentInputsProduceDifferentHashes) {
    uint8_t credId1[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t credId2[] = {0x01, 0x02, 0x03, 0x05};  // last byte differs

    uint32_t h1 = fnv1a_hash(credId1, sizeof(credId1));
    uint32_t h2 = fnv1a_hash(credId2, sizeof(credId2));

    EXPECT_NE(h1, h2);
}

TEST(FNV1aHashTest, DifferentLengthsSamePrefix) {
    uint8_t credId1[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t credId2[] = {0x01, 0x02, 0x03, 0x04, 0x05};

    uint32_t h1 = fnv1a_hash(credId1, 4);
    uint32_t h2 = fnv1a_hash(credId2, 5);

    EXPECT_NE(h1, h2);
}

TEST(FNV1aHashTest, HashNotEqualToFNVOffsetBasis) {
    // Any non-empty input should not hash to the FNV-1a 32-bit offset basis
    // (2166136261), which is the hash of an empty string.
    uint8_t credId[] = {0x00};

    uint32_t h = fnv1a_hash(credId, 1);

    EXPECT_NE(2166136261u, h);
}

TEST(FNV1aHashTest, SingleByteDifferencesDistinguishable) {
    uint8_t credId1[] = {0x00};
    uint8_t credId2[] = {0x01};

    uint32_t h1 = fnv1a_hash(credId1, 1);
    uint32_t h2 = fnv1a_hash(credId2, 1);

    EXPECT_NE(h1, h2);
}

TEST(FNV1aHashTest, FullSizeCredentialIdHashesDiffer) {
    uint8_t credId1[64];
    uint8_t credId2[64];
    memset(credId1, 0xAA, 64);
    memset(credId2, 0xBB, 64);

    uint32_t h1 = fnv1a_hash(credId1, 64);
    uint32_t h2 = fnv1a_hash(credId2, 64);

    EXPECT_NE(h1, h2);
}

TEST(FNV1aHashTest, KnownFNV1aVector) {
    // FNV-1a 32-bit of {0x61} ('a') = 0xe40c292c
    // Source: http://www.isthe.com/chongo/tech/comp/fnv/
    uint8_t input[] = {0x61};
    uint32_t expected = 0xe40c292c;

    uint32_t h = fnv1a_hash(input, 1);

    EXPECT_EQ(expected, h);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
