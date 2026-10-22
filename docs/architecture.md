# Technical Architecture - FidoLock

## Overview

FidoLock implements a physical access control system based on FIDO2 authentication via security key. The architecture is divided into functional layers that ensure modularity, security, and maintainability.

## Block Diagram

```
+------------------------------------------------------------------+
|                         APPLICATION LAYER                         |
|  +--------------------+  +--------------------+  +---------------+ |
|  | State Machine      |  | Session Manager    |  | Configuration | |
|  +--------------------+  +--------------------+  +---------------+ |
+------------------------------------------------------------------+
                                    |
+------------------------------------------------------------------+
|                         SERVICE LAYER                             |
|  +-------------+  +-------------+  +-------------+  +-----------+ |
|  | Auth        |  | Credential  |  | Access      |  | Logging   | |
|  | Service     |  | Manager     |  | Controller  |  | Service   | |
|  +-------------+  +-------------+  +-------------+  +-----------+ |
+------------------------------------------------------------------+
                                    |
+------------------------------------------------------------------+
|                         PROTOCOL LAYER                            |
|  +--------------------+  +--------------------+  +---------------+ |
|  | CTAP2 Handler      |  | CBOR Parser        |  | APDU Manager  | |
|  +--------------------+  +--------------------+  +---------------+ |
+------------------------------------------------------------------+
                                    |
+------------------------------------------------------------------+
|                         DRIVER LAYER                              |
|  +--------+  +--------+  +--------+  +--------+  +-------------+  |
|  | NFC    |  | GPIO   |  | Storage|  | Crypto |  | Timer/RTC   |  |
|  | PN532  |  | Relay  |  | LittleFS| | mbedTLS|  |             |  |
|  +--------+  +--------+  +--------+  +--------+  +-------------+  |
+------------------------------------------------------------------+
                                    |
+------------------------------------------------------------------+
|                         HARDWARE LAYER                            |
|  +--------+  +--------+  +--------+  +--------+  +-------------+  |
|  | PN532  |  | Relay  |  | Flash  |  | LEDs   |  | Buzzer      |  |
|  | Module |  | Driver |  | Memory |  | G/R    |  |             |  |
|  +--------+  +--------+  +--------+  +--------+  +-------------+  |
+------------------------------------------------------------------+
```

## Main Components

### 1. NFC Module (nfc/)

Responsible for communication with the PN532 reader and NFC card/key detection.

**Features:**
- PN532 reader initialization via SPI
- NFC card presence detection
- APDU command transmission/reception (254-byte response buffer — see [ADR 002](decisions/002-pn532-response-buffer-sizing.md))
- Timeout and retry handling

> Card UID is read by the PN532 during detection but is **not used** for credential lookup (the security key randomizes it per tap).

**Interface:**
```cpp
class NFCReader {
public:
    bool init();
    bool detectCard(uint8_t* uid, uint8_t* uidLength);
    bool transceive(uint8_t* sendBuf, uint8_t sendLen,
                    uint8_t* recvBuf, uint8_t* recvLen);
    void powerDown();
};
```

### 2. CTAP2 Module (ctap2/)

Implementation of the Client to Authenticator Protocol v2.

**Features:**
- CTAP2 command construction
- CBOR response parsing
- FIDO2 credential handling
- Assertion request generation

**Supported commands:**
- `authenticatorGetInfo` (0x04)
- `authenticatorGetAssertion` (0x02)
- `authenticatorMakeCredential` (0x01) - for registration

**Data structure:**
```cpp
struct CTAP2_Assertion {
    uint8_t credentialId[64];
    uint8_t credentialIdLen;
    uint8_t authData[37];
    uint8_t signature[72];
    uint8_t signatureLen;
    uint32_t counter;
    bool userPresent;
};
```

### 3. Cryptographic Module (crypto/)

Cryptographic functions for signature verification and challenge generation.

**Features:**
- Random challenge generation (32 bytes)
- SHA-256 data hashing
- ECDSA signature verification (ES256, P-256 curve)
- Public key parsing in COSE format

**Algorithms:**
- **Hash**: SHA-256
- **Signature**: ECDSA with NIST P-256 curve
- **COSE Key Type**: EC2 (-7 for ES256)

```cpp
class CryptoEngine {
public:
    bool generateChallenge(uint8_t* challenge, size_t len);
    bool sha256(const uint8_t* data, size_t len, uint8_t* hash);
    bool verifyECDSA(const uint8_t* publicKey,
                     const uint8_t* hash,
                     const uint8_t* signature, size_t sigLen);
};
```

### 4. Storage Module (storage/)

Credential and persistent configuration management using LittleFS.

