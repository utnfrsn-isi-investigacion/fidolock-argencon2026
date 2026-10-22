#include "fidolock_orchestrator.h"
#include "../../config.h"
#include "../../utils/time_utils.h"
#include <string.h>

FidoLockOrchestrator::FidoLockOrchestrator(
        INFCReader& nfc, ICTAP2Handler& ctap2, ICryptoEngine& crypto,
        ICredentialStore& store, IAccessController& access,
        IUserInterface& ui, ITimeProvider& time,
        ISystemControl& system, IOutputStream& log)
    : _nfc(nfc), _ctap2(ctap2), _crypto(crypto), _store(store),
      _access(access), _ui(ui), _time(time), _system(system), _log(log) {}

void FidoLockOrchestrator::tick() {
    switch (_state) {
        case IDLE:        handleIdle();      break;
        case DETECTING:   handleDetecting(); break;
        case GRANTED:     handleGranted();   break;
        case DENIED:      handleDenied();    break;
        case ERROR_STATE: handleError();     break;
    }
    _ui.update();
}

// ---------------------------------------------------------------------------
void FidoLockOrchestrator::enterIdle() {
    // A locked-out node must look different from a ready one without anyone
    // having to tap a key to find out.
    _lockoutShown = _access.isLockedOut();
    if (_lockoutShown) {
        _ui.showLockout();
    } else {
        _ui.setIdle();
    }
    _state = IDLE;
}

// ---------------------------------------------------------------------------
void FidoLockOrchestrator::handleIdle() {
    // Lockout expires on a timer, usually while idle: restore the ready
    // indication when it does.
    if (_access.isLockedOut() != _lockoutShown) enterIdle();

    if (!_nfc.detectCard(_currentUID, &_currentUIDLength)) return;

    _log.printf("Card detected. UID: ");
    for (int i = 0; i < _currentUIDLength; i++) {
        _log.printf("%02X", _currentUID[i]);
    }
    _log.println();

#if PERFORMANCE_MONITOR
    _timingDetectEnd = _timingChallengeStart = _timingChallengeEnd = 0;
    _timingAuthStart = _timingAuthEnd = 0;
    _timingTotalStart = _timingDetectStart = _time.millis();
#endif

    _state = DETECTING;
    _ui.showProcessing();
}

// ---------------------------------------------------------------------------
void FidoLockOrchestrator::handleDetecting() {
    // YubiKey randomizes its NFC UID on every tap, so UID-based lookup is not
    // viable. Credentials are discoverable (resident): the key finds its own
    // credential for rpId and the response tells us which one it used.
    _assertionReceived = false;

    if (_store.getCount() == 0) {
        _log.println("No credentials registered");
        _state = DENIED;
        return;
    }

    if (!_crypto.generateChallenge(_pendingChallenge, sizeof(_pendingChallenge))) {
        _log.println("Error generating challenge");
        _state = ERROR_STATE;
        return;
    }
    _challengeTimestamp = _time.millis();

#if PERFORMANCE_MONITOR
    _timingDetectEnd = _time.millis();
    _timingChallengeStart = _time.millis();
#endif

    uint8_t clientDataHash[32];
    _crypto.sha256(_pendingChallenge, sizeof(_pendingChallenge), clientDataHash);

    CTAP2Response response;
    int result = _ctap2.getAssertion(&_nfc, FIDOLOCK_RP_ID, clientDataHash,
                                     &response);

    if (result != CTAP2_OK) {
        _log.printf("CTAP2 error: 0x%02X\n", result);
        _state = DENIED;
        return;
    }

    // The key returned an assertion. Every rejection from here on is a
    // verification failure and counts toward lockout. The exits above never
    // had a signature to check (empty store, key left the field, non-FIDO
    // tag, authenticator error), so there was nothing to guess.
    _assertionReceived = true;

    // Enforce challenge timeout — relay-attack defence-in-depth.
    if (TimeUtils::hasElapsed(_time.millis(), _challengeTimestamp, CHALLENGE_TIMEOUT_MS)) {
        _log.println("Challenge expired — relay timeout");
        _state = DENIED;
        return;
    }

    if (response.credentialIdLen == 0) {
        _log.println("No credential ID in assertion response");
        _state = DENIED;
        return;
    }

    StoredCredential* credential = _store.findByCredentialId(
        response.credentialId, response.credentialIdLen);
    if (credential == nullptr) {
        _log.println("Credential not found");
        _state = DENIED;
        return;
    }

    _log.printf("Credential found for: %s\n", credential->userName);

#if PERFORMANCE_MONITOR
    _timingChallengeEnd = _timingAuthStart = _time.millis();
#endif

    // Verify rpId hash (CTAP2 spec §6.1 step 19)
    uint8_t expectedRpIdHash[32];
    _crypto.sha256((const uint8_t*)FIDOLOCK_RP_ID,
                   strlen(FIDOLOCK_RP_ID), expectedRpIdHash);
    if (memcmp(response.rpIdHash, expectedRpIdHash, 32) != 0) {
        _log.println("rpId hash mismatch — assertion rejected");
        _state = DENIED;
        return;
    }

    // Verify user presence (CTAP2 spec §6.1 step 21)
    if (!response.userPresent) {
        _log.println("userPresent flag not set — assertion rejected");
        _state = DENIED;
        return;
    }

    // Verify signature
    uint8_t signedData[69];
    memcpy(signedData, response.authData, 37);
    memcpy(signedData + 37, clientDataHash, 32);

    uint8_t hash[32];
    _crypto.sha256(signedData, sizeof(signedData), hash);

    if (!_crypto.verifyECDSA(credential->publicKey, hash,
                              response.signature, response.signatureLen)) {
        _log.println("Invalid signature");
        _state = DENIED;
        return;
    }

    // Verify counter (anti-cloning, CTAP2 spec §6.1 step 22)
    if (response.counter <= credential->lastCounter) {
        _log.println("ALERT: Possible cloning detected!");
        _ui.showCloneAlert();
        _state = DENIED;
        return;
    }

    _store.updateCounter(credential->credentialId,
                         credential->credentialIdLen, response.counter);
    _log.printf("Access granted to: %s\n", credential->userName);

#if PERFORMANCE_MONITOR
    _timingAuthEnd = _time.millis();
    uint32_t detectTime    = _timingDetectEnd    - _timingDetectStart;
    uint32_t challengeTime = _timingChallengeEnd - _timingChallengeStart;
    uint32_t authTime      = _timingAuthEnd      - _timingAuthStart;
    uint32_t totalTime     = _timingAuthEnd      - _timingTotalStart;
    _log.println("\n[TIMING] ======= Performance Metrics =======");
    _log.printf("[TIMING] Detection:   %lu ms\n", detectTime);
    _log.printf("[TIMING] Challenge:   %lu ms\n", challengeTime);
    _log.printf("[TIMING] Auth:        %lu ms\n", authTime);
    _log.println("[TIMING] -------------------------");
    _log.printf("[TIMING] TOTAL:       %lu ms\n", totalTime);
    _log.println("[TIMING] ====================================\n");
    _timingTotalStart = 0;
#endif

    _state = GRANTED;
}

