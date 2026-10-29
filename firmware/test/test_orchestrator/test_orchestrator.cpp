/**
 * FidoLock - Orchestrator Unit Tests
 *
 * Tests the FidoLockOrchestrator state machine.
 * All external dependencies are injected as test doubles.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string.h>

#include "../test_hal/test_hal.h"
#include "../test_mocks/mock_interfaces.h"
#include "../../src/application/orchestrator/fidolock_orchestrator.h"
#include "../../src/config.h"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::DoAll;
using ::testing::SetArgPointee;
using ::testing::InvokeWithoutArgs;
using ::testing::NiceMock;
using ::testing::StrictMock;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class OrchestratorTest : public ::testing::Test {
protected:
    NiceMock<MockNFCReader>       mockNfc;
    NiceMock<MockCTAP2Handler>    mockCtap2;
    NiceMock<MockCryptoEngine>    mockCrypto;
    NiceMock<MockCredentialStore> mockStore;
    NiceMock<MockAccessController> mockAccess;
    NiceMock<MockUserInterface>   mockUi;
    NiceMock<MockSystemControl>   mockSystem;
    FakeOutputStream              fakeLog;
    TestTimeProvider              timeProvider;

    StoredCredential _fakeCred;
    CTAP2Response    _fakeResponse;

    FidoLockOrchestrator* orch = nullptr;

    void SetUp() override {
        // Prepare a credential that satisfies all security checks
        memset(&_fakeCred, 0, sizeof(_fakeCred));
        _fakeCred.credentialIdLen = 4;
        memset(_fakeCred.credentialId, 0x01, 4);
        _fakeCred.lastCounter = 0;

        // Prepare a response that satisfies all security checks.
        // rpIdHash is all-zeros which matches mock sha256 output (also zeros).
        memset(&_fakeResponse, 0, sizeof(_fakeResponse));
        _fakeResponse.credentialIdLen = 4;
        memset(_fakeResponse.credentialId, 0x01, 4);
        _fakeResponse.userPresent  = true;
        _fakeResponse.counter      = 1;
        _fakeResponse.signatureLen = 1;

        orch = new FidoLockOrchestrator(
            mockNfc, mockCtap2, mockCrypto, mockStore,
            mockAccess, mockUi, timeProvider, mockSystem, fakeLog);

        // ---- Sensible ON_CALL defaults ----
        ON_CALL(mockNfc, detectCard(_, _)).WillByDefault(Return(false));

        ON_CALL(mockCrypto, generateChallenge(_, _)).WillByDefault(Return(true));

        // sha256 writes zeros to output buffer (so rpIdHash check passes when
        // _fakeResponse.rpIdHash is also zero-initialized)
        ON_CALL(mockCrypto, sha256(_, _, _))
            .WillByDefault(Invoke([](const uint8_t*, size_t, uint8_t* out) {
                memset(out, 0, 32);
                return true;
            }));

        ON_CALL(mockCrypto, verifyECDSA(_, _, _, _)).WillByDefault(Return(true));

        ON_CALL(mockStore, getCount()).WillByDefault(Return(0)); // no creds
        ON_CALL(mockStore, findByCredentialId(_, _)).WillByDefault(Return(nullptr));
        ON_CALL(mockStore, updateCounter(_, _, _)).WillByDefault(Return(true));

        ON_CALL(mockCtap2, getAssertion(_, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<3>(_fakeResponse),
                                 Return(CTAP2_OK)));

        ON_CALL(mockAccess, grantAccess(_)).WillByDefault(Return());
        ON_CALL(mockAccess, denyAccess()).WillByDefault(Return());
        ON_CALL(mockSystem, delayMs(_)).WillByDefault(Return());
        ON_CALL(mockSystem, restart()).WillByDefault(Return());
    }

    void TearDown() override { delete orch; }

    // Move the orchestrator from IDLE to DETECTING by presenting a card.
    void tickCardDetected() {
        ON_CALL(mockNfc, detectCard(_, _)).WillByDefault(Return(true));
        orch->tick();
        ON_CALL(mockNfc, detectCard(_, _)).WillByDefault(Return(false));
    }

    // Set up store to hold one credential, found by its credential ID.
    void setupOneCredential() {
        ON_CALL(mockStore, getCount()).WillByDefault(Return(1));
        ON_CALL(mockStore, findByCredentialId(_, _))
            .WillByDefault(Return(&_fakeCred));
    }

    // Advance the orchestrator all the way through a happy-path auth to GRANTED.
    void tickThroughHappyPath() {
        setupOneCredential();
        tickCardDetected(); // IDLE → DETECTING
        orch->tick();       // DETECTING → GRANTED
    }
};

// ===========================================================================
// IDLE state
// ===========================================================================

TEST_F(OrchestratorTest, IdleNoCard_UiUpdateCalled) {
    EXPECT_CALL(mockUi, update()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, IdleNoCard_ShowProcessingNotCalled) {
    EXPECT_CALL(mockUi, showProcessing()).Times(0);
    orch->tick();
}

TEST_F(OrchestratorTest, IdleCardDetected_ShowsProcessing) {
    EXPECT_CALL(mockUi, showProcessing()).Times(1);
    ON_CALL(mockNfc, detectCard(_, _)).WillByDefault(Return(true));
    orch->tick();
}

// ===========================================================================
// DETECTING state — early exits
// ===========================================================================

TEST_F(OrchestratorTest, DetectingNoCredentials_GetAssertionNotCalled) {
    tickCardDetected();
    // getCount default: 0 enrolled credentials
    EXPECT_CALL(mockCtap2, getAssertion(_, _, _, _)).Times(0);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingNoCredentials_LeadsToDenied) {
    tickCardDetected();
    orch->tick(); // DETECTING → DENIED
    // Verify we're in DENIED by checking next tick shows the error
    EXPECT_CALL(mockUi, showError()).Times(1);
    orch->tick();
}

// The key holds no credential for rpId: it answers NO_CREDENTIALS. No
// assertion was produced, so the denial does not count toward lockout.
TEST_F(OrchestratorTest, DetectingKeyHasNoCredential_LeadsToDenied) {
    setupOneCredential();
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(Return(CTAP2_ERR_NO_CREDENTIALS));
    EXPECT_CALL(mockStore, findByCredentialId(_, _)).Times(0);
    EXPECT_CALL(mockUi, showError()).Times(1);
    EXPECT_CALL(mockAccess, denyAccess()).Times(0);

    tickCardDetected();
    orch->tick();  // DETECTING → DENIED
    orch->tick();  // DENIED
}

// Without an allowList the response must name the credential (key 0x01);
// otherwise the door cannot know whose public key to verify against.
TEST_F(OrchestratorTest, DetectingResponseWithoutCredentialId_LeadsToDenied) {
    setupOneCredential();
    CTAP2Response noId = _fakeResponse;
    noId.credentialIdLen = 0;
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(noId), Return(CTAP2_OK)));
    EXPECT_CALL(mockStore, findByCredentialId(_, _)).Times(0);
    EXPECT_CALL(mockAccess, grantAccess(_)).Times(0);

    tickCardDetected();
    orch->tick();
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingChallengeFails_LeadsToError) {
    setupOneCredential();
    tickCardDetected();
    ON_CALL(mockCrypto, generateChallenge(_, _)).WillByDefault(Return(false));
    orch->tick(); // DETECTING → ERROR
    // Verify we're in ERROR by checking next tick calls restart
    EXPECT_CALL(mockSystem, restart()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingCTAP2Error_LeadsToDenied) {
    setupOneCredential();
    tickCardDetected();
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(Return(CTAP2_ERR_TIMEOUT));
    orch->tick(); // DETECTING → DENIED
    EXPECT_CALL(mockUi, showError()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingCTAP2Error_FindByCredentialIdNotCalled) {
    setupOneCredential();
    tickCardDetected();
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(Return(CTAP2_ERR_NO_CREDENTIALS));
    EXPECT_CALL(mockStore, findByCredentialId(_, _)).Times(0);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingChallengeExpired_LeadsToDenied) {
    setupOneCredential();
    tickCardDetected();
    // Make getAssertion advance time past the challenge timeout
    CTAP2Response resp = _fakeResponse;
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(DoAll(
            InvokeWithoutArgs([this]() {
                timeProvider.advance(CHALLENGE_TIMEOUT_MS + 1);
            }),
            SetArgPointee<3>(resp),
            Return(CTAP2_OK)));
    orch->tick(); // DETECTING → DENIED
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingCredentialNotFound_LeadsToDenied) {
    setupOneCredential();
    ON_CALL(mockStore, findByCredentialId(_, _)).WillByDefault(Return(nullptr));
    tickCardDetected();
    orch->tick(); // DETECTING → DENIED
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingRpIdHashMismatch_LeadsToDenied) {
    setupOneCredential();
    tickCardDetected();
    // Make rpIdHash in response non-zero so it won't match zeros from sha256
    CTAP2Response resp = _fakeResponse;
    memset(resp.rpIdHash, 0xFF, sizeof(resp.rpIdHash));
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(resp), Return(CTAP2_OK)));
    EXPECT_CALL(mockCrypto, verifyECDSA(_, _, _, _)).Times(0);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingUserPresentFalse_LeadsToDenied) {
    setupOneCredential();
    tickCardDetected();
    CTAP2Response resp = _fakeResponse;
    resp.userPresent = false;
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(resp), Return(CTAP2_OK)));
    EXPECT_CALL(mockCrypto, verifyECDSA(_, _, _, _)).Times(0);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingInvalidSignature_LeadsToDenied) {
    setupOneCredential();
    tickCardDetected();
    ON_CALL(mockCrypto, verifyECDSA(_, _, _, _)).WillByDefault(Return(false));
    orch->tick(); // DETECTING → DENIED
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingCounterReplay_ShowsCloneAlert) {
    setupOneCredential();
    tickCardDetected();
    CTAP2Response resp = _fakeResponse;
    resp.counter = 0; // not > lastCounter (0)
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(resp), Return(CTAP2_OK)));
    EXPECT_CALL(mockUi, showCloneAlert()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingCounterReplay_UpdateCounterNotCalled) {
    setupOneCredential();
    tickCardDetected();
    CTAP2Response resp = _fakeResponse;
    resp.counter = 0;
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(resp), Return(CTAP2_OK)));
    EXPECT_CALL(mockStore, updateCounter(_, _, _)).Times(0);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingHappyPath_UpdatesCounter) {
    setupOneCredential();
    tickCardDetected();
    EXPECT_CALL(mockStore, updateCounter(_, _, 1)).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, DetectingHappyPath_LeadsToGranted) {
    tickThroughHappyPath(); // IDLE → DETECTING → GRANTED
    EXPECT_CALL(mockAccess, grantAccess(_)).Times(1);
    orch->tick();
}

// ===========================================================================
// GRANTED state
// ===========================================================================

TEST_F(OrchestratorTest, GrantedCallsShowSuccess) {
    tickThroughHappyPath();
    EXPECT_CALL(mockUi, showSuccess()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, GrantedCallsGrantAccessWithConfiguredDuration) {
    tickThroughHappyPath();
    EXPECT_CALL(mockAccess, grantAccess(ACCESS_DURATION_MS)).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, GrantedCallsDelayMs) {
    tickThroughHappyPath();
    EXPECT_CALL(mockSystem, delayMs(ACCESS_DURATION_MS + 500)).Times(1);
    orch->tick();
}

// Regression guard (relay never re-locks): every grant must be followed by an
// explicit lock() so the relay cannot latch open. AccessController's auto-lock
// timer lives in update(), which cannot run while handleGranted() blocks on
// delayMs() — the orchestrator must secure the door itself.
TEST_F(OrchestratorTest, GrantedReLocksAfterAccessWindow) {
    tickThroughHappyPath();
    EXPECT_CALL(mockAccess, grantAccess(_)).Times(1);
    EXPECT_CALL(mockAccess, lock()).Times(1);
    orch->tick(); // GRANTED handler
}

// A valid authentication during a lockout window must not flash success on a
// door that stays shut: no showSuccess(), no grantAccess(); the user sees the
// lockout indication instead, so a valid key is not mistaken for a broken one.
TEST_F(OrchestratorTest, GrantedWhileLockedOut_ShowsLockoutNotSuccess) {
    setupOneCredential();
    ON_CALL(mockAccess, isLockedOut()).WillByDefault(Return(true));
    tickCardDetected();  // IDLE → DETECTING
    orch->tick();        // DETECTING → GRANTED
    EXPECT_CALL(mockUi, showSuccess()).Times(0);
    EXPECT_CALL(mockAccess, grantAccess(_)).Times(0);
    EXPECT_CALL(mockUi, showError()).Times(0);
    EXPECT_CALL(mockUi, showLockout()).Times(1);
    orch->tick();        // GRANTED handler withholds access
}

TEST_F(OrchestratorTest, GrantedTransitionsToIdle) {
    tickThroughHappyPath();
    orch->tick(); // GRANTED → IDLE
    // Verify back in IDLE: no card → no showProcessing
    EXPECT_CALL(mockUi, showProcessing()).Times(0);
    orch->tick();
}

// ===========================================================================
// DENIED state
// ===========================================================================

TEST_F(OrchestratorTest, DeniedCallsShowError) {
    tickCardDetected();
    orch->tick(); // DETECTING → DENIED (no creds)
    EXPECT_CALL(mockUi, showError()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, DeniedAfterVerificationFailure_CallsDenyAccess) {
    setupOneCredential();
    ON_CALL(mockCrypto, verifyECDSA(_, _, _, _)).WillByDefault(Return(false));
    tickCardDetected();
    orch->tick(); // DETECTING → DENIED
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, DeniedCallsDelayMs) {
    tickCardDetected();
    orch->tick(); // DETECTING → DENIED
    EXPECT_CALL(mockSystem, delayMs(2000)).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, DeniedTransitionsToIdle) {
    tickCardDetected();
    orch->tick(); // DETECTING → DENIED
    orch->tick(); // DENIED → IDLE
    // Verify back in IDLE
    EXPECT_CALL(mockUi, showProcessing()).Times(0);
    orch->tick();
}

// ===========================================================================
// Failed-attempt accounting
//
// Lockout counts rejected assertions only. A key pulled out of the field, a
// non-FIDO tag or an authenticator error never produced a signature to check,
// so it is denied without recording a failed attempt.
// ===========================================================================
class FailedAttemptAccountingTest : public OrchestratorTest {
protected:
    // Run one attempt from IDLE through the DENIED handler.
    void runDeniedAttempt() {
        tickCardDetected();  // IDLE → DETECTING
        orch->tick();        // DETECTING → DENIED
        orch->tick();        // DENIED → IDLE
    }

    void respondWith(const CTAP2Response& resp) {
        ON_CALL(mockCtap2, getAssertion(_, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<3>(resp), Return(CTAP2_OK)));
    }
};

TEST_F(FailedAttemptAccountingTest, KeyLeftField_NotCounted) {
    setupOneCredential();
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(Return(CTAP2_ERR_TIMEOUT));
    EXPECT_CALL(mockAccess, denyAccess()).Times(0);
    EXPECT_CALL(mockAccess, lock()).Times(1);  // relay still forced off
    EXPECT_CALL(mockUi, showError()).Times(1);
    runDeniedAttempt();
    EXPECT_NE(std::string::npos, fakeLog.output().find("not counted"));
}

TEST_F(FailedAttemptAccountingTest, NonFidoTag_NotCounted) {
    setupOneCredential();
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(Return(CTAP2_ERR_INVALID_COMMAND));  // SELECT failed
    EXPECT_CALL(mockAccess, denyAccess()).Times(0);
    runDeniedAttempt();
}

TEST_F(FailedAttemptAccountingTest, NoCredentialsRegistered_NotCounted) {
    EXPECT_CALL(mockAccess, denyAccess()).Times(0);
    EXPECT_CALL(mockAccess, lock()).Times(1);
    runDeniedAttempt();
}

TEST_F(FailedAttemptAccountingTest, MissingCredentialId_Counted) {
    setupOneCredential();
    CTAP2Response resp = _fakeResponse;
    resp.credentialIdLen = 0;
    respondWith(resp);
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    runDeniedAttempt();
}

TEST_F(FailedAttemptAccountingTest, UserPresentFalse_Counted) {
    setupOneCredential();
    CTAP2Response resp = _fakeResponse;
    resp.userPresent = false;
    respondWith(resp);
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    runDeniedAttempt();
}

TEST_F(FailedAttemptAccountingTest, CounterReplay_Counted) {
    setupOneCredential();
    CTAP2Response resp = _fakeResponse;
    resp.counter = 0;  // not > lastCounter (0)
    respondWith(resp);
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    runDeniedAttempt();
}

// The decision is per attempt: a rejected assertion must not make the next,
// incomplete attempt count too.
TEST_F(FailedAttemptAccountingTest, KeyLeftFieldAfterRejectedAssertion_NotCounted) {
    setupOneCredential();
    ON_CALL(mockCrypto, verifyECDSA(_, _, _, _)).WillByDefault(Return(false));
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    runDeniedAttempt();  // bad signature: counted

    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(Return(CTAP2_ERR_TIMEOUT));
    runDeniedAttempt();  // key left the field: not counted
}

// ===========================================================================
// Lockout indication
//
// While locked out the node must not show the dim-green "ready" state: the
// idle indication switches to the lockout pattern and back when it expires.
// ===========================================================================

TEST_F(OrchestratorTest, DeniedWithoutLockout_ReturnsToReady) {
    EXPECT_CALL(mockUi, setIdle()).Times(1);
    EXPECT_CALL(mockUi, showLockout()).Times(0);
    tickCardDetected();
    orch->tick();  // DETECTING → DENIED
    orch->tick();  // DENIED → IDLE
}

// The attempt that trips the lockout shows it as soon as the denial ends.
TEST_F(OrchestratorTest, DeniedTrippingLockout_ShowsLockout) {
    bool lockedOut = false;
    ON_CALL(mockAccess, isLockedOut())
        .WillByDefault(Invoke([&lockedOut]() { return lockedOut; }));
    ON_CALL(mockAccess, denyAccess())
        .WillByDefault(Invoke([&lockedOut]() { lockedOut = true; }));
    setupOneCredential();
    ON_CALL(mockCrypto, verifyECDSA(_, _, _, _)).WillByDefault(Return(false));
    tickCardDetected();
    orch->tick();  // DETECTING → DENIED

    EXPECT_CALL(mockUi, showError()).Times(1);
    EXPECT_CALL(mockUi, showLockout()).Times(1);
    EXPECT_CALL(mockUi, setIdle()).Times(0);
    orch->tick();  // DENIED → IDLE (locked out)
    orch->tick();  // IDLE: already showing lockout, no repeat
}

// Shown once on entering lockout, not re-triggered on every idle tick.
TEST_F(OrchestratorTest, IdleWhileLockedOut_ShowsLockoutOnce) {
    ON_CALL(mockAccess, isLockedOut()).WillByDefault(Return(true));
    EXPECT_CALL(mockUi, showLockout()).Times(1);
    orch->tick();
    orch->tick();
    orch->tick();
}

TEST_F(OrchestratorTest, IdleLockoutExpires_RestoresReady) {
    bool lockedOut = true;
    ON_CALL(mockAccess, isLockedOut())
        .WillByDefault(Invoke([&lockedOut]() { return lockedOut; }));
    orch->tick();  // lockout shown

    lockedOut = false;
    EXPECT_CALL(mockUi, setIdle()).Times(1);
    EXPECT_CALL(mockUi, showLockout()).Times(0);
    orch->tick();  // expiry detected
    orch->tick();  // ready already shown, no repeat
}

// ===========================================================================
// ERROR state
// ===========================================================================

TEST_F(OrchestratorTest, ErrorCallsShowError) {
    setupOneCredential();
    tickCardDetected();
    ON_CALL(mockCrypto, generateChallenge(_, _)).WillByDefault(Return(false));
    orch->tick(); // DETECTING → ERROR
    EXPECT_CALL(mockUi, showError()).Times(1);
    orch->tick();
}

TEST_F(OrchestratorTest, ErrorCallsRestart) {
    setupOneCredential();
    tickCardDetected();
    ON_CALL(mockCrypto, generateChallenge(_, _)).WillByDefault(Return(false));
    orch->tick(); // DETECTING → ERROR
    EXPECT_CALL(mockSystem, restart()).Times(1);
    orch->tick();
}

// ===========================================================================
// Security-Invariant Regression Tests
//
// The four accept/deny invariants the paper claims, exercised through the REAL
// orchestrator decision logic (handleDetecting). Crypto is mocked here because
// the decision branch is what these guard; the *cryptographic* core of the
// forged-signature and rpId-hash invariants is proven against real mbedTLS in
// test_crypto_integration/. Each invariant covers the three mandated
// categories: valid input accepted, adversarial input rejected, and the
// relevant boundary.
//
// Helper: drive IDLE -> DETECTING with one credential whose stored counter is
// `lastCounter`, deliver `resp`, then run the DETECTING decision tick.
// ===========================================================================
class SecurityInvariantTest : public OrchestratorTest {
protected:
    // Run the decision tick with a given stored counter and assertion response.
    void runDecision(uint32_t lastCounter, const CTAP2Response& resp) {
        setupOneCredential();
        _fakeCred.lastCounter = lastCounter;
        tickCardDetected();                 // IDLE -> DETECTING
        ON_CALL(mockCtap2, getAssertion(_, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<3>(resp), Return(CTAP2_OK)));
    }
};

// --- Invariant 1: forged ECDSA signature -> access denied --------------------

TEST_F(SecurityInvariantTest, Signature_Valid_GrantsAccess) {
    CTAP2Response resp = _fakeResponse;     // counter 1 > lastCounter 0
    runDecision(0, resp);
    // verifyECDSA default ON_CALL returns true
    EXPECT_CALL(mockStore, updateCounter(_, _, 1)).Times(1);
    orch->tick();                           // DETECTING -> GRANTED
    EXPECT_CALL(mockAccess, grantAccess(_)).Times(1);
    orch->tick();                           // GRANTED handler
}

TEST_F(SecurityInvariantTest, Signature_Forged_DeniesAndDoesNotUpdateCounter) {
    CTAP2Response resp = _fakeResponse;
    runDecision(0, resp);
    ON_CALL(mockCrypto, verifyECDSA(_, _, _, _)).WillByDefault(Return(false));
    EXPECT_CALL(mockStore, updateCounter(_, _, _)).Times(0);
    orch->tick();                           // DETECTING -> DENIED
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    orch->tick();                           // DENIED handler
}

// --- Invariant 2: replayed counter -> clone alert + denied -------------------

TEST_F(SecurityInvariantTest, Counter_StrictlyGreater_Accepted) {
    CTAP2Response resp = _fakeResponse;
    resp.counter = 11;                      // lastCounter + 1 (boundary, accepted)
    runDecision(10, resp);
    EXPECT_CALL(mockUi, showCloneAlert()).Times(0);
    EXPECT_CALL(mockStore, updateCounter(_, _, 11)).Times(1);
    orch->tick();
}

TEST_F(SecurityInvariantTest, Counter_Equal_RejectedAsClone) {
    CTAP2Response resp = _fakeResponse;
    resp.counter = 10;                      // == lastCounter (boundary, rejected)
    runDecision(10, resp);
    EXPECT_CALL(mockUi, showCloneAlert()).Times(1);
    EXPECT_CALL(mockStore, updateCounter(_, _, _)).Times(0);
    orch->tick();
}

TEST_F(SecurityInvariantTest, Counter_Lower_RejectedAsClone) {
    CTAP2Response resp = _fakeResponse;
    resp.counter = 3;                       // < lastCounter (replayed/cloned)
    runDecision(10, resp);
    EXPECT_CALL(mockUi, showCloneAlert()).Times(1);
    EXPECT_CALL(mockStore, updateCounter(_, _, _)).Times(0);
    orch->tick();
}

// --- Invariant 3: wrong rpId hash -> denied before signature check -----------

TEST_F(SecurityInvariantTest, RpId_Spoofed_DeniedBeforeSignatureCheck) {
    CTAP2Response resp = _fakeResponse;
    memset(resp.rpIdHash, 0xAB, sizeof(resp.rpIdHash));  // != SHA-256 (mock: zeros)
    runDecision(0, resp);
    EXPECT_CALL(mockCrypto, verifyECDSA(_, _, _, _)).Times(0);
    EXPECT_CALL(mockStore, updateCounter(_, _, _)).Times(0);
    orch->tick();
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    orch->tick();
}

TEST_F(SecurityInvariantTest, RpId_SingleByteMismatch_Denied) {
    CTAP2Response resp = _fakeResponse;     // rpIdHash all-zero (matches mock)...
    resp.rpIdHash[31] = 0x01;               // ...except one byte (boundary)
    runDecision(0, resp);
    EXPECT_CALL(mockCrypto, verifyECDSA(_, _, _, _)).Times(0);
    orch->tick();
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    orch->tick();
}

// --- Invariant 4: expired challenge -> denied (relay-attack bound) ------------

TEST_F(SecurityInvariantTest, Challenge_AtTimeoutBoundary_Accepted) {
    CTAP2Response resp = _fakeResponse;
    setupOneCredential();
    tickCardDetected();
    // Elapsed == CHALLENGE_TIMEOUT_MS exactly: not yet expired (accepted).
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(DoAll(
            InvokeWithoutArgs([this]() {
                timeProvider.advance(CHALLENGE_TIMEOUT_MS);
            }),
            SetArgPointee<3>(resp), Return(CTAP2_OK)));
    orch->tick();                           // DETECTING -> GRANTED
    EXPECT_CALL(mockAccess, grantAccess(_)).Times(1);
    orch->tick();
}

TEST_F(SecurityInvariantTest, Challenge_JustOverTimeout_Denied) {
    CTAP2Response resp = _fakeResponse;
    setupOneCredential();
    tickCardDetected();
    // Elapsed == CHALLENGE_TIMEOUT_MS + 1: expired. Rejected before signature.
    ON_CALL(mockCtap2, getAssertion(_, _, _, _))
        .WillByDefault(DoAll(
            InvokeWithoutArgs([this]() {
                timeProvider.advance(CHALLENGE_TIMEOUT_MS + 1);
            }),
            SetArgPointee<3>(resp), Return(CTAP2_OK)));
    EXPECT_CALL(mockCrypto, verifyECDSA(_, _, _, _)).Times(0);
    orch->tick();                           // DETECTING -> DENIED
    EXPECT_CALL(mockAccess, denyAccess()).Times(1);
    orch->tick();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
