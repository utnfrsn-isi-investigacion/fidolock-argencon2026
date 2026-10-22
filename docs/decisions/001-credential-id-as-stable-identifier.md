---
title: Use FIDO2 Credential ID as stable identifier instead of NFC UID
status: Accepted
date: 2026-03-12
---

# 001 — Use FIDO2 Credential ID as stable identifier instead of NFC UID

**Status:** Accepted
**Date:** 2026-03-12

## Context

The initial implementation stored credentials in LittleFS keyed by a hash of the NFC UID returned by the PN532 reader during card detection (`InListPassiveTarget`). This worked correctly for generic Mifare cards, which have fixed UIDs.

The Yubico Security Key NFC, however, implements a privacy anti-tracking feature mandated by the NFC Forum: the ISO 14443 UID is **randomized on every tap**. Each presentation of the same security key produces a different 4-byte UID (e.g., `081F63ED`, `0838789F`, `08D00AE2` on three successive taps). The leading `08` byte signals a random UID per ISO 14443-3.

The consequence was that every tap generated a different filename (`hash(uid).json`), and the credential lookup always failed silently:

```
open(): /littlefs/credentials/398CD304.json does not exist
open(): /littlefs/credentials/0A9D5D20.json does not exist
open(): /littlefs/credentials/C2F750CD.json does not exist
```

The system would register a credential once but could never authenticate it.

## Options Considered

1. **Disable UID randomization** — Not possible; the security key does not expose a configuration interface for this.
2. **Use the ATS historical bytes as identifier** — the security key appends `"YubiKey"` in ASCII to its ATS, which is stable. However, this identifies the device *model*, not the individual credential, so it cannot distinguish between two keys.
3. **Use the FIDO2 credential ID** — The credential ID is assigned during `authenticatorMakeCredential` and is returned in every `authenticatorGetAssertion` response. It is stable, unique per credential, and part of the FIDO2 spec.

## Decision

Use the **FIDO2 credential ID** (64 bytes, hex-encoded) as the primary identifier for credential storage. Files are named `hash(credentialId).json` where the hash is FNV-1a over the raw credential ID bytes (see [ADR 004](004-fnv1a-hash-for-credential-filenames.md)).

Lookup is O(1): given a credential ID from the assertion response, compute its hash and open the file directly.

## Consequences

- **Eliminates the NFC UID randomization problem entirely** — the credential ID is retrieved from the CTAP2 layer, not the NFC layer.
- **Requires sending credential IDs in the CTAP2 `allowList`** — since we can no longer pre-select the credential by UID before calling `getAssertion`, we must send all registered IDs (see [ADR 003](003-allowlist-multi-credential-flow.md)).
- **Removes `uid` from `StoredCredential` struct** — the registration CLI no longer needs to read the NFC UID.
- **Incompatible with pre-existing credential files** — any credentials registered with the old UID-based scheme must be re-registered.
