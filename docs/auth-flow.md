# Authentication Flow - FidoLock

## General Description

This document describes in detail the FIDO2 authentication flow implemented in FidoLock, from security key detection to access grant or denial.

> **Note on security key NFC UID randomization**: the Yubico Security Key NFC randomizes its ISO 14443 UID on every tap (privacy anti-tracking feature). The flow below does **not** use the NFC UID for credential lookup. See [ADR 001](decisions/001-credential-id-as-stable-identifier.md).

## Process Phases

### Phase 1: Detection

**Objective**: Detect the presence of an NFC card.

**Sequence**:
1. User presents security key to NFC reader
2. PN532 generates NFC field
3. ESP32 polls `detectCard()` in the main loop
4. PN532 returns card presence + NFC UID (informational only — not used for lookup)

**Reference code** (`main.cpp`):
```cpp
if (nfcReader.detectCard(currentUID, &currentUIDLength)) {
    currentState = STATE_DETECTING;
    ui.showProcessing();
}
```

**Timeouts**:
- Card detection polling: 10ms loop iteration
- NFC command timeout: 2000ms

---

### Phase 2: Challenge

**Objective**: Generate a cryptographic challenge and ask the security key to sign it.

Because the security key UID changes on every tap, credential selection cannot happen at the NFC layer. Credentials are discoverable (resident): they are stored on the security key, which finds its own credential for the `rpId`, so the request carries no `allowList`. See [ADR 011](decisions/011-discoverable-credentials-only.md).

**Sequence**:
1. If no credentials are registered (`credentialStore.getCount() == 0`): deny access immediately
2. Generate 32-byte random challenge via `cryptoEngine.generateChallenge()`
3. Compute `clientDataHash = SHA256(challenge)`
4. Build CTAP2 `authenticatorGetAssertion` command
5. Send to security key via PN532 `InDataExchange`

**CTAP2 GetAssertion command structure (CBOR)**:
```
{
  0x01: "fidolock.local",           // rpId
  0x02: <clientDataHash 32 bytes>   // SHA-256 of challenge
}
```

The APDU is 62 bytes regardless of how many users are enrolled.

**Security key internal processing**:
1. Look up a discoverable credential for `fidolock.local` (created with `credProtect = 1`, so no PIN is needed)
2. If none exists, return `CTAP2_ERR_NO_CREDENTIALS` (0x2E)
3. Sign `authData || clientDataHash` with that credential's private key
4. Increment the usage counter
5. Return CTAP2 response with credential descriptor (key `0x01`), authData, signature, user entity (key `0x04`) and, if it holds more than one credential for the rpId, `numberOfCredentials` (key `0x05`)

**NFC user presence**: Unlike USB CTAP2, NFC proximity automatically satisfies the user-presence requirement. The security key does not require a physical touch when authenticating over NFC.

---

### Phase 3: Credential Identification

**Objective**: Identify which registered credential the security key used.

Without an `allowList`, the CTAP2 assertion response always includes the credential descriptor (CBOR key `0x01`). A response without it is rejected:

```cpp
if (response.credentialIdLen == 0) {
    _state = DENIED;
    return;
}

StoredCredential* credential = _store.findByCredentialId(
    response.credentialId, response.credentialIdLen);
```

`findByCredentialId` performs an O(1) lookup: `FNV1a(credentialId)` → filename → file open. See [ADR 004](decisions/004-fnv1a-hash-for-credential-filenames.md).

---

### Phase 4: Cryptographic Verification

**Objective**: Validate the digital signature using the stored public key.

**Sequence**:
1. Load public key (65 bytes, uncompressed P-256 point) from the stored credential
2. Build signed data: `authenticatorData (37 B) || clientDataHash (32 B)`
3. Compute `hash = SHA-256(signedData)`
4. Verify ECDSA signature (ES256, NIST P-256)

**Algorithm**:
- Curve: NIST P-256
- Hash: SHA-256
- Signature format: DER-encoded (r || s), up to 72 bytes

```cpp
uint8_t signedData[69];
memcpy(signedData, response.authData, 37);
memcpy(signedData + 37, clientDataHash, 32);

uint8_t hash[32];
cryptoEngine.sha256(signedData, sizeof(signedData), hash);

bool valid = cryptoEngine.verifyECDSA(
    credential->publicKey, hash,
    response.signature, response.signatureLen);
```

