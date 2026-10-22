---
title: Use FNV-1a hash for credential filename derivation
status: Accepted
date: 2026-03-12
---

# 004 — Use FNV-1a hash for credential filename derivation

**Status:** Accepted
**Date:** 2026-03-12

## Context

Credentials are stored in LittleFS under `/credentials/<hash>.json`. The hash maps a credential ID (up to 64 bytes) to a fixed-length, filesystem-safe filename. The hash function used here is a key implementation choice for embedded storage.

## Options Considered

1. **SHA-256** — Cryptographically strong but 32-byte output → 64-character hex filename. LittleFS supports long filenames but the overhead of running mbedTLS SHA-256 for a non-security-critical filename derivation is wasteful. Also: SHA-256 collisions are computationally infeasible but the security property is not needed here.
2. **CRC-32** — Fast, common in embedded contexts. 4-byte output, low collision risk for small credential sets. Not designed as a hash function (poor avalanche for similar inputs).
3. **FNV-1a (32-bit)** — Non-cryptographic hash with excellent avalanche properties for short inputs, no dependencies, ~10 lines of C, 4-byte (8 hex character) output. Widely used for hash-based file indexing.
4. **Sequential numbering** — Simple integer counter. No collision risk. Requires a persistent counter in NVS and makes the filename meaningless for debugging.

## Decision

Use **FNV-1a 32-bit** hash over the raw credential ID bytes. Filenames are 8 uppercase hex characters: `<FNV1A32(credentialId)>.json`.

```cpp
uint32_t hashCredentialId(const uint8_t* credId, size_t len) {
    uint32_t hash = 2166136261u;  // FNV offset basis
    for (size_t i = 0; i < len; i++) {
        hash ^= credId[i];
        hash *= 16777619u;        // FNV prime
    }
    return hash;
}
```

## Consequences

- **O(1) lookup** — given a credential ID, derive its filename and open the file directly, no directory scan needed.
- **Filename is stable** — the same credential ID always maps to the same hash.
- **Collision risk is negligible** — with 2^32 possible values and a typical deployment of < 50 credentials, the birthday-bound collision probability is < 0.0000003%.
- **Not a security boundary** — the hash is used only for file naming. The credential ID itself is stored inside the JSON and verified after loading. A hash collision would cause the wrong credential file to be opened, which would then fail the credential ID comparison and return "not found". No security property relies on hash uniqueness.
- **8-character filenames** — easy to correlate with logs during debugging.
