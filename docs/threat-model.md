# Threat Model and Security Analysis - FidoLock

## 1. Introduction

This document describes the threat model for the FidoLock system, identifying critical assets, attack surfaces, potential threats, and implemented mitigations.

## 2. Critical Assets

### 2.1 High Priority Assets

| Asset | Description | Impact if Compromised |
|-------|-------------|----------------------|
| FIDO2 private keys | Stored in security key | Identity spoofing |
| Public keys | Stored in ESP32 (LittleFS) | Authorization of invalid users |
| Registered credentials | Credential ID -> User mapping | Unauthorized access |
| Anti-cloning counter | Stored in ESP32 | Replay attacks |
| Physical lock | Control via relay | Unauthorized physical access |

### 2.2 Medium Priority Assets

| Asset | Description | Impact if Compromised |
|-------|-------------|----------------------|
| Access logs | Event records | Loss of audit trail |
| Key holder privacy | Link between a carried key, its holder's username and their movements | Tracking, targeted key theft |
| System configuration | Operational parameters | Security degradation |
| ESP32 firmware | Executable code | Full system control |

## 3. Attack Surfaces

### 3.1 NFC Interface

```
+----------+     +--------+     +-------+
| Attacker | --> | NFC    | --> | PN532 |
|          |     | Field  |     |       |
+----------+     +--------+     +-------+
```

**Vectors:**
- NFC communication interception (eavesdropping)
- Malicious command injection
- Relay attacks (man-in-the-middle)
- Denial of service (jamming)

### 3.2 Physical Interface

```
+----------+     +-------+     +-----------+
| Attacker | --> | ESP32 | --> | Lock      |
|          |     | GPIO  |     |           |
+----------+     +-------+     +-----------+
```

**Vectors:**
- Direct GPIO manipulation
- Relay bypass
- Power cut
- Voltage glitching

### 3.3 Storage

```
+----------+     +----------+     +-------------+
| Attacker | --> | ESP32    | --> | Credentials |
|          |     | Flash    |     |             |
+----------+     +----------+     +-------------+
```

**Vectors:**
- Firmware extraction via JTAG/UART
- Direct flash reading
- Credential modification

### 3.4 Security Key

```
+----------+     +---------+     +-------------+
| Attacker | --> | Sec Key | --> | Private Key |
|          |     | Physical|     |             |
+----------+     +---------+     +-------------+
```

**Vectors:**
- Physical security key theft
- Cloning (theoretically impossible)
- Side-channel attacks
- Skimming by a rogue NFC reader: `fidolock.local` credentials answer without a PIN (§5.6)

### 3.5 Boot-time RF Entropy Source

```
+----------+     +-----------+     +--------+     +-------------+
| Attacker | --> | 2.4 GHz   | --> | ESP32  | --> | DRBG seed   |
|          |     | RF field  |     | RF ADC |     | (once/boot) |
+----------+     +-----------+     +--------+     +-------------+
```

At boot, `CryptoEngine::init()` keeps the Bluetooth controller on while the
challenge CTR-DRBG draws its seed, so the RF subsystem's high-speed ADC feeds
the hardware RNG. Nothing reseeds the DRBG with fresh entropy until the next
reboot.

**Vectors** (plausible, not demonstrated):
- High-power 2.4 GHz interference during the seeding window, driving the
  high-speed ADC toward the saturation that the ESP32 TRM v5.8 §18 says lowers
  entropy
- Forced reboot (power cut, see §3.2) to choose when the window opens
- Bluetooth modem sleep (enabled in the Arduino-ESP32 2.0.17 build) may switch
  the RF off during the window without any attacker; not measured

## 4. Threat Analysis (STRIDE)

### 4.1 Spoofing

| ID | Threat | Probability | Impact | Risk |
|----|--------|-------------|--------|------|
| S1 | Clone security key | Very Low | Critical | Low |
| S2 | Fake NFC UID | Medium | High | Medium |
| S3 | Replay old signature | Medium | Critical | High |
| S4 | Relay spoofing | Low | Critical | Medium |
| S5 | Pre-play with predicted challenges after degrading the DRBG seed (§3.5) | Low (not demonstrated) | Critical | Medium |

**Mitigations:**
- S1: Security key uses a tamper-resistant hardware secure element (private key non-exportable)
- S2: NFC UID is not used for credential lookup (the security key randomizes it per tap — see [ADR 001](decisions/001-credential-id-as-stable-identifier.md)); authentication is gated entirely on ECDSA signature verification with the stored public key
- S3: Random challenge + timestamp + counter
- S4: Strict timeout on challenge-response
- S5: None beyond the DRBG itself; the single-use check, the timeout and the counter do not stop pre-play. Residual risk accepted for the prototype ([security-model.md §7](security-model.md)); planned fix (future work): seed from the SAR-ADC noise source instead of the radio

