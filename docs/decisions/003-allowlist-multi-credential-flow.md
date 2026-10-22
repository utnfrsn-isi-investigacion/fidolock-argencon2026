---
title: Send all registered credential IDs in CTAP2 allowList
status: Superseded
date: 2026-03-12
---

# 003 — Send all registered credential IDs in CTAP2 allowList

**Status:** Superseded by [ADR 011](011-discoverable-credentials-only.md): credentials are discoverable and no allowList is sent.
**Date:** 2026-03-12

## Context

The CTAP2 `authenticatorGetAssertion` command accepts an optional `allowList` parameter (map key `0x03`): an array of credential descriptors the relying party is willing to accept. The security key uses this list to determine which of its stored credentials to use for signing.

Because the security key randomizes its NFC UID per tap (see [ADR 001](001-credential-id-as-stable-identifier.md)), we cannot pre-select the credential at the NFC layer before initiating CTAP2. We therefore cannot send a single credential ID in the `allowList` — we do not yet know which credential the presenting security key holds.

## Options Considered

1. **Omit `allowList` entirely** — The security key will sign with any matching credential for the `rpId`. This works for a single registered key but is ambiguous with multiple keys: the response may not include key `0x01` (credential descriptor), so the verifier cannot determine which credential was used.
2. **Send all registered credential IDs** — The security key inspects the list, finds the one it holds, signs with it, and — when the list has more than one entry — returns the used credential descriptor in key `0x01` of the CBOR response per CTAP2 spec §6.1.
3. **Use a separate GetInfo + per-credential probe** — Sending one credential ID at a time until the security key responds successfully. This multiplies NFC round trips (each ~500ms) proportionally to the number of registered credentials.

## Decision

**Send all registered credential IDs** in the `allowList` on every `getAssertion` call.

The firmware collects all credential IDs from `credentialStore.listAll()` before initiating CTAP2, builds pointer arrays, and passes them to `ctap2Handler.getAssertion()`, which encodes them as a CBOR array under key `0x03`.

The CTAP2 spec §6.1 defines a special case: when the `allowList` contains exactly one entry and the security key matches it, key `0x01` may be **omitted** from the response. The firmware handles this explicitly:

```cpp
if (response.credentialIdLen == 0 && _numCollectedCreds == 1) {
    memcpy(response.credentialId, _collectedCredIds[0], _collectedCredIdLens[0]);
    response.credentialIdLen = _collectedCredIdLens[0];
}
```

## Consequences

- **Single NFC round trip** regardless of how many credentials are registered.
- **Correct credential identification** — the assertion response includes which credential was used, enabling O(1) credential lookup by ID.
- **CBOR buffer must be sized for multiple descriptors** — `buildGetAssertionCBOR` allocates 320 bytes to accommodate up to 10 credential IDs (each descriptor ~30 bytes CBOR-encoded).
- **Current limit: 10 credentials** — set by the `_collectedCredIds[10][64]` static array in `main.cpp`. Sufficient for a typical door access deployment; can be increased at the cost of stack space.
- **NFC user presence** — unlike USB CTAP2, NFC proximity automatically satisfies the user-presence check. The security key does not require a touch when authenticating over NFC.
