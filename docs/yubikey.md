Security key randomizes its NFC UID on every tap (by design)

Security keys use "randomized UIDs" as a privacy anti-tracking feature. Each tap generates a new random UID at the NFC ISO 14443 layer:

- Tap 1: 081F63ED → hash 398CD304
- Tap 2: 0838789F → hash 0A9D5D20
- Tap 3: 08D00AE2 → hash C2F750CD

That's why we got this logs
```shell
[ 17220][I][nfc_reader.cpp:326] detectCard(): [NFC] detectCard: got 29 bytes in response
Card detected. UID: 081F63ED
[ 17229][I][user_interface.cpp:150] showProcessing(): [UI] State -> PROCESSING
[ 17254][E][vfs_api.cpp:105] open(): /littlefs/credentials/398CD304.json does not exist, no permits for creation
Credential not found
[ 17275][E][user_interface.cpp:180] showError(): [UI] State -> ERROR
[ 17281][I][access_controller.cpp:37] activateRelay(): [Access] Relay OFF (locked)
[ 17289][W][access_controller.cpp:101] recordFailedAttempt(): [Access] Failed attempt 1/5
[ 17297][W][access_controller.cpp:66] denyAccess(): [Access] Denied
[ 19303][I][user_interface.cpp:138] setIdle(): [UI] State -> IDLE
[ 22298][I][nfc_reader.cpp:326] detectCard(): [NFC] detectCard: got 29 bytes in response
Card detected. UID: 0838789F
[ 22307][I][user_interface.cpp:150] showProcessing(): [UI] State -> PROCESSING
[ 22331][E][vfs_api.cpp:105] open(): /littlefs/credentials/0A9D5D20.json does not exist, no permits for creation
Credential not found
[ 22352][E][user_interface.cpp:180] showError(): [UI] State -> ERROR
[ 22358][I][access_controller.cpp:37] activateRelay(): [Access] Relay OFF (locked)
[ 22366][W][access_controller.cpp:101] recordFailedAttempt(): [Access] Failed attempt 2/5
[ 22374][W][access_controller.cpp:66] denyAccess(): [Access] Denied
[ 24380][I][user_interface.cpp:138] setIdle(): [UI] State -> IDLE
[ 26063][I][nfc_reader.cpp:326] detectCard(): [NFC] detectCard: got 29 bytes in response
Card detected. UID: 08D00AE2
[ 26072][I][user_interface.cpp:150] showProcessing(): [UI] State -> PROCESSING
[ 26096][E][vfs_api.cpp:105] open(): /littlefs/credentials/C2F750CD.json does not exist, no permits for creation
Credential not found
[ 26116][E][user_interface.cpp:180] showError(): [UI] State -> ERROR
[ 26122][I][access_controller.cpp:37] activateRelay(): [Access] Relay OFF (locked)
[ 26130][W][access_controller.cpp:101] recordFailedAttempt(): [Access] Failed attempt 3/5
[ 26138][W][access_controller.cpp:66] denyAccess(): [Access] Denied
[ 28144][I][user_interface.cpp:138] setIdle(): [UI] State -> IDLE
```

Accordingly, he system was keying everything off the UID, so it generated a different filename every tap and never found the credential.

The correct stable identifier for a security key is the FIDO2 credential ID, which is returned during the CTAP2 authentication ceremony and doesn't change.

## Fix: credential-ID-based lookup

The solution is to store credentials keyed by a hash of the FIDO2 credential ID (not the NFC UID), and to send all registered credential IDs in the CTAP2 `allowList`. The security key picks the one it holds and the response identifies which was used (key `0x01` in the GetAssertion CBOR response, per CTAP2 spec §6.1 — omitted when the allowList has only one entry, in which case the single entry is used directly).

## Security Key ATS / NFC layer details

The Yubico Security Key NFC advertises itself as an ISO 14443-4 (T=CL) card:

- **SAK = 0x20** — bit 5 set signals ISO 14443-4 compliance; the PN532 will include ATS data after the UID in the InListPassiveTarget response.
- **ATS historical bytes end in `59 75 62 69 4B 65 79`** — ASCII for "YubiKey"; useful to identify the card type without FIDO2 communication. (Observed on the original test device; Yubico's NFC keys report this regardless of product name. Re-verify against the Security Key NFC if you rely on it.)
- **FSCI = 8** in the ATS T0 byte → maximum NFC frame size of 256 bytes for the card side.

## PN532 response buffer sizing

The PN532 `InDataExchange` response wraps the full card APDU reply. For FIDO2 GetAssertion this is ~120–200 bytes (CTAP2 status + CBOR authData 37 bytes + DER signature ~72 bytes + overhead). The PN532 normal frame LEN field is a single `uint8_t`, so the maximum data payload is **254 bytes** (255 − 1 for TFI).

The `transceive()` internal buffer must be at least 254 bytes. Using a 64-byte buffer (the original value) caused `readFrame` to clip the read, then compute the DCS against the wrong byte, failing the checksum check and returning "Error in transceive read" — misreported as `CTAP2_ERR_TIMEOUT (0x05)`.

## CTAP2 over NFC — user presence

Unlike USB CTAP2 (which requires a physical button press), NFC user presence is satisfied automatically by the physical proximity of the card. The security key does not blink or wait for a touch when authenticating over NFC.
