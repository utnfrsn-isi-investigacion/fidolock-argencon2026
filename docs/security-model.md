# Security Model — FidoLock

## 1. Overview

FidoLock implements a FIDO2-based physical access control system in which all cryptographic verification is performed by a self-contained ESP32 microcontroller with no network connectivity required. This document characterizes which security guarantees of the FIDO2/WebAuthn specification are preserved, which are transformed, and which are not applicable — establishing the formal security posture of the system.

For the architectural decision to operate the ESP32 as a self-contained Relying Party, see [ADR 005](decisions/005-serverless-relying-party-model.md). For the full threat catalog and STRIDE analysis, see [threat-model.md](threat-model.md).

---

## 2. The Standard FIDO2 Trust Model

In a standard WebAuthn deployment:

```
┌───────────┐   CTAP2/NFC   ┌────────────┐   HTTPS/TLS    ┌─────────────────────┐
│  Sec Key  │ ────────────► │   Client   │ ─────────────► │  Relying Party      │
│ (priv key │               │ (browser / │                │  (server)           │
│  in SE)   │               │  platform) │                │  - issues challenge │
└───────────┘               └────────────┘                │  - verifies attest  │
                                                          │  - stores pub key   │
                                                          │  - checks counter   │
                                                          │  - enforces revoc.  │
                                                          └─────────────────────┘
```

The Relying Party owns the trust anchor. It verifies that a legitimate, certified authenticator produced the credential (attestation), and it is the authoritative source for which credentials are currently valid. In practice this is a web application or identity provider — e.g., a Keycloak instance, an Okta/Azure AD tenant, or a custom WebAuthn server — running behind HTTPS with a verified domain that anchors the `rpId`.

---

## 3. The FidoLock Trust Model

FidoLock collapses the client and relying party into the ESP32:

```
┌───────────┐   CTAP2/NFC   ┌────────────────────────────────────────────────┐
│  Sec Key  │ ────────────► │  ESP32 (self-contained RP)                     │
│ (priv key │               │  - generates random challenge (TRNG)            │
│  in SE)   │               │  - sends CTAP2 authenticatorGetAssertion        │
└───────────┘               │  - verifies ECDSA/ES256 signature (mbedTLS)    │
                            │  - checks counter against LittleFS store        │
                            │  - activates relay on success                   │
                            └────────────────────────────────────────────────┘
                                          ▲
                                          │ enrollment (serial / future wireless)
                                   ┌──────────────┐
                                   │  key_manager │
                                   │  (admin CLI) │
                                   └──────────────┘
```

There is no server. No network connection is required for authentication. The ESP32 is the sole verifier.

---

## 4. FIDO2 Guarantee Analysis

### 4.1 Preserved guarantees

| FIDO2 Property | Mechanism in FidoLock | Assessment |
|---|---|---|
| **Private key never leaves hardware token** | Tamper-resistant hardware secure element in the Yubico Security Key NFC. Key is generated on-device via `authenticatorMakeCredential` and never exported. | ✅ Fully preserved |
| **Authentication proves possession of a specific hardware token** | ECDSA signature over a fresh challenge verifiable only with the private key bound to the enrolled credential. | ✅ Fully preserved |
| **Anti-replay** | 32-byte random challenge drawn per session from an mbedTLS CTR-DRBG seeded at boot inside a brief Bluetooth window (§6, §7). Each authentication attempt uses a fresh, single-use challenge that is never reaccepted once a response has been processed. `CHALLENGE_TIMEOUT_MS` (5 s) is enforced in `handleDetectingState()`: if the elapsed time since challenge generation exceeds the timeout, the response is rejected and access is denied. This bounds the window available to a relay attacker who proxies the live NFC session. | ⚠️ Preserved, with a seeding residual risk. The single-use guarantee is enforced, and the 5 s timeout is active as defence-in-depth against relay attacks. Neither stops pre-play if challenges become predictable: an adversary who degrades the boot-time DRBG seed and recovers its state could request assertions for future challenges in advance (plausible, not demonstrated; see §7). |
| **Anti-cloning (counter)** | `lastCounter` in `StoredCredential` is compared to the counter in every `CTAP2Response`. If `new_counter <= last_counter`, access is denied and a clone alert is raised for administrative review; the stored credential itself is not automatically disabled. | ✅ Preserved for access control; administrative revocation remains a manual process. |
| **User presence assertion** | CTAP2 `userPresent` flag is set by the authenticator for every assertion response. Over NFC, the security key sets this flag as soon as it enters the RF field — no button press is required (unlike USB/Lightning, which require touching the capacitive sensor). This is by FIDO2 NFC spec design: RF field proximity is the user presence gesture. The firmware now independently enforces `response.userPresent == true` in `handleDetectingState()` (CTAP2 spec §6.1 step 21): if the flag is absent the assertion is rejected and `denyAccess()` is called before any relay activation. | ✅ Fully enforced. A non-compliant or compromised authenticator that omits `userPresent` will be rejected. NFC presence remains weaker than a deliberate button press; a relay attack that wirelessly extends the NFC field could satisfy it without the user's knowledge (mitigated by single-use challenges and counter checks, see threat-model.md §5.2). |

