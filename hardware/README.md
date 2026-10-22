# FidoLock Hardware

This directory contains hardware design files for the FidoLock access control system.

## Directory Structure

```
hardware/
├── kicad/              # KiCad schematic and PCB files
│   ├── fidolock.kicad_pro    # KiCad project file
│   └── fidolock.kicad_sch    # Main schematic
└── README.md           # This file
```

## Requirements

- **KiCad 7.0** or later (free, open-source EDA software)
  - Download: https://www.kicad.org/download/

## Opening the Project

1. Install KiCad 7.0 or later
2. Open KiCad
3. File → Open Project
4. Navigate to `hardware/kicad/fidolock.kicad_pro`
5. Double-click the schematic file to view/edit

## Schematic Overview

The schematic is organized into functional sections:

### Power Section
- 12V input from DC barrel jack
- 5V regulation for ESP32 and relay
- 3.3V from ESP32 for PN532 and logic

### NFC Reader (PN532)
- SPI interface to ESP32
- 3.3V power supply
- IRQ interrupt line

### Relay & Lock Control
- 2N2222A transistor driver
- 1N4007 flyback protection diode
- 5V relay coil
- Lock connected to relay contacts

### LED Indicators
- Green LED (GPIO27) - Success/Ready
- Red LED (GPIO14) - Error/Denied
- 330Ω current limiting resistors

### Buzzer Driver
- 2N2222A transistor driver
- Active 5V buzzer
- 1kΩ base resistor

### Tamper Detection
- Normally Closed (NC) microswitch
- 10kΩ pull-up resistor
- Connected to GPIO33

## Pin Assignments

| ESP32 GPIO | Function | Connected To |
|------------|----------|--------------|
| GPIO 4 | IRQ | PN532 IRQ |
| GPIO 5 | SPI SS | PN532 NSS |
| GPIO 14 | LED Red | Red LED (via 330Ω) |
| GPIO 18 | SPI SCK | PN532 SCK |
| GPIO 19 | SPI MISO | PN532 MISO |
| GPIO 23 | SPI MOSI | PN532 MOSI |
| GPIO 26 | Relay Ctrl | 2N2222A Base (via 1kΩ) |
| GPIO 27 | LED Green | Green LED (via 330Ω) |
| GPIO 32 | Buzzer | 2N2222A Base (via 1kΩ) |
| GPIO 33 | Tamper | Tamper Switch (with 10kΩ pullup) |

## Bill of Materials (BOM)

See [docs/hardware-setup.md](../docs/hardware-setup.md) for complete BOM.

### Key Components

| Ref | Value | Description |
|-----|-------|-------------|
| U1 | ESP32-WROOM-32 | Main microcontroller |
| U2 | PN532 | NFC reader module (SPI mode) |
| K1 | 5V Relay | SPDT relay for lock control |
| Q1, Q2 | 2N2222A | NPN transistors for driving |
| D1 | 1N4007 | Flyback protection diode |
| D2 | Green LED | 5mm status LED |
| D3 | Red LED | 5mm status LED |
| BZ1 | Buzzer | Active 5V piezo buzzer |
| R1, R2 | 330Ω | LED current limiting |
| R3, R4 | 1kΩ | Transistor base resistors |
| R5 | 10kΩ | Tamper switch pull-up |
| SW1 | NC Switch | Tamper detection switch |

## Future Plans

- [ ] PCB layout design
- [ ] Gerber files for manufacturing
- [ ] 3D printed enclosure design
- [ ] Fritzing files for beginners

## Contributing

When modifying hardware files:

1. Use KiCad 7.0+ for compatibility
2. Follow schematic conventions (power at top, ground at bottom)
3. Update BOM if components change
4. Test changes on breadboard before committing
5. Update pin assignments table if GPIOs change

## License

Hardware designs are released under the CERN Open Hardware Licence v2 - Permissive (CERN-OHL-P-2.0); see [LICENSE](LICENSE).
