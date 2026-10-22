/**
 * FidoLock - CTAP2 Handler Unit Tests
 *
 * Tests the REAL CTAP2Handler through its driving port (ICTAP2Handler).
 * NFC hardware is replaced by MockNFCReader (gmock adapter for INFCReader).
 *
 * Composition root: each TEST_F SetUp() wires CTAP2Handler ← MockNFCReader.
 *
 * Rule 5 (ADR 008): no local re-implementations of production CBOR logic.
 *
 * Security-invariant note: the four end-to-end accept/deny
 * invariants — forged signature, replayed counter, wrong rpId hash, expired
 * challenge — are regression-tested where the decision logic actually lives
 * after the ADR 008 refactor: test_orchestrator/ (real FidoLockOrchestrator) and
 * test_crypto_integration/ (real mbedTLS verifyECDSA / SHA-256). The
 * rpId/timeout/userPresent cases below are lightweight authData-field checks,
 * not the end-to-end verification path.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string.h>
#include <stdint.h>
#include <vector>

#include "../../src/config.h"
#include "../../src/utils/time_utils.h"
#include "../test_mocks/mock_interfaces.h"

// Real application module under test
#include "../../src/application/ctap2/ctap2_handler.h"

using ::testing::_;
using ::testing::Return;
using ::testing::SetArrayArgument;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::WithArgs;
using ::testing::SetArgPointee;

// ============================================================
// Test fixture: real CTAP2Handler wired to MockNFCReader
// Composition root per ADR 008, Rule 4.
// ============================================================

class CTAP2HandlerTest : public ::testing::Test {
protected:
    MockNFCReader mockNfc;
    CTAP2Handler handler;

    // Helper: make MockNFCReader return a failed SELECT APDU (applet not found)
    void makeSelectFail() {
        EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _))
            .WillRepeatedly(Return(false));
    }

    // Helper: build a minimal 37-byte authData block.
    // Layout: rpIdHash[32] | flags[1] | counter[4] (big-endian)
public:
    static void buildAuthData(uint8_t* authData, const uint8_t* rpIdHash,
                               uint8_t flags, uint32_t counter) {
        memcpy(authData, rpIdHash, 32);
        authData[32] = flags;
        authData[33] = (counter >> 24) & 0xFF;
        authData[34] = (counter >> 16) & 0xFF;
        authData[35] = (counter >>  8) & 0xFF;
        authData[36] =  counter        & 0xFF;
    }
};

// ============================================================
// CTAP2Handler behaviour when NFC communication fails
// ============================================================

TEST_F(CTAP2HandlerTest, GetInfoReturnsErrorWhenSelectFails) {
    // sendAPDU for SELECT fails → getInfo must not succeed
    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _)).WillOnce(Return(false));

    uint8_t info[256];
    size_t infoLen = sizeof(info);
    int ret = handler.getInfo(&mockNfc, info, &infoLen);

    EXPECT_NE(CTAP2_OK, ret);
}

TEST_F(CTAP2HandlerTest, GetAssertionReturnsErrorWhenSelectFails) {
    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _)).WillOnce(Return(false));

    uint8_t hash[32] = {};
    CTAP2Response resp{};

    int ret = handler.getAssertion(&mockNfc, "fidolock.local",
                                   hash, &resp);

    EXPECT_NE(CTAP2_OK, ret);
}

// The request is the two-entry map {1: rpId, 2: clientDataHash}, with no
// allowList, so the authenticator picks its own discoverable credential.
TEST_F(CTAP2HandlerTest, GetAssertionEncodesRpIdAndHashOnly) {
    std::vector<uint8_t> captured;
    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _))
        .WillOnce(Invoke([&](uint8_t*, size_t, uint8_t* resp, size_t* len) {
            resp[0] = 0x90; resp[1] = 0x00; *len = 2;   // SELECT → 0x9000
            return true;
        }))
        .WillOnce(Invoke([&](uint8_t* apdu, size_t apduLen, uint8_t*, size_t*) {
            captured.assign(apdu, apdu + apduLen);
            return false;
        }));

    uint8_t hash[32];
    memset(hash, 0x5C, sizeof(hash));
    CTAP2Response resp{};

    handler.getAssertion(&mockNfc, "fidolock.local", hash, &resp);

    // map(2) | 01 text(14) "fidolock.local" | 02 bytes(32) <hash>
    std::vector<uint8_t> cbor = {0xA2, 0x01, 0x6E};
    const char* rp = "fidolock.local";
    cbor.insert(cbor.end(), rp, rp + 14);
    cbor.push_back(0x02); cbor.push_back(0x58); cbor.push_back(0x20);
    cbor.insert(cbor.end(), hash, hash + 32);

    // CLA INS P1 P2 | extended Lc | GetAssertion | CBOR | extended Le
    std::vector<uint8_t> expected = {0x80, 0x10, 0x00, 0x00,
                                     0x00, 0x00, (uint8_t)(cbor.size() + 1),
                                     0x02};
    expected.insert(expected.end(), cbor.begin(), cbor.end());
    expected.push_back(0x00); expected.push_back(0x00);

    EXPECT_EQ(expected, captured);
}

TEST_F(CTAP2HandlerTest, MakeCredentialReturnsErrorWhenSelectFails) {
    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _)).WillOnce(Return(false));

    uint8_t userId[8] = {};
    uint8_t hash[32] = {};
    uint8_t credId[64];
    uint8_t credIdLen = 0;
    uint8_t pubKey[65];

    int ret = handler.makeCredential(&mockNfc, "fidolock.local", "FidoLock",
                                     userId, 8, "alice", hash,
                                     credId, &credIdLen, pubKey);

    EXPECT_NE(CTAP2_OK, ret);
}

TEST_F(CTAP2HandlerTest, GetInfoReturnsErrorWhenResponseTooShort) {
    // SELECT succeeds (SW=0x9000), but GetInfo APDU returns only 2 bytes
    uint8_t selectResp[] = {0x90, 0x00};
    uint8_t getInfoResp[] = {0x90, 0x00};  // Only SW, no CTAP payload → responseLen < 3

    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _))
        .WillOnce(Invoke([&](uint8_t*, size_t, uint8_t* resp, size_t* len) {
            memcpy(resp, selectResp, sizeof(selectResp));
            *len = sizeof(selectResp);
            return true;
        }))
        .WillOnce(Invoke([&](uint8_t*, size_t, uint8_t* resp, size_t* len) {
            memcpy(resp, getInfoResp, sizeof(getInfoResp));
            *len = sizeof(getInfoResp);
            return true;
        }));

    uint8_t info[256];
    size_t infoLen = sizeof(info);
    int ret = handler.getInfo(&mockNfc, info, &infoLen);

    EXPECT_NE(CTAP2_OK, ret);
}

// ============================================================
// rpId Hash Extraction and Verification Tests
// (CTAP2 spec §6.1 step 19)
// These test the authData security invariant directly.
// ============================================================

// Pre-computed SHA-256("fidolock.local") — authoritative test vector.
// Regenerate with: printf '%s' 'fidolock.local' | openssl dgst -sha256 -hex
static const uint8_t EXPECTED_RP_ID_HASH[32] = {
    0xb1, 0x01, 0xb1, 0x20, 0x76, 0x19, 0x9d, 0x81,
    0x4c, 0x84, 0xba, 0x33, 0x2e, 0xdc, 0x92, 0x8e,
    0xea, 0xb6, 0xe4, 0x02, 0x6f, 0x81, 0xc0, 0xe8,
    0x53, 0x13, 0xac, 0xad, 0x3d, 0x88, 0xb5, 0x91
};

TEST(RpIdHash, CorrectHashExtractedFromAuthData) {
    uint8_t authData[37];
    CTAP2HandlerTest::buildAuthData(authData, EXPECTED_RP_ID_HASH, 0x01, 1);

    uint8_t rpIdHash[32];
    memcpy(rpIdHash, authData, 32);

    EXPECT_EQ(0, memcmp(EXPECTED_RP_ID_HASH, rpIdHash, 32));
}

TEST(RpIdHash, CorrectHashAccepted) {
    uint8_t authData[37];
    CTAP2HandlerTest::buildAuthData(authData, EXPECTED_RP_ID_HASH, 0x01, 5);

    EXPECT_EQ(0, memcmp(authData, EXPECTED_RP_ID_HASH, 32));
}

TEST(RpIdHash, WrongHashRejected) {
    uint8_t wrongHash[32];
    memcpy(wrongHash, EXPECTED_RP_ID_HASH, 32);
    wrongHash[0] ^= 0xFF;

    uint8_t authData[37];
    CTAP2HandlerTest::buildAuthData(authData, wrongHash, 0x01, 5);

    EXPECT_NE(0, memcmp(authData, EXPECTED_RP_ID_HASH, 32));
}

TEST(RpIdHash, AllZerosRejected) {
    uint8_t zeroHash[32] = {};
    uint8_t authData[37];
    CTAP2HandlerTest::buildAuthData(authData, zeroHash, 0x01, 5);

    EXPECT_NE(0, memcmp(authData, EXPECTED_RP_ID_HASH, 32));
}

TEST(RpIdHash, SingleBitFlipRejected) {
    uint8_t altHash[32];
    memcpy(altHash, EXPECTED_RP_ID_HASH, 32);
    altHash[31] ^= 0x01;

    uint8_t authData[37];
    CTAP2HandlerTest::buildAuthData(authData, altHash, 0x01, 5);

    EXPECT_NE(0, memcmp(authData, EXPECTED_RP_ID_HASH, 32));
}

// ============================================================
// Challenge Timeout Tests
// Uses the real TimeUtils::hasElapsed() — same function called by
// handleDetectingState() in main.cpp.
// ============================================================

TEST(ChallengeTimeout, ExpiredChallengeRejected) {
    uint32_t challengeTs = 1000;
    uint32_t responseTs  = challengeTs + CHALLENGE_TIMEOUT_MS + 1;

    EXPECT_TRUE(TimeUtils::hasElapsed(responseTs, challengeTs, CHALLENGE_TIMEOUT_MS));
}

TEST(ChallengeTimeout, WithinWindowAccepted) {
    uint32_t challengeTs = 1000;
    uint32_t responseTs  = challengeTs + 1800;  // 1.8 s — typical NFC latency

    EXPECT_FALSE(TimeUtils::hasElapsed(responseTs, challengeTs, CHALLENGE_TIMEOUT_MS));
}

TEST(ChallengeTimeout, ExactlyAtBoundaryAccepted) {
    uint32_t challengeTs = 1000;
    uint32_t responseTs  = challengeTs + CHALLENGE_TIMEOUT_MS;

    EXPECT_FALSE(TimeUtils::hasElapsed(responseTs, challengeTs, CHALLENGE_TIMEOUT_MS));
}

TEST(ChallengeTimeout, ZeroElapsedAccepted) {
    uint32_t challengeTs = 42000;
    uint32_t responseTs  = 42000;

    EXPECT_FALSE(TimeUtils::hasElapsed(responseTs, challengeTs, CHALLENGE_TIMEOUT_MS));
}

// ============================================================
// userPresent Flag Verification Tests
// (CTAP2 spec §6.1 step 21)
// ============================================================

TEST(UserPresent, FlagSetAccepted) {
    uint8_t authData[37];
    CTAP2HandlerTest::buildAuthData(authData, EXPECTED_RP_ID_HASH, 0x01, 10);

    EXPECT_TRUE((authData[32] & 0x01) != 0);
}

TEST(UserPresent, FlagClearRejected) {
    uint8_t authData[37];
    CTAP2HandlerTest::buildAuthData(authData, EXPECTED_RP_ID_HASH, 0x00, 10);

    EXPECT_FALSE((authData[32] & 0x01) != 0);
}

TEST(UserPresent, OnlyUVSetRejected) {
    uint8_t authData[37];
    CTAP2HandlerTest::buildAuthData(authData, EXPECTED_RP_ID_HASH, 0x04, 10);

    EXPECT_FALSE((authData[32] & 0x01) != 0);
}

TEST(UserPresent, UPAndUVBothSetAccepted) {
    uint8_t authData[37];
    CTAP2HandlerTest::buildAuthData(authData, EXPECTED_RP_ID_HASH, 0x05, 10);

    EXPECT_TRUE((authData[32] & 0x01) != 0);
}

// ============================================================
// Golden-vector parser tests (issue: CBOR parser hardening)
//
// These drive the production parseAssertionResponse /
// parseCredentialResponse through the public getAssertion / makeCredential
// API by feeding the MockNFCReader realistic, YubiKey-shaped CBOR. They are
// the regression guard for the bounds-checked parsers: a valid response is
// decoded field-for-field, and truncated/over-long inputs are rejected
// without reading out of bounds.
// ============================================================

namespace {

// status(0x00) | CBOR map{0x02:authData(37), 0x03:signature} | SW(0x9000)
std::vector<uint8_t> buildAssertionResponse(uint32_t counter, uint8_t flags,
                                            const uint8_t rpHash[32],
                                            const uint8_t* sig, uint8_t sigLen) {
    std::vector<uint8_t> r;
    r.push_back(0x00);                  // CTAP2 status OK
    r.push_back(0xA2);                  // map(2)
    r.push_back(0x02);                  // key: authData
    r.push_back(0x58); r.push_back(37); // byte string, length 37
    for (int i = 0; i < 32; i++) r.push_back(rpHash[i]);
    r.push_back(flags);
    r.push_back((counter >> 24) & 0xFF); r.push_back((counter >> 16) & 0xFF);
    r.push_back((counter >> 8) & 0xFF);  r.push_back(counter & 0xFF);
    r.push_back(0x03);                       // key: signature
    r.push_back(0x58); r.push_back(sigLen);  // byte string, length sigLen
    for (uint8_t i = 0; i < sigLen; i++) r.push_back(sig[i]);
    r.push_back(0x90); r.push_back(0x00);    // SW
    return r;
}

// status(0x00) | CBOR map{0x02:authData} | SW(0x9000), where authData carries
// the AT flag, a 16-byte AAGUID, the credential ID, and a COSE EC2 public key.
std::vector<uint8_t> buildMakeCredentialResponse(const uint8_t* credId,
                                                 uint8_t credIdLen,
                                                 const uint8_t x[32],
                                                 const uint8_t y[32]) {
    std::vector<uint8_t> ad;
    for (int i = 0; i < 32; i++) ad.push_back(0xCC);   // rpIdHash (unused here)
    ad.push_back(0x41);                                // flags: AT | UP
    ad.push_back(0); ad.push_back(0); ad.push_back(0); ad.push_back(1); // counter
    for (int i = 0; i < 16; i++) ad.push_back(0xAA);   // AAGUID
    ad.push_back((credIdLen >> 8) & 0xFF); ad.push_back(credIdLen & 0xFF);
    for (uint8_t i = 0; i < credIdLen; i++) ad.push_back(credId[i]);
    ad.push_back(0xA5);                          // COSE map(5)
    ad.push_back(0x01); ad.push_back(0x02);      // kty: EC2
    ad.push_back(0x03); ad.push_back(0x26);      // alg: ES256 (-7)
    ad.push_back(0x20); ad.push_back(0x01);      // crv (-1): P-256
    ad.push_back(0x21); ad.push_back(0x58); ad.push_back(0x20); // -2 (x): bytes(32)
    for (int i = 0; i < 32; i++) ad.push_back(x[i]);
    ad.push_back(0x22); ad.push_back(0x58); ad.push_back(0x20); // -3 (y): bytes(32)
    for (int i = 0; i < 32; i++) ad.push_back(y[i]);

    std::vector<uint8_t> r;
    r.push_back(0x00);                                  // status OK
    r.push_back(0xA1);                                  // map(1)
    r.push_back(0x02);                                  // key: authData
    r.push_back(0x58); r.push_back((uint8_t)ad.size()); // byte string, length
    r.insert(r.end(), ad.begin(), ad.end());
    r.push_back(0x90); r.push_back(0x00);               // SW
    return r;
}

// gmock action: copy a prepared response vector into the sendAPDU out-params.
auto replyWith(const std::vector<uint8_t>& bytes) {
    return [&bytes](uint8_t*, size_t, uint8_t* resp, size_t* len) {
        memcpy(resp, bytes.data(), bytes.size());
        *len = bytes.size();
        return true;
    };
}

} // namespace

TEST_F(CTAP2HandlerTest, GetAssertionParsesValidResponse) {
    uint8_t sig[70];
    for (int i = 0; i < 70; i++) sig[i] = (uint8_t)(i + 1);
    auto resp = buildAssertionResponse(42, 0x01, EXPECTED_RP_ID_HASH, sig, 70);

    std::vector<uint8_t> sel = {0x90, 0x00};
    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _))
        .WillOnce(Invoke(replyWith(sel)))
        .WillOnce(Invoke(replyWith(resp)));

    uint8_t hash[32] = {};
    CTAP2Response out{};

    int ret = handler.getAssertion(&mockNfc, "fidolock.local", hash, &out);

    EXPECT_EQ(CTAP2_OK, ret);
    EXPECT_TRUE(out.userPresent);
    EXPECT_EQ(42u, out.counter);
    EXPECT_EQ(0, memcmp(out.rpIdHash, EXPECTED_RP_ID_HASH, 32));
    EXPECT_EQ(70, out.signatureLen);
    EXPECT_EQ(0, memcmp(out.signature, sig, 70));
}

TEST_F(CTAP2HandlerTest, GetAssertionRejectsTruncatedAuthData) {
    // authData header claims 37 bytes but only 10 follow before the SW.
    std::vector<uint8_t> resp = {0x00, 0xA1, 0x02, 0x58, 37};
    for (int i = 0; i < 10; i++) resp.push_back(0x11);
    resp.push_back(0x90); resp.push_back(0x00);

    std::vector<uint8_t> sel = {0x90, 0x00};
    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _))
        .WillOnce(Invoke(replyWith(sel)))
        .WillOnce(Invoke(replyWith(resp)));

    uint8_t hash[32] = {};
    CTAP2Response out{};

    int ret = handler.getAssertion(&mockNfc, "fidolock.local", hash, &out);
    EXPECT_NE(CTAP2_OK, ret);  // rejected, and no out-of-bounds read
}

namespace {

// Discoverable-credential response as a YubiKey returns it with no allowList:
// status | map(5){1: {"id","type"}, 2: authData, 3: sig, 4: {"id": userId},
// 5: numberOfCredentials} | SW. userIdLen lets a test claim more bytes than
// it supplies to exercise truncation inside the skipped 0x04 entity.
std::vector<uint8_t> buildResidentAssertionResponse(const uint8_t credId[16],
                                                    const uint8_t rpHash[32],
                                                    uint32_t counter,
                                                    const uint8_t* sig,
                                                    uint8_t sigLen,
                                                    uint8_t userIdLen = 16,
                                                    bool truncateUser = false) {
    std::vector<uint8_t> r = {0x00, 0xA5};
    // 0x01: credential descriptor
    r.insert(r.end(), {0x01, 0xA2, 0x62, 'i', 'd', 0x50});
    r.insert(r.end(), credId, credId + 16);
    r.insert(r.end(), {0x64, 't', 'y', 'p', 'e',
                       0x6A, 'p', 'u', 'b', 'l', 'i', 'c', '-', 'k', 'e', 'y'});
    // 0x02: authData
    r.insert(r.end(), {0x02, 0x58, 37});
    r.insert(r.end(), rpHash, rpHash + 32);
    r.push_back(0x01);                                      // flags: UP
    r.push_back((counter >> 24) & 0xFF); r.push_back((counter >> 16) & 0xFF);
    r.push_back((counter >> 8) & 0xFF);  r.push_back(counter & 0xFF);
    // 0x03: signature
    r.insert(r.end(), {0x03, 0x58, sigLen});
    r.insert(r.end(), sig, sig + sigLen);
    // 0x04: user entity; its id holds bytes that look like top-level keys
    // (0x02 / 0x03 followed by byte-string headers) to catch shallow skipping.
    r.insert(r.end(), {0x04, 0xA1, 0x62, 'i', 'd', (uint8_t)(0x40 | userIdLen)});
    if (!truncateUser) {
        for (uint8_t i = 0; i < userIdLen; i++) r.push_back(i % 2 ? 0x58 : 0x02);
        // 0x05: numberOfCredentials
        r.insert(r.end(), {0x05, 0x01});
    } else {
        r.insert(r.end(), {0x02, 0x58, 0x02});              // 3 of userIdLen bytes
    }
    r.push_back(0x90); r.push_back(0x00);
    return r;
}

} // namespace

TEST_F(CTAP2HandlerTest, GetAssertionParsesDiscoverableResponseWithUserEntity) {
    uint8_t credId[16];
    for (int i = 0; i < 16; i++) credId[i] = (uint8_t)(0xC0 + i);
    uint8_t sig[70];
    for (int i = 0; i < 70; i++) sig[i] = (uint8_t)(i + 1);
    auto resp = buildResidentAssertionResponse(credId, EXPECTED_RP_ID_HASH,
                                               42, sig, 70);

    std::vector<uint8_t> sel = {0x90, 0x00};
    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _))
        .WillOnce(Invoke(replyWith(sel)))
        .WillOnce(Invoke(replyWith(resp)));

    uint8_t hash[32] = {};
    CTAP2Response out{};
    int ret = handler.getAssertion(&mockNfc, "fidolock.local", hash, &out);

    EXPECT_EQ(CTAP2_OK, ret);
    EXPECT_EQ(16, out.credentialIdLen);
    EXPECT_EQ(0, memcmp(out.credentialId, credId, 16));
    EXPECT_EQ(42u, out.counter);
    EXPECT_TRUE(out.userPresent);
    EXPECT_EQ(0, memcmp(out.rpIdHash, EXPECTED_RP_ID_HASH, 32));
    EXPECT_EQ(70, out.signatureLen);
    EXPECT_EQ(0, memcmp(out.signature, sig, 70));
}

TEST_F(CTAP2HandlerTest, GetAssertionRejectsTruncatedUserEntity) {
    uint8_t credId[16] = {};
    uint8_t sig[70] = {};
    auto resp = buildResidentAssertionResponse(credId, EXPECTED_RP_ID_HASH,
                                               42, sig, 70, 16, true);

    std::vector<uint8_t> sel = {0x90, 0x00};
    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _))
        .WillOnce(Invoke(replyWith(sel)))
        .WillOnce(Invoke(replyWith(resp)));

    uint8_t hash[32] = {};
    CTAP2Response out{};
    int ret = handler.getAssertion(&mockNfc, "fidolock.local", hash, &out);
    EXPECT_NE(CTAP2_OK, ret);
}

// ============================================================
// skipCBORItem boundaries
//
// Unknown response keys are skipped whole by skipCBORItem (nesting capped at
// kCBORMaxSkipDepth = 4). These cases put a crafted value under key 0x04 and
// drive it through the public getAssertion: valid items must be skipped
// exactly, malformed ones rejected without reading past the input.
// ============================================================

namespace {

const uint8_t kSkipSig[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

enum class Placement { BeforeSignature, Last };

// status | map(3){0x02: authData, 0x03: signature, 0x04: value} | SW.
// BeforeSignature: a skip that consumes too few or too many bytes corrupts
// the signature entry that follows. Last: the value ends exactly at the end
// of the CBOR input, so its length can be matched against what remains.
std::vector<uint8_t> buildResponseSkipping(const std::vector<uint8_t>& value,
                                           Placement where) {
    std::vector<uint8_t> skipped = {0x04};
    skipped.insert(skipped.end(), value.begin(), value.end());
    std::vector<uint8_t> sig = {0x03, 0x48};                // bytes(8)
    sig.insert(sig.end(), kSkipSig, kSkipSig + sizeof(kSkipSig));

    std::vector<uint8_t> r = {0x00, 0xA3, 0x02, 0x58, 37};
    r.insert(r.end(), EXPECTED_RP_ID_HASH, EXPECTED_RP_ID_HASH + 32);
    r.insert(r.end(), {0x01, 0x00, 0x00, 0x00, 42});        // flags: UP, counter
    const auto& first  = (where == Placement::Last) ? sig : skipped;
    const auto& second = (where == Placement::Last) ? skipped : sig;
    r.insert(r.end(), first.begin(), first.end());
    r.insert(r.end(), second.begin(), second.end());
    r.insert(r.end(), {0x90, 0x00});
    return r;
}

// Byte-string header whose length uses the 1-, 2-, 4- or 8-byte extended
// form (additional info 24..27).
std::vector<uint8_t> bytesHeader(uint8_t info, uint64_t len) {
    std::vector<uint8_t> h = {(uint8_t)(0x40 | info)};
    for (size_t i = (size_t)1 << (info - 24); i-- > 0;) {
        h.push_back((uint8_t)(len >> (8 * i)));
    }
    return h;
}

// Real getAssertion against `resp` on a fresh mock, so tests can loop.
int getAssertionWith(const std::vector<uint8_t>& resp, CTAP2Response* out) {
    MockNFCReader nfc;
    CTAP2Handler handler;
    std::vector<uint8_t> sel = {0x90, 0x00};
    EXPECT_CALL(nfc, sendAPDU(_, _, _, _))
        .WillOnce(Invoke(replyWith(sel)))
        .WillOnce(Invoke(replyWith(resp)));
    uint8_t hash[32] = {};
    return handler.getAssertion(&nfc, "fidolock.local", hash, out);
}

void expectDecoded(const CTAP2Response& out) {
    EXPECT_EQ(42u, out.counter);
    EXPECT_TRUE(out.userPresent);
    ASSERT_EQ(sizeof(kSkipSig), out.signatureLen);
    EXPECT_EQ(0, memcmp(out.signature, kSkipSig, sizeof(kSkipSig)));
}

} // namespace

TEST(SkipCBORItem, NestingAtDepthLimitIsSkipped) {
    // The skipped value is depth 0, so four nested arrays put the innermost
    // item at depth 4, the deepest allowed.
    CTAP2Response out{};
    auto resp = buildResponseSkipping({0x81, 0x81, 0x81, 0x81, 0x00},
                                      Placement::BeforeSignature);
    ASSERT_EQ(CTAP2_OK, getAssertionWith(resp, &out));
    expectDecoded(out);
}

TEST(SkipCBORItem, NestingBeyondDepthLimitIsRejected) {
    CTAP2Response out{};
    auto resp = buildResponseSkipping({0x81, 0x81, 0x81, 0x81, 0x81, 0x00},
                                      Placement::BeforeSignature);
    EXPECT_NE(CTAP2_OK, getAssertionWith(resp, &out));
}

TEST(SkipCBORItem, ZeroLengthItemsAreSkipped) {
    // [h'', "", [], {}]
    CTAP2Response out{};
    auto resp = buildResponseSkipping({0x84, 0x40, 0x60, 0x80, 0xA0},
                                      Placement::BeforeSignature);
    ASSERT_EQ(CTAP2_OK, getAssertionWith(resp, &out));
    expectDecoded(out);
}

TEST(SkipCBORItem, ZeroLengthItemAtEndOfInputIsSkipped) {
    CTAP2Response out{};
    auto resp = buildResponseSkipping({0x40}, Placement::Last);
    ASSERT_EQ(CTAP2_OK, getAssertionWith(resp, &out));
    expectDecoded(out);
}

TEST(SkipCBORItem, ExtendedLengthEqualToRemainingInputIsSkipped) {
    for (uint8_t info = 24; info <= 27; info++) {
        SCOPED_TRACE((int)info);
        auto value = bytesHeader(info, 16);
        value.insert(value.end(), 16, 0x02);   // looks like key 0x02 if misread
        CTAP2Response out{};
        ASSERT_EQ(CTAP2_OK,
                  getAssertionWith(buildResponseSkipping(value, Placement::Last),
                                   &out));
        expectDecoded(out);
    }
}

TEST(SkipCBORItem, ExtendedLengthBeyondRemainingInputIsRejected) {
    for (uint8_t info = 24; info <= 27; info++) {
        SCOPED_TRACE((int)info);
        auto value = bytesHeader(info, 17);    // one more than supplied
        value.insert(value.end(), 16, 0x02);
        CTAP2Response out{};
        EXPECT_NE(CTAP2_OK,
                  getAssertionWith(buildResponseSkipping(value, Placement::Last),
                                   &out));
    }
}

TEST(SkipCBORItem, MaximumExtendedLengthIsRejected) {
    // Lengths that wrap a 32-bit (ESP32) or 64-bit offset if added unchecked.
    for (uint64_t len : {(uint64_t)UINT32_MAX, (uint64_t)UINT64_MAX}) {
        SCOPED_TRACE(len);
        auto value = bytesHeader(len > UINT32_MAX ? 27 : 26, len);
        value.insert(value.end(), 16, 0x02);
        CTAP2Response out{};
        EXPECT_NE(CTAP2_OK,
                  getAssertionWith(buildResponseSkipping(value, Placement::Last),
                                   &out));
    }
}

TEST(SkipCBORItem, TruncatedLengthHeaderIsRejected) {
    // Additional info 27 announces 8 length bytes; only 3 follow.
    CTAP2Response out{};
    auto resp = buildResponseSkipping({0x5B, 0x00, 0x00, 0x00}, Placement::Last);
    EXPECT_NE(CTAP2_OK, getAssertionWith(resp, &out));
}

TEST(SkipCBORItem, ContainerCountBeyondRemainingInputIsRejected) {
    // map with 2^64-1 pairs: rejected before the item loop (and before the
    // pair count is doubled).
    CTAP2Response out{};
    auto resp = buildResponseSkipping(
        {0xBB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, Placement::Last);
    EXPECT_NE(CTAP2_OK, getAssertionWith(resp, &out));
}

TEST(SkipCBORItem, IndefiniteLengthIsRejected) {
    CTAP2Response out{};
    auto resp = buildResponseSkipping({0xBF, 0xFF}, Placement::BeforeSignature);
    EXPECT_NE(CTAP2_OK, getAssertionWith(resp, &out));
}

TEST_F(CTAP2HandlerTest, MakeCredentialParsesValidResponse) {
    uint8_t credId[16];
    uint8_t x[32], y[32];
    for (int i = 0; i < 16; i++) credId[i] = (uint8_t)(0x10 + i);
    for (int i = 0; i < 32; i++) { x[i] = (uint8_t)(0x40 + i); y[i] = (uint8_t)(0x80 + i); }
    auto resp = buildMakeCredentialResponse(credId, 16, x, y);

    std::vector<uint8_t> sel = {0x90, 0x00};
    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _))
        .WillOnce(Invoke(replyWith(sel)))
        .WillOnce(Invoke(replyWith(resp)));

    uint8_t userId[8] = {};
    uint8_t hash[32] = {};
    uint8_t outCredId[64] = {};
    uint8_t outCredIdLen = 0;
    uint8_t outPubKey[65] = {};

    int ret = handler.makeCredential(&mockNfc, "fidolock.local", "FidoLock",
                                     userId, 8, "alice", hash,
                                     outCredId, &outCredIdLen, outPubKey);

    EXPECT_EQ(CTAP2_OK, ret);
    EXPECT_EQ(16, outCredIdLen);
    EXPECT_EQ(0, memcmp(outCredId, credId, 16));
    EXPECT_EQ(0x04, outPubKey[0]);
    EXPECT_EQ(0, memcmp(&outPubKey[1], x, 32));
    EXPECT_EQ(0, memcmp(&outPubKey[33], y, 32));
}

TEST_F(CTAP2HandlerTest, MakeCredentialRejectsOversizedCredentialId) {
    // authData advertises a credential ID length far beyond the 64-byte max.
    std::vector<uint8_t> ad;
    for (int i = 0; i < 32; i++) ad.push_back(0xCC);   // rpIdHash
    ad.push_back(0x41);                                // flags: AT | UP
    ad.push_back(0); ad.push_back(0); ad.push_back(0); ad.push_back(1); // counter
    for (int i = 0; i < 16; i++) ad.push_back(0xAA);   // AAGUID
    ad.push_back(0xFF); ad.push_back(0xFF);            // credIdLen = 65535
    // pad to the 55-byte minimum so the length field itself is in range
    while (ad.size() < 55) ad.push_back(0x00);

    std::vector<uint8_t> resp = {0x00, 0xA1, 0x02, 0x58, (uint8_t)ad.size()};
    resp.insert(resp.end(), ad.begin(), ad.end());
    resp.push_back(0x90); resp.push_back(0x00);

    std::vector<uint8_t> sel = {0x90, 0x00};
    EXPECT_CALL(mockNfc, sendAPDU(_, _, _, _))
        .WillOnce(Invoke(replyWith(sel)))
        .WillOnce(Invoke(replyWith(resp)));

    uint8_t userId[8] = {};
    uint8_t hash[32] = {};
    uint8_t outCredId[64] = {};
    uint8_t outCredIdLen = 0;
    uint8_t outPubKey[65] = {};

    int ret = handler.makeCredential(&mockNfc, "fidolock.local", "FidoLock",
                                     userId, 8, "alice", hash,
                                     outCredId, &outCredIdLen, outPubKey);
    EXPECT_NE(CTAP2_OK, ret);  // rejected, and no out-of-bounds read
}

int main(int argc, char** argv) {
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
