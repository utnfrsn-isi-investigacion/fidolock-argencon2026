---
title: "Adopt Textbook Hexagonal (Ports & Adapters) Architecture"
status: Accepted
date: 2026-05-31
---

# ADR 008 — Adopt Textbook Hexagonal (Ports & Adapters) Architecture

## Status

Accepted

## Context

The FidoLock firmware had a partial hexagonal skeleton (`interfaces/`, `hal/`) but did not follow
textbook Ports & Adapters consistently (Cockburn, 2005). The symptom that forced action was the
test suite: three of five test suites had **zero coverage of production code** because tests
re-implemented production logic locally instead of exercising the real modules.

| Suite | State before this ADR |
|---|---|
| `test_access` | GOOD — tested real `AccessController` with hand-written HAL fakes |
| `test_crypto` | GOOD — tested real `CryptoUtils` pure functions |
| `test_ctap2` | BAD — re-implemented CBOR encoding locally; `CTAP2Handler` never instantiated |
| `test_serial` | BAD — re-implemented `parseCommandType` locally; `SerialCommandHandler` never called |
| `test_storage` | BAD — tested struct field assignment; `CredentialStore` never instantiated |

### Root Cause: Gap Analysis

| Gap | Before | After |
|---|---|---|
| Port naming & location | `src/interfaces/` (flat) | `src/ports/in/` + `src/ports/out/` |
| HAL port location | `src/hal/i_time_provider.h` | `src/ports/out/` |
| Domain layer | None — `StoredCredential` mixed with port types | `src/domain/` — zero non-standard includes |
| Driving port for `CTAP2Handler` | Missing | `src/ports/in/i_ctap2_handler.h` |
| Driving port for `SerialCommands` | Missing | `src/ports/in/i_serial_commands.h` |
| Adapter isolation | `nfc/`, `storage/`, `crypto/` in `src/` root | `src/adapters/` subtree |
| `CTAP2Handler` dependency | Took `NFCReader*` (concrete adapter) | Takes `INFCReader*` (port) |
| `test_ctap2`, `test_serial` | Re-implement production logic | Test real modules through ports |

## Decision

Migrate the firmware to textbook Ports & Adapters with the following five rules:

1. **The application core depends only on ports, never on adapters.**
   A port is a C++ pure-abstract interface. An adapter is any concrete class — production
   (`NFCReader`, `CredentialStore`) or test double (`MockNFCReader`, `TestTimeProvider`).

2. **No `#include` of an adapter inside a domain or application module.**
   `CTAP2Handler`, `AccessController`, `SerialCommandHandler` must include only `domain/`,
   `ports/`, and `utils/` headers. They must never include `nfc_reader.h`, `credential_store.h`,
   or `arduino_hal.h`.

3. **Ports are split into driving (in) and driven (out).**
   - Driving ports (`ports/in/`): the API the application exposes so an external actor (test, CLI,
     NFC event) can drive it.
   - Driven ports (`ports/out/`): the SPI the application calls out to infrastructure (storage,
     crypto, NFC, HAL).

4. **Wiring happens at the composition root only.**
   `main.cpp` is the composition root for production. Each test fixture `SetUp()` is the
   composition root for tests. No module constructs its own dependencies.

5. **Test doubles are adapters, not re-implementations of domain logic.**
   A mock/fake/stub implements a port interface and controls observable behaviour. It never
   duplicates the logic of the module under test.

### Target directory structure

```
firmware/src/
  domain/                     ← pure business objects; zero includes outside <stdint.h>/<stddef.h>
    credential.h              ← StoredCredential
    ctap2_types.h             ← CTAP2Response, CTAP2 status codes

  ports/
    in/                       ← driving ports (what external actors call into the app)
      i_access_controller.h
      i_ctap2_handler.h       (new)
      i_serial_commands.h     (new)
    out/                      ← driven ports (what the app calls out to infrastructure)
      i_credential_store.h
      i_crypto_engine.h
      i_nfc_reader.h
      i_time_provider.h
      i_gpio_controller.h

  adapters/                   ← all concrete implementations
    nfc/
    storage/
    crypto/
    hal/
    serial/
    ui/

  application/                ← application services
    access/
    ctap2/

  utils/                      ← pure portable helpers
  config.h
  main.cpp                    ← composition root

firmware/test/
  test_mocks/
    mock_interfaces.h         ← gmock adapters for driven ports
  test_hal/
    test_hal.h                ← hand-written fakes for HAL driven ports
  test_access/                ← real AccessController + TestTimeProvider/TestGPIOController
  test_ctap2/                 ← real CTAP2Handler + MockNFCReader
  test_crypto/                ← real crypto_utils (no mocks needed)
  test_serial/                ← real SerialCommandHandler + MockCredentialStore
  test_storage/               ← real CredentialStore pure logic
```

### Test double guideline

- `test_hal/test_hal.h`: Hand-written fakes for HAL driven ports. Use when the test needs to
  control time/GPIO state, not assert call sequences.
- `test_mocks/mock_interfaces.h`: gmock adapters for domain driven ports. Use when the test needs
  to assert what was called and with what arguments.

### Test framework

Migrate all test suites from Unity to **GoogleTest / GoogleMock** (`test_framework = googletest`
in `platformio.ini`). GoogleMock is required for `MOCK_METHOD` / `EXPECT_CALL` patterns.

## Consequences

- All five test suites now compile and exercise real production code.
- The CTAP2 and serial test suites gain meaningful coverage of the security-critical modules.
- All future application/domain modules must not include adapter headers (enforced by code review
  against Rule 2).
- The old `src/interfaces/` and `src/hal/` directories are removed entirely; `main.cpp` and every
  test suite were migrated to include from `ports/` in this change. No forwarding headers are kept,
  so any new code must include from `ports/`.
- `test_storage` retains only tests for Arduino-free pure logic (`hashCredentialId`, filename
  generation). LittleFS-dependent methods belong in device integration tests.

## References

- Cockburn, A. (2005). *Hexagonal Architecture*. <https://alistair.cockburn.us/hexagonal-architecture/>
- Supersedes the partial structure in `src/interfaces/` (no ADR existed)
- Related: [ADR 001](001-credential-id-as-stable-identifier.md), [ADR 004](004-fnv1a-hash-for-credential-filenames.md)