Credentials are keyed by **FIDO2 credential ID**, not by NFC UID. Security key randomizes its NFC UID on every tap, making UID-based lookup non-viable. See [ADR 001](decisions/001-credential-id-as-stable-identifier.md) and [ADR 004](decisions/004-fnv1a-hash-for-credential-filenames.md).

**Stored data structure:**
```
/littlefs/
|-- credentials/
|   |-- <fnv1a(credentialId)>.json  # Credentials keyed by credential ID hash
```

**Filename derivation**: `FNV-1a 32-bit` hash over raw credential ID bytes → 8 uppercase hex characters. Provides O(1) lookup with no directory scan.

**Credential format (JSON on disk):**
```json
{
  "credential_id": "<hex>",
  "public_key": "<65-byte uncompressed P-256 point, hex>",
  "user_name": "alice",
  "last_counter": 42,
  "registration_time": 1710000000,
  "enabled": true
}
```

**In-memory struct:**
```cpp
struct StoredCredential {
    uint8_t credentialId[64];
    uint8_t credentialIdLen;
    uint8_t publicKey[65];     // Uncompressed P-256 point
    uint32_t lastCounter;
    uint32_t registrationTime;
    char userName[32];
    bool enabled;
};
```

### 5. Access Control Module (access/)

Lock hardware control and access logic.

**Features:**
- Lock activation/deactivation via relay (2N2222A transistor driver)
- Configurable unlock timer
- Failed attempt lockout
- Emergency mode

```cpp
class AccessController {
public:
    void grantAccess(uint32_t durationMs = 5000);
    void denyAccess();
    void emergencyUnlock();
    void setLockoutPolicy(uint8_t maxAttempts, uint32_t lockoutDuration);
};
```

### 6. User Interface Module (ui/)

Visual and audio feedback for the user.

**LED States:**
| State | Color | Pattern |
|-------|-------|---------|
| Ready | Green | Solid |
| Processing | Green | Blinking |
| Access granted | Green | 2 seconds |
| Access denied | Red | 3 blinks |
| Error | Red | Solid |
| Clone alert | Red | Fast blinking |

**Buzzer patterns:**
- Success: Short beep (100ms)
- Error: Long beep (500ms)
- Alert: 3 rapid beeps

## Data Flow

### Authentication Sequence

> NFC UID is **not used** for credential lookup — security key randomizes it on every tap. Credentials are discoverable (resident): the GetAssertion request carries no `allowList`, and the security key finds its own credential for `fidolock.local`. See [ADR 001](decisions/001-credential-id-as-stable-identifier.md) and [ADR 011](decisions/011-discoverable-credentials-only.md).

```
User             Sec Key         PN532           ESP32
   |                |              |                |
   |--[Present]---->|              |                |
   |                |--[NFC Field]->|                |
   |                |              |--[detectCard]->|
   |                |              |                | Generate challenge
   |                |              |<-[CTAP2 GetAssertion (rpId, hash)]-|
   |                |<-[APDU]------|                |
   |                | Find resident credential       |
   |                | Sign with private key          |
   |                |--[CTAP2 Response]->|           |
   |                |              |--------------->| Identify credential (key 0x01)
   |                |              |                | Verify ECDSA signature
   |                |              |                | Check counter (anti-clone)
   |                |              |                |---> Activate relay
   |<-[Green LED]---|--------------|----------------|
```

## Memory Considerations

### ESP32-WROOM-32
- Flash: 4MB (partitioned for OTA)
- SRAM: 520KB
- LittleFS: ~1.5MB for credentials

### Recommended Partitions
```
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x5000
otadata,  data, ota,     0xe000,  0x2000
app0,     app,  ota_0,   0x10000, 0x140000
app1,     app,  ota_1,   0x150000,0x140000
littlefs, data, spiffs,  0x290000,0x160000
```

## Communication Interfaces

### SPI (PN532)
- Clock: 1MHz
- Mode: SPI_MODE0
- Byte order: LSBFIRST

### GPIO
| Pin | Function | Mode |
|-----|----------|------|
| 18 | SPI SCK | Output |
| 19 | SPI MISO | Input |
| 23 | SPI MOSI | Output |
| 5 | SPI SS | Output |
| 4 | PN532 IRQ | Input |
| 26 | Relay Control | Output |
| 27 | LED Green | Output |
| 14 | LED Red | Output |
| 32 | Buzzer | Output PWM |
| 33 | Tamper Switch | Input Pullup |

## Security in Architecture

1. **Key isolation**: Private keys never leave the security key
2. **Layered validation**: Each layer validates its inputs
3. **Strict timeouts**: Prevent timing attacks
4. **Secure logging**: Event recording without sensitive data
5. **Watchdog**: Automatic restart on deadlocks