### 4.2 Transformed guarantees

| FIDO2 Property | Standard behavior | FidoLock behavior | Impact |
|---|---|---|---|
| **Attestation verification** | RP validates the authenticator's attestation certificate chain against a manufacturer root CA, confirming the device is genuine and certified. | Skipped. Any public key enrolled through the admin channel is trusted. | The system cannot distinguish a genuine security key from a software authenticator enrolled by an attacker with admin access. **Physical security of the enrollment channel becomes the substitute control** (see §5). |
| **`rpId` / origin binding** | `rpId` is tied to a verified TLS origin; WebAuthn client enforces it to prevent phishing. | `rpId = "fidolock.local"` is a static firmware constant. No origin verification is performed. | No material impact: phishing is not a relevant threat model for physical access control (there is no web context, no user typing a URL). |
| **`rpId` hash verification in authData** | RP (server) computes SHA-256(rpId) and compares it byte-for-byte against authData[0..31] before accepting an assertion (CTAP2 spec §6.1 step 19). | Explicitly enforced in firmware: after receiving the assertion response, the firmware computes SHA-256("fidolock.local") and compares it against authData[0..31]. A mismatch causes immediate denial (see `handleDetectingState()` in `main.cpp`). | ✅ Fully preserved. A misconfigured or compromised authenticator that signed authData for a different rpId will be rejected. |
| **Credential privacy (`credProtect`)** | CTAP 2.1 lets the RP protect a discoverable credential with `credProtect` level 2 or 3. The authenticator then hides it from a reader that lacks user verification (or, at level 2, the credential ID). | Level 1 (`userVerificationOptional`) is enforced at registration, because the door has no PIN entry and sends no allowList ([ADR 011](decisions/011-discoverable-credentials-only.md)). Any reader that sends `getAssertion` for `fidolock.local` receives the credential ID, user handle and signature counter without a PIN. | Privacy, not authentication. A nearby reader can track the holder through the stable credential ID and handle; the handle is random and reveals nothing about the username. Nothing disclosed opens the door. Accepted. See [threat-model.md §5.6](threat-model.md). |
| **Credential revocation** | Immediate, server-side: delete the credential record. | Requires an authenticated command over the local serial channel or the future wireless admin channel. | Revocation has operational latency. A stolen security key remains valid until an admin physically connects to the device or wireless management is implemented. |

### 4.3 Properties not applicable

| FIDO2 Property | Why it does not apply |
|---|---|
| **Phishing resistance (origin binding)** | There is no web browser, no URL, and no way for an attacker to create a fake "fidolock.local" website to intercept an authentication. The threat simply does not exist in this deployment context. |
| **Cross-device credential portability** | FIDO2 passkeys are designed to sync across devices via platform authenticators. FidoLock uses roaming authenticators (security key) with device-local credential stores. Each FidoLock device is an independent RP; sync is an explicit design problem addressed by the wireless management work. |

---

## 5. Enrollment Trust Model

Enrollment is the highest-risk operation in this system because it determines which public keys the verifier will accept. The guarantee chain is:

```
Who enrolled it? → Was the enrollment channel authenticated? → Is the stored public key trustworthy?
```

### 5.1 Current model (serial)
Enrollment requires physical access to the device's UART and the `key_manager` CLI on an admin host. Enrolling a credential requires:
1. Physical access to the device enclosure (defeated by tamper detection on `PIN_TAMPER_SW`).
2. A USB-serial connection from the admin host to the ESP32.
3. **The security key plugged into the admin host over USB, with a FIDO2 PIN set.**
   - `key_manager` creates a discoverable credential (`rk: true`, user verification required, `credProtect = 1`, random user handle) and asks for the PIN, because the key only stores discoverable credentials behind it.
   - Without a PIN, registration stops with a hint to run `ykman fido access change-pin`.
   - If the key applied a `credProtect` level other than 1, the credential is rejected.
   - The door's NFC reader plays no part in enrollment.
