---
title: Size PN532 transceive buffer to 254 bytes for FIDO2 responses
status: Accepted
date: 2026-03-12
---

# 002 — Size PN532 transceive buffer to 254 bytes for FIDO2 responses

**Status:** Accepted
**Date:** 2026-03-12

## Context

The `transceive()` method in `nfc_reader.cpp` wraps PN532 `InDataExchange` and allocates a local buffer for the raw frame response. The original implementation used a 64-byte buffer, matching the size used for simple APDU exchanges (e.g., SELECT, GetInfo).

FIDO2 `authenticatorGetAssertion` responses are substantially larger. The response CBOR contains:

| Field | Size |
|---|---|
| CTAP2 status byte | 1 B |
| authData (CTAP2 §6.1) | 37 B |
| DER-encoded ECDSA signature | 70–72 B |
| credential descriptor (key 0x01) | ~12 B |
| CBOR map overhead | ~5 B |
| **Total** | **~125–127 B** |

With a 64-byte buffer, `readFrame` copied only the first 64 bytes of the response, then read the byte at offset 64 as the DCS checksum. This byte was part of the payload, not the actual checksum, so the checksum verification always failed. The error surfaced as `CTAP2_ERR_TIMEOUT (0x05)` — misleading because the timing was well within the 2000ms NFC timeout.

The failure mode was confirmed by examining the timestamp delta in logs: the `getAssertion` call returned after ~875–1095ms, which is consistent with a completed NFC exchange, not a timeout.

## Options Considered

1. **256-byte buffer** — Covers all possible responses but `sizeof(uint8_t[256]) = 256` does not fit in a `uint8_t` length variable, causing a compiler `-Woverflow` warning (value wraps to 0).
2. **254-byte buffer** — The PN532 normal frame LEN field is a single `uint8_t`. The maximum data payload is **254 bytes** (255 − 1 byte for TFI). This is the physical ceiling and fits in `uint8_t` without overflow.
3. **Dynamic allocation** — Adds heap fragmentation risk on a constrained embedded system with no MMU.

## Decision

Allocate a **254-byte** stack buffer in `transceive()`. This matches the PN532 normal frame physical maximum and avoids both the overflow warning and heap allocation.

```cpp
uint8_t response[254];
uint8_t responseLen = sizeof(response);  // 254, fits in uint8_t
```

## Consequences

- **Eliminates the false CTAP2_ERR_TIMEOUT errors** — the full assertion response is now read correctly.
- **Fixes DCS checksum verification** — `readFrame` now reads the actual DCS byte at the correct offset.
- **Stack cost**: 254 bytes on the call stack during `transceive()`. Acceptable on ESP32 with 520KB SRAM.
- **Does not support extended PN532 frames** — those use a 3-byte LEN field and could carry larger payloads, but no standard FIDO2 response exceeds 254 bytes in practice.