// ---------------------------------------------------------------------------
void FidoLockOrchestrator::handleGranted() {
    // The credential authenticated successfully, but if the node is in a
    // lockout window the relay will not open. Show the lockout indication
    // rather than flashing success on a door that stays shut. A legitimate
    // authentication must not extend the lockout, so no failed attempt is
    // recorded here.
    if (_access.isLockedOut()) {
        _log.println("Authenticated during lockout — access withheld");
        enterIdle();
        _system.delayMs(2000);
        return;
    }

    _ui.showSuccess();
    _access.grantAccess(ACCESS_DURATION_MS);
    _system.delayMs(ACCESS_DURATION_MS + 500);
    // Re-lock explicitly once the access window closes. The relay must never
    // be left energised: AccessController's timer-based auto-lock only fires
    // from update() in the main loop, which cannot run while this handler
    // blocks on delayMs(). Locking here guarantees the door secures on every
    // grant regardless of loop timing.
    _access.lock();
    enterIdle();
}

// ---------------------------------------------------------------------------
void FidoLockOrchestrator::handleDenied() {
#if PERFORMANCE_MONITOR
    if (_timingTotalStart != 0) {
        uint32_t now = _time.millis();
        _log.println("\n[TIMING] ======= Performance Metrics (DENIED) =======");
        if (_timingDetectEnd != 0)
            _log.printf("[TIMING] Detection:  %lu ms\n",
                        _timingDetectEnd - _timingDetectStart);
        if (_timingChallengeStart != 0) {
            uint32_t challengeEnd =
                (_timingAuthStart != 0) ? _timingChallengeEnd : now;
            _log.printf("[TIMING] Challenge:  %lu ms\n",
                        challengeEnd - _timingChallengeStart);
        }
        if (_timingAuthStart != 0)
            _log.printf("[TIMING] Auth:       %lu ms\n",
                        now - _timingAuthStart);
        _log.println("[TIMING] -------------------------");
        _log.printf("[TIMING] TOTAL:      %lu ms\n", now - _timingTotalStart);
        _log.println("[TIMING] ====================================\n");
        _timingTotalStart = 0;
    }
#endif
    _ui.showError();
    if (_assertionReceived) {
        _access.denyAccess();
    } else {
        // A flaky read must not lock out legitimate users. lock() keeps the
        // relay-off guarantee that denyAccess() gives.
        _log.println("No assertion verified — not counted as a failed attempt");
        _access.lock();
    }
    _system.delayMs(2000);
    // If this attempt tripped the lockout, enterIdle() shows it now.
    enterIdle();
}

// ---------------------------------------------------------------------------
void FidoLockOrchestrator::handleError() {
    _ui.showError();
    _log.println("System error. Restarting...");
    _system.delayMs(3000);
    _system.restart();
}