4. `key_manager` sends the credential ID, public key and username to the door (`REG:`). The user handle is not sent; the door neither stores nor uses it.

**Scope of the PIN requirement:**
- The PIN is entered only at enrollment, on the admin host. The door has no PIN entry and never asks for it: authentication uses presence only (UV = 0).
- It protects the key holder: nobody can create a `fidolock.local` credential on their key without the PIN. It does not stop an attacker with enclosure access from enrolling a key of their own, whose PIN they know.
- It does not protect the credential once created. With `credProtect = 1`, any NFC reader obtains the credential ID and user handle without it (§4.2, [threat-model.md §5.6](threat-model.md)).
- Every `key_manager register` writes a backup file (default `tmp/<user>.json`) that holds the username, user handle and credential ID together. Treat it as sensitive: it links a name to the identifiers a skimmer reads.

This is equivalent to requiring two-factor physical presence: access to the device internals AND a security key unlocked with its PIN at enrollment.

### 5.2 Future model (wireless)
The wireless admin channel introduces a remote enrollment attack surface. The following controls are required before any wireless enrollment is accepted:

1. **Mutual authentication**: the admin client must prove identity to the device (e.g., pre-shared key stored in NVS, or ECDH + device certificate burned at manufacturing).
2. **Enrollment authorization token**: each `register` command must carry a signed, short-lived, single-use token. Tokens must be verified before any credential is written to LittleFS.
3. **Physical presence gate**: the device must be in an explicit enrollment mode, triggered by a local button press, before accepting any wireless enrollment command. This prevents silent remote enrollment even with a valid admin token.
4. **Anti-replay on enrollment messages**: enrollment requests must include a sequence number or timestamp checked against an NVS-stored high-water mark.

---

## 6. Trusted Computing Base

The minimal set of components whose correct operation is required for security guarantees to hold:

| Component | Role | Trust assumption |
|---|---|---|
| Yubico Security Key NFC secure element | Holds private key, performs signing | Tamper-resistant hardware; not modifiable under threat model |
| ESP32 TRNG + mbedTLS CTR-DRBG | Challenge generation | Hardware RNG, fed by the RF subsystem's high-speed ADC during a brief Bluetooth window at boot, seeds an SP 800-90A CTR-DRBG (approved algorithm; mbedTLS is not a CMVP-validated module); all challenges are drawn from the DRBG. Seeding residual risks: §7 |
| mbedTLS ECDSA verifier | Signature verification | Cryptographic library correctness |
| LittleFS credential store | Public key and counter persistence | Flash integrity; mitigated by ESP32 Flash Encryption (pending, see [threat-model.md §5.5](threat-model.md)) |
| Admin enrollment channel | Sole path to register new credentials | Physical security of device enclosure + `key_manager` authentication |

The TCB is intentionally small. The security key private key is never present on the ESP32 at any point. Compromise of the ESP32 (e.g., firmware extraction) exposes public keys and credential IDs — neither of which is usable to authenticate without physical possession of the corresponding security key.

---

## 7. Residual Risks and Open Items

