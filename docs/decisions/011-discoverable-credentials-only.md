---
title: Use discoverable (resident) credentials only, with no allowList
status: Accepted
date: 2026-10-19
---

# 011 — Use discoverable (resident) credentials only, with no allowList

**Status:** Accepted
**Date:** 2026-10-19
**Supersedes:** [ADR 003](003-allowlist-multi-credential-flow.md)

## Context

ADR 003 sends every registered credential ID in the `getAssertion` allowList, because with non-resident credentials the credential ID is what lets the YubiKey re-derive its private key. The request grows by ~86 bytes per enrolled credential (64-byte ID), and the whole APDU must fit in a single PN532 `InDataExchange` frame (~255 bytes; `NFCReader::sendAPDU` also narrows the length to `uint8_t`). With 64-byte IDs only two credentials fit: from the third enrolment on, every authentication fails. The allowList also hands every enrolled credential ID to anyone who presents a reader or card to the door.

ADR 003 rejected omitting the allowList because the response "may not include key `0x01`". That only applies to a single-entry allowList: with no allowList, CTAP2 always returns the credential descriptor. What actually prevents omitting it is that a non-resident credential cannot be found without its ID.

## Options Considered

1. **ISO 7816-4 command chaining** — split the APDU across several frames. Keeps non-resident credentials, but the request still grows linearly, still leaks every ID, and the YubiKey caps the list at `maxCredentialCountInList`.
2. **Discoverable credentials first, allowList fallback for non-resident ones** — keeps old enrolments working, at the cost of two request shapes, a per-credential `resident` flag and a second round trip for legacy keys. The allowList size limit would still apply to the legacy path.
3. **Discoverable credentials only** — the door sends `{1: rpId, 2: clientDataHash}` and the key picks its own credential.

## Decision

**Option 3.**

- `ICTAP2Handler::getAssertion` takes no credential list; `buildGetAssertionCBOR` always encodes the two-entry map. `MAX_ALLOWLIST_CREDS` and the allowList buffers are removed.
- `FidoLockOrchestrator::handleDetecting` denies when the store is empty (`getCount() == 0`), otherwise sends one request. The response must carry key `0x01`; the single-entry "key omitted" fallback is gone.
- `parseAssertionResponse` skips unknown keys as complete CBOR items, since discoverable responses add `0x04` (user entity, a nested map) and `0x05` (numberOfCredentials).
- `key_manager register` always creates the credential with `rk: true`, user verification required at registration, and `credProtect = 1` (`userVerificationOptional`); it rejects the result if the key applied another level. Level 2 would hide the credential from a request without allowList and level 3 would require a PIN at the door.

## Consequences

- **Request size is constant** (62-byte APDU) regardless of how many users are enrolled; the PN532 frame limit no longer caps the user count.
- **The door no longer discloses credential IDs.**
- **The key does instead.** With `credProtect = 1` and no PIN at the door, any NFC reader that knows `fidolock.local` obtains the credential ID and user handle without a PIN, so key holders can be tracked. Accepted tradeoff; see [threat-model.md §5.6](../threat-model.md).
- **A FIDO2 PIN is required at registration**, because the YubiKey only stores discoverable credentials behind it. Authentication at the door still uses presence only (UV = 0).
- **Credentials consume key storage**: 25 slots (firmware < 5.7) or 100 (≥ 5.7), shared with every other site. They are managed with `ykman fido credentials list/delete`.
- **Breaking change for existing enrolments**: credentials registered before this change were created as non-resident and are no longer found by the door. Users must be deleted and registered again with the new `key_manager`.
- **A key holding several discoverable credentials for `fidolock.local`** (e.g. registered twice) signs with the most recent one and reports `numberOfCredentials`; only that credential needs to be enrolled at the door.
