---
title: Operate the ESP32 as a self-contained FIDO2 Relying Party with no server
status: Accepted
date: 2026-03-20
---

# 005 — Operate the ESP32 as a self-contained FIDO2 Relying Party with no server

**Status:** Accepted
**Date:** 2026-03-20

## Context

The FIDO2/WebAuthn specification defines three parties:

1. **Authenticator** — the hardware token (security key), which holds the private key in a certified secure element and performs ECDSA signing.
2. **Client** — the browser or platform mediating the ceremony.
3. **Relying Party (RP)** — the server that owns the trust anchor: it generates registration challenges, verifies attestation certificate chains, stores public keys and credential IDs, checks the counter, and enforces revocation.

FidoLock has no server, no network, and no browser. The design question was: which component takes on the RP responsibility?

## Options Considered

1. **Require a backend server** — A registration server would handle enrollment and push credential updates to the device over WiFi/BLE. This preserves standards-compliance but introduces a network dependency, increases attack surface, and defeats the offline access guarantee.

2. **Trust-on-first-use (TOFU) with admin PIN** — The device accepts the first registration unconditionally, then requires a physical PIN or master credential for subsequent registrations. Still serverless, but weaker: TOFU leaves a window for rogue enrollment at first boot.

3. **ESP32 as self-contained RP** — The device itself generates challenges, verifies ECDSA signatures with stored public keys, checks counters, and persists credentials in LittleFS. The enrollment channel mechanism is a separate design problem; the authentication path requires no network regardless of how enrollment is delivered.

## Decision

The ESP32 acts as the sole Relying Party. The `rpId` is a static constant (`fidolock.local`) burned into firmware. Credential management is currently performed through the local admin channel (`key_manager` CLI over serial) as an interim implementation. The definitive enrollment channel — likely wireless (BLE or WiFi) with a physical presence gate — is under active design. The authentication path is independent of whatever enrollment transport is chosen.

This is a deliberate, security-motivated deviation from the standard WebAuthn deployment model. The tradeoffs are documented in full in [docs/security-model.md](../security-model.md).

## Consequences

### Properties preserved from standard FIDO2
- The security key private key never leaves the secure element — authentication still provides hardware-bound proof of possession.
- Each authentication uses a fresh random challenge (ESP32 TRNG), preventing replay attacks.
- The monotonically increasing counter detects cloned authenticators.

### Properties transformed
- **Attestation verification is skipped.** A standard RP verifies that the authenticator is a genuine certified device via a certificate chain rooted at the manufacturer (e.g., Yubico CA). FidoLock cannot perform this check — it trusts any public key enrolled through the admin channel. Physical presence at registration time partially compensates: an attacker must have both physical access to the device *and* a security key to register a rogue credential.
- **`rpId` origin binding is static.** WebAuthn ties the `rpId` to a verified TLS origin to prevent phishing. Here `rpId = "fidolock.local"` is a constant in `config.h` — origin validation is not meaningful in this context, but phishing is also not a relevant threat for a physical access control system.

### Properties lost vs. standard FIDO2
- **Remote revocation** — An RP revokes a credential by deleting it from its database. Here revocation requires an authenticated command over the local serial channel (or future wireless admin channel).
- **Multi-device credential consistency** — Each FidoLock device is an independent RP with its own credential store. Keeping two devices in sync is an operational problem to be addressed by the wireless management channel.

### New risks introduced
- **Admin channel is the sole enrollment trust anchor** — regardless of transport (serial today, wireless in future), unauthorized access to the enrollment channel allows rogue credential registration. The definitive mitigation design — mutual authentication, signed enrollment tokens, physical presence gate — is being specified separately. Current interim risk is bounded by requiring physical access to the device enclosure (tamper detection on `PIN_TAMPER_SW`).

## Related
- [docs/security-model.md](../security-model.md) — full analysis of FIDO2 guarantee preservation
- [ADR 001](001-credential-id-as-stable-identifier.md) — credential ID as stable identifier
- [ADR 003](003-allowlist-multi-credential-flow.md) — allowList-based multi-credential flow
