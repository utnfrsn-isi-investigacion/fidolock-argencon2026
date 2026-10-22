# FidoLock: ARGENCON 2026 artifact

This repository holds the firmware, host tools, hardware schematic and latency dataset for the paper:

> A. Recalde, L. Torcello, E. Paez and D. P. Arce, "FidoLock: FIDO2/CTAP2-Based Physical Access Control with Hardware Security Keys on an Embedded Platform," in *Proc. IEEE ARGENCON 2026*.

FidoLock is a door controller built on an ESP32. It acts as a self-contained FIDO2 relying party for a Yubico Security Key NFC: it sends a fresh challenge over NFC (PN532, CTAP2), verifies the ES256 signature with mbedTLS, checks the signature counter, and drives a relay. No network connection is needed.

**This is a frozen snapshot, published so the results can be reproduced. It is not maintained and will not be updated.**

> [!WARNING]
> This is a research prototype and is not meant for production use. The challenge DRBG is seeded during a short Bluetooth window at boot. That window is a residual risk, described in the paper and in [docs/security-model.md §7](docs/security-model.md). CTR_DRBG is an approved NIST SP 800-90A algorithm, but the mbedTLS build used here is not a CMVP-validated module.

## Contents

| Path | What it holds |
|---|---|
| [`firmware/`](firmware/) | ESP32 firmware (PlatformIO, Arduino-ESP32) and its native unit tests (GoogleTest). |
| [`tools/key_manager/`](tools/key_manager/) | Python CLI that enrolls security keys on the device over serial. |
| [`hardware/`](hardware/) | KiCad schematic and pin assignments. |
| [`data/latency_measurements.csv`](data/latency_measurements.csv) | The N=40 cold-tap latency dataset reported in Appendix A of the paper. |
| [`docs/`](docs/) | Architecture, authentication flow, hardware setup, security and threat models, and design decisions (ADRs). |

## Hardware

- ESP32-WROOM-32 DevKit V1
- PN532 NFC module, wired over SPI
- Yubico Security Key NFC
- Relay driver, LEDs, buzzer and tamper switch

The full bill of materials, wiring table and assembly steps are in [docs/hardware-setup.md](docs/hardware-setup.md). The schematic is in [hardware/kicad/](hardware/kicad/) and needs KiCad 7 or later.

## Pinned toolchain

| Component | Version |
|---|---|
| PlatformIO Core (CI) | 6.1.19 |
| PlatformIO platform `espressif32` | 6.12.0 |
| Arduino-ESP32 | 2.0.17 (`framework-arduinoespressif32@3.20017.241212`) |
| ESP-IDF (bundled with Arduino-ESP32) | v4.4.7 |
| mbedTLS on the device (bundled with ESP-IDF) | 2.28.7 |
| Xtensa toolchain | `toolchain-xtensa-esp32@8.4.0+2021r2-patch5` |
| Adafruit PN532 / Adafruit BusIO / ArduinoJson | 1.3.4 / 1.17.4 / 7.4.2 |
| mbedTLS for the host unit tests | 3.6 LTS (CI builds 3.6.6 from source) |
| Python / python-fido2 (key_manager) | 3.11 / 1.1.3 |

All firmware versions are pinned in [`firmware/platformio.ini`](firmware/platformio.ini).

## Build, flash and test

```bash
cd firmware
make build            # compile for esp32dev
make flash-monitor    # upload and open the serial monitor
make uploadfs         # first flash only: write the LittleFS image
make test             # native unit tests on the host
```

The host tests need mbedTLS 3.x. On macOS, run `brew install mbedtls@3`. On Linux, build 3.6 LTS from source, as [`.github/workflows/ci.yml`](.github/workflows/ci.yml) does. Run `make help` to list all targets.

## Enroll a security key

```bash
cd tools/key_manager
make install && source .venv/bin/activate
key-manager register -u "Jane Doe"
```

The credential is resident (discoverable). The key asks for its FIDO2 PIN once, at enrollment; the door never asks for it. See [tools/key_manager/README.md](tools/key_manager/README.md) for the other commands.

## Reproduce the latency measurements

```bash
cd firmware
PERFORMANCE_MONITOR=1 make flash-monitor
```

With this flag, the firmware prints a `[TIMING]` block (detection, challenge, auth, total) after each tap. The paper's protocol is as follows:
- one enrolled credential;
- 40 cold taps, removing the key fully from the field between taps.

The resulting dataset is [data/latency_measurements.csv](data/latency_measurements.csv), and its header defines each phase.

## Licence

The firmware, tools, documentation and data are licensed under the [Apache License 2.0](LICENSE); see also [NOTICE](NOTICE). The hardware design files in [`hardware/`](hardware/) are licensed under [CERN-OHL-P-2.0](hardware/LICENSE).

Copyright 2026 Alejandro Recalde, Leandro Torcello, Enzo Paez and Débora P. Arce.

## Citation

If you use this artifact, please cite the paper. The citation metadata is in [CITATION.cff](CITATION.cff).

```bibtex
@inproceedings{recalde2026fidolock,
  author    = {Recalde, Alejandro and Torcello, Leandro and Paez, Enzo and Arce, D{\'e}bora P.},
  title     = {{FidoLock}: {FIDO2/CTAP2}-Based Physical Access Control with Hardware Security Keys on an Embedded Platform},
  booktitle = {Proc. IEEE ARGENCON},
  year      = {2026}
}
```

## Acknowledgment

This work was funded by the Universidad Tecnológica Nacional, Facultad Regional San Nicolás (UTN FRSN). The Departamento de Ingeniería en Sistemas de Información, UTN FRSN, provided the hardware and administrative support.