### 4.2 Tampering

| ID | Threat | Probability | Impact | Risk |
|----|--------|-------------|--------|------|
| T1 | Modify credentials in flash | Low | Critical | Medium |
| T2 | Modify firmware | Low | Critical | Medium |
| T3 | Directly manipulate GPIO | Medium | Critical | High |
| T4 | Physical lock bypass | Medium | Critical | High |

**Mitigations:**
- T1: Credential encryption at rest (AES-256)
- T2: Secure boot + firmware signing
- T3: Physical encapsulation, tamper detection
- T4: Outside scope of electronic system

### 4.3 Repudiation

| ID | Threat | Probability | Impact | Risk |
|----|--------|-------------|--------|------|
| R1 | Deny granted access | Medium | Medium | Medium |
| R2 | Delete access logs | Low | Medium | Low |

**Mitigations:**
- R1: Logging with timestamp and credential ID (user name)
- R2: Logs in separate partition, secure rotation

### 4.4 Information Disclosure

| ID | Threat | Probability | Impact | Risk |
|----|--------|-------------|--------|------|
| I1 | Intercept NFC communication | Medium | Low | Low |
| I2 | Extract public keys | Low | Low | Very Low |
| I3 | Read access logs | Low | Medium | Low |
| I4 | Skim credential ID and user handle from a carried key | Medium | Medium | Medium |
| I5 | Recover the username from a skimmed user handle | Low | Medium | Low |

**Mitigations:**
- I1: Challenge-response doesn't expose secrets
- I2: Public keys are not secrets
- I3: Physical access required, optional encryption
- I4: Accepted (§5.6): requires NFC range; `credProtect = 1` is required because the door has no PIN entry
- I5: Mitigated (§5.6): the handle is random; credentials registered with an earlier `key_manager` must be re-registered

### 4.5 Denial of Service

| ID | Threat | Probability | Impact | Risk |
|----|--------|-------------|--------|------|
| D1 | NFC signal jamming | Medium | High | High |
| D2 | Exhaust storage | Low | Medium | Low |
| D3 | Firmware crash | Low | High | Medium |
| D4 | Power cut | High | High | High |

**Mitigations:**
- D1: Interference detection, fallback mode
- D2: Log limits, automatic rotation
- D3: Watchdog, automatic restart
- D4: UPS/backup battery (recommended)

### 4.6 Elevation of Privilege

| ID | Threat | Probability | Impact | Risk |
|----|--------|-------------|--------|------|
| E1 | Register unauthorized credential | Low | Critical | Medium |
| E2 | Modify user permissions | Low | High | Medium |
| E3 | Access admin mode | Low | Critical | Medium |

**Mitigations:**
- E1: Registration requires prior authentication
- E2: Fixed permissions per credential
- E3: Admin mode requires master security key

## 5. Specific Attacks and Mitigations

### 5.1 Security Key Cloning Attack

**Description:** Attempt to duplicate an authorized security key.

**Analysis:**
```
Attacker -> Attempts to extract private key -> security key
                         |
                         v
               Secure Element (SE)
               - Key never leaves chip
               - Tamper-resistant hardware
               - Side-channel resistant
```

**Implemented mitigation:**
- Monotonically increasing counter
- If `new_counter <= last_counter`: CLONE ALERT

```cpp
if (response.counter <= credential->lastCounter) {
    logger.log("SECURITY_ALERT", "Possible cloning");
    ui.showCloneAlert();
    // Temporarily lock credential
    credential->enabled = false;
    return AUTH_DENIED;
}
```

### 5.2 Relay Attack (Man-in-the-Middle)

**Description:** Relay NFC communication between legitimate security key and reader.

**Diagram:**
```
Legitimate          Attacker           Reader
Sec Key             (relay)            FidoLock
   |                 |                  |
   |<-- NFC Field ---|<-- NFC Field ----|
   |--- Response --->|--- Response ---->|
```

**Implemented mitigation:**
- Strict challenge timeout (5 seconds)
- Detectable relay latency
- Unique challenge per session

```cpp
#define CHALLENGE_TIMEOUT_MS 5000

if (isChallengeExpired(millis(), challengeTimestamp, CHALLENGE_TIMEOUT_MS)) {
    logger.log("AUTH_FAIL", "Challenge expired");
    return AUTH_TIMEOUT;
}
```

### 5.3 Replay Attack

**Description:** Capture and reuse a valid authentication response.