---

### Phase 5: Counter Verification (Anti-Cloning)

**Objective**: Detect credential cloning via monotonic counter.

The CTAP2 authenticator increments a counter on every assertion. The stored `lastCounter` must be strictly less than the counter in the response. A non-increasing counter indicates the credential may have been cloned.

```cpp
if (response.counter <= credential->lastCounter) {
    Serial.println("ALERT: Possible cloning detected!");
    ui.showCloneAlert();
    currentState = STATE_DENIED;
    return;
}

credentialStore.updateCounter(credential->credentialId,
                              credential->credentialIdLen,
                              response.counter);
```

**Clone alert indicators**:
- Fast-blinking red LED
- Security log entry
- Access denied

---

### Phase 6: Access Grant

**Objective**: Activate the lock and provide feedback to the user.

**Sequence**:
1. `ui.showSuccess()` — green LED
2. `accessController.grantAccess(ACCESS_DURATION_MS)` — activates relay via 2N2222A transistor driver
3. Wait for access period to elapse
4. Lock re-engages, return to IDLE

---

### Failed Attempts and Lockout

A `DENIED` counts as a failed attempt only if the key returned an assertion that the firmware then rejected. The rejection reasons are an expired challenge, a missing or unknown credential ID, an rpId hash mismatch, `userPresent` not set, an invalid signature, or a counter regression. `MAX_FAILED_ATTEMPTS` (default 5) counted failures with no grant in between trigger a `LOCKOUT_DURATION_MS` (default 5 min) lockout.

Denials that happen before any assertion exists are not counted:
- empty credential store
- NFC transport failure (key pulled out of the field mid-exchange)
- SELECT failure (non-FIDO tag)
- authenticator status error such as `CTAP2_ERR_NO_CREDENTIALS`
- unparseable response

No signature was checked, so an attacker gains nothing from these attempts, and counting them would let flaky reads lock out legitimate users. These denials still force the relay off with `lock()`.

**Lockout indicators**:
- While locked out, a slow red blink (`LOCKOUT_BLINK_INTERVAL_MS`) replaces the dim-green ready light
- Two short low beeps when the lockout starts and on every tap during it
- A valid key tapped during lockout gets the lockout indication, not the error one, and the relay stays shut
- The dim-green ready light comes back when the lockout expires

---

## State Diagram

```
                    +-------+
                    | IDLE  |<------------------+
                    +---+---+                   |
                        |                       |
                   [Card detected]              |
                        |                       |
                        v                       |
              +--------------------+            |
              | DETECTING          |            |
              | (new challenge,    |            |
              |  CTAP2 GetAssertion|            |
              |  verify signature, |            |
              |  check counter)    |            |
              +---------+----------+            |
                        |                       |
           +------------+------------+          |
           |                         |          |
      [CTAP2 error /            [All checks     |
       sig invalid /             passed]        |
       counter bad /                |           |
       not found]                   v           |
           |                +---------------+   |
           v                | GRANTED       |   |
    +-------------+         +-------+-------+   |
    | DENIED      |                 |           |
    +------+------+          [Timeout]          |
           |                        |           |
           +------------------------+----------->
```

---

## Error Codes

| Code | Description | Action |
|------|-------------|--------|
| 0x00 | Success | — |
| 0x01 | No credentials registered | Deny |
| 0x02 | CTAP2 error from security key | Deny, log |
| 0x03 | Credential not found in store | Deny |
| 0x04 | Invalid ECDSA signature | Deny, log |
| 0x05 | Counter not strictly increasing (possible clone) | Alert, deny |
| 0x06 | NFC communication error | Deny, retry |

## Expected Response Times

| Operation | Typical Time | Timeout |
|-----------|--------------|---------|
| NFC card detection | 50–100ms | — (polled) |
| CTAP2 GetAssertion (NFC) | 400–900ms | 2000ms |
| Signature verification (mbedTLS) | 10–30ms | — |
| **Total authentication** | **~500–1000ms** | 3000ms |

> CTAP2 over NFC is slower than USB because each APDU exchange includes NFC framing overhead and the PN532 `InDataExchange` round trip. The 254-byte response buffer (see [ADR 002](decisions/002-pn532-response-buffer-sizing.md)) ensures the full response is received in a single exchange.
