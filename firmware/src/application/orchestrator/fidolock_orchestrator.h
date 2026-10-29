/**
 * FidoLock - Authentication Orchestrator
 *
 * Application service that owns the NFC authentication state machine.
 * All dependencies are injected via ports; no adapter #includes here.
 *
 * State machine:
 *   IDLE → (card detected) → DETECTING → GRANTED / DENIED / ERROR
 *   GRANTED / DENIED → IDLE
 *   ERROR → (restart)
 *
 * Only a DENIED reached after the key returned an assertion records a failed
 * attempt toward lockout; transport and protocol failures do not.
 * While locked out, IDLE shows the lockout indication instead of "ready".
 */

#ifndef FIDOLOCK_ORCHESTRATOR_H
#define FIDOLOCK_ORCHESTRATOR_H

#include <stdint.h>
#include "../../config.h"
#include "../../ports/in/i_access_controller.h"
#include "../../ports/in/i_ctap2_handler.h"
#include "../../ports/out/i_nfc_reader.h"
#include "../../ports/out/i_crypto_engine.h"
#include "../../ports/out/i_credential_store.h"
#include "../../ports/out/i_user_interface.h"
#include "../../ports/out/i_time_provider.h"
#include "../../ports/out/i_system_control.h"
#include "../../ports/out/i_output_stream.h"
#include "../../domain/credential.h"
#include "../../domain/ctap2_types.h"

class FidoLockOrchestrator {
public:
    FidoLockOrchestrator(INFCReader&        nfc,
                         ICTAP2Handler&     ctap2,
                         ICryptoEngine&     crypto,
                         ICredentialStore&  store,
                         IAccessController& access,
                         IUserInterface&    ui,
                         ITimeProvider&     time,
                         ISystemControl&    system,
                         IOutputStream&     log);

    /**
     * Advance the state machine by one step.
     * Call once per main-loop iteration.
     */
    void tick();

private:
    enum State { IDLE, DETECTING, GRANTED, DENIED, ERROR_STATE };

    void enterIdle();
    void handleIdle();
    void handleDetecting();
    void handleGranted();
    void handleDenied();
    void handleError();

    INFCReader&        _nfc;
    ICTAP2Handler&     _ctap2;
    ICryptoEngine&     _crypto;
    ICredentialStore&  _store;
    IAccessController& _access;
    IUserInterface&    _ui;
    ITimeProvider&     _time;
    ISystemControl&    _system;
    IOutputStream&     _log;

    State    _state             = IDLE;
    uint8_t  _currentUID[10]    = {};
    uint8_t  _currentUIDLength  = 0;
    uint8_t  _pendingChallenge[32] = {};
    uint32_t _challengeTimestamp   = 0;
    bool     _assertionReceived    = false;  // decides if DENIED counts
    bool     _lockoutShown         = false;  // idle indication is lockout

#if PERFORMANCE_MONITOR
    uint32_t _timingDetectStart   = 0, _timingDetectEnd   = 0;
    uint32_t _timingChallengeStart = 0, _timingChallengeEnd = 0;
    uint32_t _timingAuthStart     = 0, _timingAuthEnd     = 0;
    uint32_t _timingTotalStart    = 0;
#endif
};

#endif // FIDOLOCK_ORCHESTRATOR_H