**Implemented mitigation:**
- 32-byte random challenge drawn from an mbedTLS CTR-DRBG, seeded at boot
  while the Bluetooth controller is on so the RF subsystem's high-speed ADC
  feeds the hardware RNG (residual risks of this window:
  [security-model.md §7](security-model.md))
- Each challenge is unique
- Counter increments on each use

**Residual risk (S5):** replay resistance assumes unpredictable challenges. If
the boot-time seed is degraded (§3.5) and the DRBG state recovered, for
instance by testing candidates against challenges sniffed from the NFC link,
an adversary could predict future challenges and skim matching assertions
from a victim's key in advance. This is plausible but not demonstrated, and
its likelihood has not been measured. Planned mitigation: radio-free seeding
from the SAR-ADC source (future work).

```cpp
// CryptoEngine::init(): seed the CTR-DRBG inside a brief Bluetooth window
const bool rfWasOff = !_btRadio.isStarted();
if (rfWasOff && !_btRadio.start()) {
    LOG_W(TAG, "BT enable failed; seeding DRBG from pseudo-random RNG output");
}
if (_btRadio.isStarted()) {
    _system.delayMs(RNG_RF_SETTLE_MS);
}
int ret = mbedtls_ctr_drbg_seed(&_drbg, mbedtls_entropy_func, &_entropy,
                                (const unsigned char*)pers, sizeof(pers) - 1);
if (rfWasOff && _btRadio.isStarted()) {
    _btRadio.stop();
}

// CryptoEngine::generateChallenge(): every challenge comes from the DRBG
mbedtls_ctr_drbg_random(&_drbg, challenge, len);
```

### 5.4 Brute Force Attack

**Description:** Attempt multiple authentications to find valid credential.

**Implemented mitigation:**
- Lockout after N failed attempts
- Incremental delay between attempts
- Failed attempt logging

```cpp
void AccessController::recordFailedAttempt() {
    _failedAttempts++;

    if (_failedAttempts >= _maxAttempts) {
        _lockedOut = true;
        _lockoutStart = millis();
        logger.log("SECURITY", "Lockout activated");
    }

    // Incremental delay: 1s, 2s, 4s, 8s...
    delay(1000 << min(_failedAttempts, 4));
}
```

### 5.5 Firmware Extraction Attack

**Description:** Read firmware/data via JTAG, UART, or flash dump.

**Recommended mitigation:**
- Disable JTAG in production
- Enable flash encryption
- Enable secure boot

```ini
# platformio.ini - Security configuration
build_flags =
    -DCONFIG_SECURE_BOOT=y
    -DCONFIG_FLASH_ENCRYPTION_ENABLED=y
    -DCONFIG_SECURE_BOOT_ALLOW_JTAG=n
```

### 5.6 NFC Skimming and Credential Privacy

**Description:** A rogue NFC reader within range of a carried key, for example in a pocket or bag, sends `authenticatorGetAssertion` for `fidolock.local`. The rpId is public: it is a constant in this open-source firmware. Credentials are discoverable and created with `credProtect = 1` (`userVerificationOptional`). The key therefore answers without a PIN, a touch or an allowList, exactly as it answers the door ([ADR 011](decisions/011-discoverable-credentials-only.md)).

**What the reader obtains:**

| Data | Disclosed without PIN | Consequence |
|------|-----------------------|-------------|
| Credential ID | Yes, stable for the life of the credential | Links every read of the same key. Randomized NFC UIDs ([ADR 001](decisions/001-credential-id-as-stable-identifier.md)) do not prevent tracking. |
| User handle (`user.id`) | Yes: 16 random bytes from `generate_user_id()` in `key_manager` | Stable per credential, so it links reads just as the credential ID does, but it reveals nothing about the username. Credentials registered with an earlier `key_manager` keep `SHA-256(username)[:16]`, which a reader can match by hashing candidate names, until they are re-registered. |
| Signature counter (in authData) | Yes | Shows how much the key was used between two reads. |
| Signature over a reader-chosen hash | Yes | Useless at the door: every challenge is fresh CTR-DRBG output, so a pre-collected signature never matches (§5.3). |
| `name` / `displayName` | No: CTAP 2.1 §6.2.2 forbids returning them without user verification | The username is not disclosed. |
| Private key | No | Opening the door still requires the physical key. |

**Accepted assumptions:**
- **The door has no PIN entry, and none is planned.** Credentials must therefore be usable without user verification and without an allowList:
  - Level 3 requires a PIN at the door.
  - Level 2 hides the credential from a request without an allowList. An allowList does not fit the PN532 frame, and it would hand every enrolled credential ID to any device tapped on the door (ADR 011).