| Risk | Severity | Status |
|---|---|---|
| Credentials stored in plaintext on internal flash | High | Pending — ESP32 Flash Encryption must be enabled in production (see [threat-model.md §5.5](threat-model.md)) |
| Challenge DRBG seed entropy (boot-time Bluetooth window) | Medium | Partially mitigated (interim) — The ESP32 hardware RNG is true-random only while the RF subsystem's high-speed ADC or the SAR ADC feeds it; otherwise its output is pseudo-random (ESP32 TRM v5.8 §18; ESP-IDF v4.4.7 *Random Number Generation*). `CryptoEngine::init()` keeps the Bluetooth controller on (`btStart()` → `RNG_RF_SETTLE_MS` settle → seed → `btStop()`) while the CTR-DRBG seed is drawn, and the radio is off again before any authentication traffic; all challenge bytes come from the DRBG (`mbedtls_ctr_drbg_random`), an approved NIST SP 800-90A construction. If the radio is already running (future wireless admin channel) its state is left untouched. Residual risks of the window: **(1)** with Wi-Fi on, the TRM warns the high-speed ADC can saturate in extreme cases, lowering entropy; a high-power 2.4 GHz transmitter could plausibly force that during the window, and an adversary who forces a reboot chooses when the window opens. **(2)** The failure is silent: the RNG exposes no raw noise samples to test, and later automatic reseeds run with the radio off (pseudo-random input), so a weak seed persists until the next reboot. **(3)** If `btStart()` fails, seeding proceeds from pseudo-random output with a logged warning (fail-open on entropy quality rather than bricking the lock). **(4)** Arduino-ESP32 2.0.17 enables Bluetooth modem sleep (ORIG mode), in which ESP-IDF v4.4.7 calls `esp_phy_disable()` when the idle controller sleeps, and ESP-IDF v5.5.5 notes that the high-speed ADC may then be unavailable; whether the RF stays on while the seed is drawn has not been measured. Planned fix: seed and reseed from the SAR-ADC source (`bootloader_random_enable()`) as a paced strong mbedTLS source; future work, not part of this artifact. Covered by `CryptoEngineChallenge.*` and `CryptoEngineRfWindow.*` tests in `test_crypto_integration`. |
| No FIPS 140-3 validated cryptographic module | Low | Accepted — the challenge CTR-DRBG, ECDSA-P256 and SHA-256 are NIST-approved algorithms (SP 800-90A, FIPS 186-5, FIPS 180-4), but the implementation is not a validated module: mbedTLS (ESP-IDF bundled) has no CMVP certificate and the ESP32 noise source has no SP 800-90B validation. The security argument rests on the algorithms, not on certification; a deployment that mandates FIPS 140-3 validation must substitute a validated module (e.g. a validated library or secure element). |
| Attestation not verified at enrollment | Medium | Accepted — mitigated by physical enrollment channel (see ADR 005) |
| Discoverable credentials readable without PIN (`credProtect = 1`) | Medium (privacy) | Accepted — the door has no PIN entry and sends no allowList ([ADR 011](decisions/011-discoverable-credentials-only.md)), so any nearby NFC reader can obtain the credential ID and user handle and track the key holder. Assumptions and mitigations in [threat-model.md §5.6](threat-model.md). |
| User handle derived from the username (`SHA-256(username)[:16]`, unsalted) | Medium (privacy) | Resolved — `generate_user_id()` in `key_manager` now returns 16 random bytes, so a skimmed handle no longer lets anyone confirm a guessed username or link the same user across installations. No firmware change was needed: the door ignores the handle. Credentials registered with an earlier `key_manager` keep the derived handle until re-registered. |
| No remote revocation path | Medium | Future work — wireless admin channel |
| Storage backend physical attack surface | Medium | Under analysis |
| Wireless enrollment rogue injection | High | Design required before implementation |
| `CHALLENGE_TIMEOUT_MS` enforcement | Low | Resolved — `CHALLENGE_TIMEOUT_MS = 5000` ms is checked in `FidoLockOrchestrator::handleDetecting()` immediately after `getAssertion()` returns. If `(millis() - challengeTimestamp) > CHALLENGE_TIMEOUT_MS`, the response is rejected and a timeout event is logged. Covered by the `ChallengeTimeout` cases in `test_ctap2` and the end-to-end `SecurityInvariantTest.Challenge_*` regression tests in `test_orchestrator`. |
| `userPresent` flag not independently verified by firmware | Medium | Resolved — `handleDetectingState()` now checks `response.userPresent` and calls `denyAccess()` if the flag is not set. |
| Counter-rollback does not auto-disable credential | Low | Accepted — on clone detection (`new_counter <= last_counter`), access is denied and an alert is logged, but the credential remains enabled. Manual administrative action is required to revoke it. Acceptable for current deployment context; auto-disable could be added as a configuration option in a future PR. |

---

## 8. Research Contribution Summary

FidoLock answers the following question: **what is the minimal trusted computing base needed to preserve FIDO2's core security guarantee — hardware-bound, replay-resistant, phishing-immune authentication — without a relying party server?**

The answer demonstrated by this system is: an ESP32, a TRNG, mbedTLS for ECDSA, and a physically-secured enrollment channel. The FIDO2 properties that depend on network infrastructure (attestation chain verification, remote revocation, cross-device sync) are either not applicable in the physical access control context, or are explicitly transformed into equivalent local mechanisms.

The key claim for publication is that **the core FIDO2 security guarantees — possession proof, anti-replay, anti-cloning — are preserved in an offline, embedded deployment**, with anti-replay subject to the boot-time seeding residual risk in §7, and that the properties which are lost are either irrelevant to this threat model or addressable with explicit design choices (wireless admin channel, Flash Encryption).