- **Skimming requires proximity.** NFC reads typically work within a few centimetres; extended-range antennas reach somewhat further, but the attacker still has to get close to the target.
- **Disclosure affects privacy, not authentication.** Nothing read this way opens the door without the key.
- **The deployment accepts that key holders are identifiable.** A nearby reader can recognise and track a key holder, and learn that they have access to a FidoLock door. This makes targeted key theft easier, and a stolen key opens the door without a PIN (§9).

**Mitigations:**

| Mitigation | Covers | Status |
|------------|--------|--------|
| Firmware skips the user entity (response key `0x04`) and never logs it | Door logs do not collect handles | Implemented |
| Random user handle in `key_manager` | Username recovery, linking the same user across installations. Not tracking by credential ID. | Implemented |
| RF-shielding sleeve or wallet for the key | All skimming | Operational, optional |
| `credProtect = 2` or `3` | All skimming | Not viable: needs an allowList or a PIN at the door |

The key matches existing credentials on rpId + `user.id`. With a random handle, re-registering the same user on the same key adds a credential instead of replacing the old one. The key signs with the newest; delete the old one with `ykman fido credentials delete`.

## 6. Security Controls Matrix

| Control | Mitigated Threat | Status | Priority |
|---------|------------------|--------|----------|
| Random challenge | Replay, Prediction | Implemented | Critical |
| Radio-free DRBG seeding (SAR-ADC source) | Seed entropy manipulation (S5) | Planned | Medium |
| Anti-cloning counter | security key cloning | Implemented | Critical |
| Challenge timeout | Relay attacks | Implemented | High |
| ECDSA verification | Spoofing | Implemented | Critical |
| Attempt lockout | Brute force | Implemented | High |
| Event logging | Repudiation, Audit | Implemented | Medium |
| Credential encryption | Data extraction | Pending | High |
| Secure boot | Malicious firmware | Pending | High |
| Flash encryption | Data extraction | Pending | High |
| Tamper detection | Physical manipulation | Pending | Medium |
| Random user handle | Username recovery from a skimmed handle | Implemented | Medium |

## 7. Attack Scenarios and Response

### Scenario 1: Security Key Theft

**Situation:** An attacker steals an authorized user's security key.

**Response:**
1. User reports loss
2. Administrator revokes credential in system
3. System rejects authentication from revoked credential (via `enabled = false` flag on the stored credential)

**Implementation:**
```cpp
struct StoredCredential {
    // ...
    bool enabled;        // false = revoked
    uint32_t revokedAt;  // revocation timestamp
};
```

### Scenario 2: Cloning Detection

**Situation:** System detects counter less than or equal to stored value.

**Response:**
1. Immediately deny access
2. Activate visual/audio alarm
3. Log security event
4. Notify administrator (if connected)
5. Temporarily lock credential

### Scenario 3: Multiple Failed Attempts

**Situation:** 5 rejected assertions without a grant in between (transport failures are not counted, see auth-flow.md).

**Response:**
1. Activate 5-minute lockout
2. Slow red blink in place of the ready light, two low beeps
3. Log security event
4. Reject all authentications during lockout

## 8. Deployment Recommendations

### 8.1 Physical Security

- [ ] Install ESP32 in metal enclosure with tamper switch
- [ ] Protect wiring from relay to lock
- [ ] Use security screws
- [ ] Place NFC reader in accessible but protected position

### 8.2 Production Configuration

- [ ] Enable secure boot
- [ ] Enable flash encryption
- [ ] Disable JTAG
- [ ] Change default credentials
- [ ] Configure remote logging (if applicable)

### 8.3 Operations

- [ ] New key registration procedure
- [ ] Key revocation procedure
- [ ] Periodic log review
- [ ] Credential backup (encrypted)
- [ ] Disaster recovery plan

## 9. Known Limitations

1. **No network connectivity**: No real-time notifications
2. **No biometrics**: Depends only on security key possession
3. **Physical vulnerability**: Mechanical lock bypass possible
4. **Incomplete MFA**: Only possession factor (security key); the door has no PIN entry
5. **Key holders are trackable**: any NFC reader within a few centimetres can read the `fidolock.local` credential ID and user handle without a PIN (§5.6)

## 10. References

- [FIDO Alliance Security Reference](https://fidoalliance.org/specs/fido-v2.0-ps-20190130/fido-security-ref-v2.0-ps-20190130.html)
- [OWASP IoT Security Guidelines](https://owasp.org/www-project-internet-of-things/)
- [ESP32 Security Features](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/security.html)
- [NIST SP 800-63B Digital Identity Guidelines](https://pages.nist.gov/800-63-3/sp800-63b.html)
