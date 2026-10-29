# Hardware Setup Guide - FidoLock

This guide provides detailed instructions for assembling the FidoLock physical access control system hardware.

## Table of Contents

1. [Bill of Materials](#bill-of-materials)
2. [Tools Required](#tools-required)
3. [Circuit Schematic](#circuit-schematic)
4. [Assembly Instructions](#assembly-instructions)
5. [Wiring Diagrams](#wiring-diagrams)
6. [Testing Procedures](#testing-procedures)
7. [Enclosure Recommendations](#enclosure-recommendations)
8. [Troubleshooting](#troubleshooting)

---

## Bill of Materials

### Main Components

| Qty | Component | Specification | Notes |
|-----|-----------|---------------|-------|
| 1 | ESP32-WROOM-32 | DevKit V1 or similar | 38-pin recommended |
| 1 | PN532 NFC Module | SPI/I2C/HSU compatible | Must support SPI mode |
| 1 | 5V Relay Module | Single channel, 10A | With optocoupler isolation |
| 1 | Electric Lock | 12V solenoid or magnetic | Fail-secure type recommended |
| 1 | Power Supply | 12V 2A DC | For lock and system |

### Semiconductors

| Qty | Component | Specification | Notes |
|-----|-----------|---------------|-------|
| 1 | 2N2222A | NPN Transistor | TO-92 package — buzzer driver only; relay module has built-in driver |
| 1 | 1N4007 | Rectifier Diode | Flyback protection for relay |
| 1 | Green LED | 5mm, 20mA | Status indicator |
| 1 | Red LED | 5mm, 20mA | Status indicator |
| 1 | Buzzer | Active, 5V | Piezoelectric type |

### Passive Components

| Qty | Component | Specification | Notes |
|-----|-----------|---------------|-------|
| 2 | Resistor | 330Ω, 1/4W | LED current limiting |
| 2 | Resistor | 1kΩ, 1/4W | Transistor base resistors |
| 1 | Resistor | 10kΩ, 1/4W | Tamper switch pull-up |
| 2 | Capacitor | 100µF, 16V | Power supply filtering (C1 input, C3 output) |
| 1 | Capacitor | 100nF | Decoupling |

### Connectors and Mechanical

| Qty | Component | Specification | Notes |
|-----|-----------|---------------|-------|
| 1 | Tamper Switch | Normally Closed (NC) | Microswitch preferred |
| 1 | DC Barrel Jack | 5.5x2.1mm | Power input |
| 1 | Screw Terminal | 2-position | Lock connection |
| - | Jumper Wires | Various lengths | For prototyping |
| 1 | Protoboard/PCB | 7x9cm minimum | For assembly |

### Optional Components

| Qty | Component | Specification | Notes |
|-----|-----------|---------------|-------|
| 1 | Voltage Regulator | LM7805 or AMS1117-5V | If using 12V for ESP32 |
| 1 | Heat Sink | For voltage regulator | If using LM7805 |
| 1 | USB-C Breakout | For programming | Alternative power input |

---

## Tools Required

### Essential Tools
- Soldering iron (temperature controlled, 350°C recommended)
- Solder (60/40 or lead-free)
- Wire strippers
- Multimeter
- Screwdrivers (Phillips and flathead)
- Flush cutters

### Recommended Tools
- Third-hand tool or PCB holder
- Desoldering pump or wick
- Heat shrink tubing and heat gun
- Label maker (for wire identification)
- Oscilloscope (for debugging)

---

## Circuit Schematic

### System Block Diagram

```
                                    +12V
                                      |
    +------------------+              |
    |   Power Supply   |──────────────┼──────────────────────┐
    |     12V 2A       |              |                      |
    +------------------+              |                      |
                                      |                      |
                              +-------+-------+              |
                              |               |              |
                           [7805]          [Relay]      [Lock]
                              |            Module           |
                              |               |              |
                            +5V              |              |
                              |               |              |
    +------------------+      |               |              |
    |     ESP32        |──────┴───────────────┘              |
    |   DevKit V1      |                                     |
    +--------+---------+                                     |
             |                                               |
    +--------+---------+                                     |
    |    PN532 NFC     |                                     |
    |     Module       |                                     |
    +------------------+                                     |
                                                             |
    [Green LED]──[330Ω]──GPIO27                              |
    [Red LED]────[330Ω]──GPIO14                              |
    [Buzzer]─────[2N2222A]──GPIO32                           |
    [Tamper SW]──[10kΩ]──GPIO33                              |
                                                             |
    [Relay Coil]─[2N2222A]──GPIO26 ───────────────────[Lock]─┘
```

### Detailed Schematic

```
                           VIN (12V)
                              │
                              │
                    ┌─────────┴─────────┐
                    │                   │
                  [C1]                  │
                 100µF             ┌────┴────┐
                    │              │  7805   │
                    │              │ or Buck │
                   GND             │Converter│
                                   └────┬────┘
                                        │
                                       5V ──────────┬──── ESP32 VIN
                                        │          │
                                      [C2]       [C3]
                                     100nF      100µF
                                        │          │
                                       GND        GND


═══════════════════════════════════════════════════════════════════════

                    PN532 NFC MODULE (SPI Mode)
                    ┌─────────────────────┐
                    │                     │
     ESP32 GPIO18 ──┤ SCK            VCC ├── 3.3V
     ESP32 GPIO19 ──┤ MISO           GND ├── GND
     ESP32 GPIO23 ──┤ MOSI               │
     ESP32 GPIO5  ──┤ SS (NSS)           │
     ESP32 GPIO4  ──┤ IRQ                │
                    │                     │
                    └─────────────────────┘

    Note: Set PN532 DIP switches to SPI mode:
          SW1: OFF, SW2: ON

═══════════════════════════════════════════════════════════════════════

                    RELAY DRIVER CIRCUIT
                                                    +12V
                          ┌─────────────────────────┤
                          │                         │
                          │    ┌──────────┐      [Lock]
                          │    │   Relay  │         │
                         [D1]  │   Coil   │      ───┴───
                        1N4007 │    5V    │      ───┬───
                          │    └────┬─────┘         │
                          │         │            COM (NC)
                          └────┬────┘               │
                               │                   GND
                               C
                              ─┴─
                        2N2222A├─┐
                              ─┬─ │
                               E  │
                               │  └──[1kΩ]── ESP32 GPIO26
                              GND

═══════════════════════════════════════════════════════════════════════

                    LED CIRCUITS

        +3.3V                              +3.3V
          │                                  │
        [330Ω]                             [330Ω]
          │                                  │
          ▼ Green LED                        ▼ Red LED
         ─┬─                                ─┬─
          │                                  │
     ESP32 GPIO27                       ESP32 GPIO14

    Note: LEDs are active HIGH (GPIO HIGH = LED ON)

═══════════════════════════════════════════════════════════════════════

                    BUZZER DRIVER CIRCUIT

                          +5V
                           │
                        [Buzzer]
                        (Active)
                           │
                           C
                          ─┴─
                    2N2222A├─┐
                          ─┬─ │
                           E  │
                           │  └──[1kΩ]── ESP32 GPIO32
                          GND

═══════════════════════════════════════════════════════════════════════

                    TAMPER SWITCH CIRCUIT

                          +3.3V
                            │
                          [10kΩ]
                            │
                            ├────── ESP32 GPIO33
                            │
                          [SW1]   ← Tamper Switch (NC)
                            │
                           GND

    Note: Switch OPEN = Tamper detected (GPIO reads HIGH)
          Switch CLOSED = Normal operation (GPIO reads LOW)
```

---

## Assembly Instructions

### Step 1: Prepare the PN532 Module

1. **Set SPI Mode**: Locate the DIP switches on the PN532 module
   - Switch 1 (SEL0): **OFF**
   - Switch 2 (SEL1): **ON**

2. **Solder Headers**: If not pre-installed, solder pin headers to the PN532

3. **Verify Pinout**: Identify these pins:
   - VCC (3.3V)
   - GND
   - SCK (SPI Clock)
   - MISO (Master In, Slave Out)
   - MOSI (Master Out, Slave In)
   - SS/NSS (Slave Select)
   - IRQ (Interrupt)

### Step 2: Prepare the ESP32

1. **Inspect the Board**: Check for any damaged pins or solder bridges

2. **Test Power**: Connect USB and verify the power LED illuminates

3. **Identify GPIO Pins**: Mark or note the following pins:
   - GPIO 4, 5, 14, 18, 19, 23, 26, 27, 32, 33

### Step 3: Build the Power Supply

**Option A: USB Power Only (for testing)**
- Power ESP32 via USB
- Use separate 12V supply for lock
- Ensure common ground between supplies

**Option B: Single 12V Supply (recommended for deployment)**

1. Connect 12V input to DC barrel jack
2. Wire 12V directly to relay module VCC
3. Add voltage regulator circuit:
   ```
   12V IN → [100µF cap] → [7805/Buck] → [100µF cap] → 5V OUT
                             ↓
                            GND
   ```
4. Connect 5V output to ESP32 VIN pin

### Step 4: Wire the PN532 NFC Module

| PN532 Pin | ESP32 Pin | Wire Color (suggested) |
|-----------|-----------|------------------------|
| VCC | 3.3V | Red |
| GND | GND | Black |
| SCK | GPIO 18 | Yellow |
| MISO | GPIO 19 | Blue |
| MOSI | GPIO 23 | Green |
| SS | GPIO 5 | Orange |
| IRQ | GPIO 4 | White |

**Important**: Use 3.3V for PN532 VCC, NOT 5V!

### Step 5: Build the Relay Driver Circuit

1. **Mount the transistor** (2N2222A):
   - Flat side facing you
   - Pins from left to right: Emitter, Base, Collector

2. **Connect components**:
   - Base → 1kΩ resistor → ESP32 GPIO 26
   - Emitter → GND
   - Collector → Relay coil negative

3. **Add flyback diode** (1N4007):
   - Cathode (stripe) → Relay coil positive (+5V)
   - Anode → Relay coil negative (Collector)

4. **Wire relay output**:
   - COM (Common) → Lock negative terminal
   - NC (Normally Closed) → GND (for fail-secure lock)
   - Lock positive → +12V

### Step 6: Wire the LEDs

**Green LED (Success indicator)**:
1. Anode (long leg) → 330Ω resistor → 3.3V
2. Cathode (short leg) → ESP32 GPIO 27

**Red LED (Error indicator)**:
1. Anode (long leg) → 330Ω resistor → 3.3V
2. Cathode (short leg) → ESP32 GPIO 14

*Note: The firmware uses active HIGH logic, so GPIO HIGH = LED ON*

### Step 7: Build the Buzzer Driver

1. **Mount transistor** (2N2222A) same as relay driver

2. **Connect**:
   - Base → 1kΩ resistor → ESP32 GPIO 32
   - Emitter → GND
   - Collector → Buzzer negative
   - Buzzer positive → +5V

### Step 8: Wire the Tamper Switch

1. Connect 10kΩ pull-up resistor between 3.3V and GPIO 33
2. Connect tamper switch between GPIO 33 and GND
3. Use Normally Closed (NC) switch configuration

---

## Wiring Diagrams

### Complete Wiring Table

| ESP32 Pin | Function | Connected To | Notes |
|-----------|----------|--------------|-------|
| 3.3V | Power | PN532 VCC, Pull-up resistors | Max 50mA from this pin |
| 5V/VIN | Power | Main power input | From regulator or USB |
| GND | Ground | All grounds | Common ground required |
| GPIO 4 | Input | PN532 IRQ | Interrupt signal |
| GPIO 5 | Output | PN532 SS | SPI Slave Select |
| GPIO 14 | Output | Red LED (via resistor) | Error indicator |
| GPIO 18 | Output | PN532 SCK | SPI Clock |
| GPIO 19 | Input | PN532 MISO | SPI Data In |
| GPIO 23 | Output | PN532 MOSI | SPI Data Out |
| GPIO 26 | Output | Relay transistor base | Lock control |
| GPIO 27 | Output | Green LED (via resistor) | Success indicator |
| GPIO 32 | Output | Buzzer transistor base | Audio feedback |
| GPIO 33 | Input | Tamper switch | With 10kΩ pull-up |

### Breadboard Layout (Top View)

```
    +─────────────────────────────────────────────────────────────+
    │  ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○  │ +5V
    │  ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○  │ GND
    ├─────────────────────────────────────────────────────────────┤
    │                                                             │
    │    ┌─────────────┐              ┌─────────────┐             │
    │    │   ESP32     │              │   PN532     │             │
    │    │   DevKit    │              │   NFC       │             │
    │    │             │              │   Module    │             │
    │    │         G18 ├──────────────┤ SCK         │             │
    │    │         G19 ├──────────────┤ MISO        │             │
    │    │         G23 ├──────────────┤ MOSI        │             │
    │    │          G5 ├──────────────┤ SS          │             │
    │    │          G4 ├──────────────┤ IRQ         │             │
    │    │        3.3V ├──────────────┤ VCC         │             │
    │    │         GND ├──────────────┤ GND         │             │
    │    │             │              └─────────────┘             │
    │    │         G26 ├───[1kΩ]───┬──[2N2222A]──→ Relay         │
    │    │         G27 ├───[330Ω]──┬──[LED Green]                │
    │    │         G14 ├───[330Ω]──┬──[LED Red]                  │
    │    │         G32 ├───[1kΩ]───┬──[2N2222A]──→ Buzzer        │
    │    │         G33 ├───[10kΩ]──┬──[Tamper SW]                │
    │    └─────────────┘           │                              │
    │                             GND                             │
    ├─────────────────────────────────────────────────────────────┤
    │  ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○  │ GND
    │  ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○  │ +12V
    +─────────────────────────────────────────────────────────────+
```

---

## Testing Procedures

### Pre-Power Checks

Before applying power, verify with a multimeter:

1. **Continuity Check**:
   - [ ] No short between VCC and GND
   - [ ] No short between 3.3V and GND
   - [ ] No short between 5V and GND
   - [ ] No short between 12V and GND

2. **Resistance Check**:
   - [ ] LED circuits: ~330Ω between GPIO and LED anode
   - [ ] Transistor base circuits: ~1kΩ between GPIO and transistor base
   - [ ] Pull-up resistor: ~10kΩ between 3.3V and GPIO 33

### Power-On Tests

1. **Initial Power Test**:
   ```
   - Connect USB to ESP32 (no external power yet)
   - Verify ESP32 power LED is ON
   - Check 3.3V rail with multimeter (should read 3.2-3.4V)
   ```

2. **PN532 Module Test**:
   ```
   - Measure PN532 VCC pin (should read 3.3V)
   - PN532 LED should blink during power-up
   ```

### Firmware Upload Test

1. Connect ESP32 via USB
2. Open PlatformIO terminal
3. Run: `pio run --target upload`
4. Monitor output: `pio device monitor`

Expected serial output:
```
=================================
  FidoLock v0.1.0
  FIDO2 Access Control
=================================

Initializing UI... OK
Initializing storage... OK
Initializing crypto... OK
Initializing NFC (PN532)... OK
Initializing access controller... OK

System ready. Waiting for security key...
```

### Component Tests

#### LED Test
```cpp
// Add to setup() temporarily for testing
void testLEDs() {
    pinMode(27, OUTPUT);  // Green
    pinMode(14, OUTPUT);  // Red

    digitalWrite(27, HIGH);  // Green ON
    delay(1000);
    digitalWrite(27, LOW);

    digitalWrite(14, HIGH);  // Red ON
    delay(1000);
    digitalWrite(14, LOW);
}
```

#### Buzzer Test
```cpp
void testBuzzer() {
    pinMode(32, OUTPUT);

    digitalWrite(32, HIGH);  // Buzzer ON
    delay(200);
    digitalWrite(32, LOW);
}
```

#### Relay Test
```cpp
void testRelay() {
    pinMode(26, OUTPUT);

    Serial.println("Relay ON");
    digitalWrite(26, HIGH);
    delay(2000);

    Serial.println("Relay OFF");
    digitalWrite(26, LOW);
}
```

#### NFC Reader Test
Present a security key or NFC card to the reader:
```
Expected output:
Card detected. UID: 04A1B2C3D4E5F6
```

#### Tamper Switch Test
```cpp
void testTamper() {
    pinMode(33, INPUT);

    Serial.print("Tamper state: ");
    Serial.println(digitalRead(33) ? "OPEN (Tamper!)" : "CLOSED (Normal)");
}
```

---

## Enclosure Recommendations

### Indoor Installation

**Recommended enclosure**: IP20 rated plastic box
- Minimum dimensions: 120mm x 80mm x 40mm
- Mounting: Wall-mounted near door frame
- Material: ABS plastic

**Component placement**:
```
┌────────────────────────────────────┐
│  [Power]    [ESP32]    [Relay]     │
│     ○          ○          ○        │
│                                    │
│  [PN532 behind front panel]        │
│         ┌──────────┐               │
│         │   NFC    │  [Green LED]  │
│         │  Reader  │  [Red LED]    │
│         └──────────┘               │
│                      [Buzzer hole] │
└────────────────────────────────────┘
```

### Outdoor Installation

**Recommended enclosure**: IP65 rated weatherproof box
- Minimum dimensions: 150mm x 100mm x 60mm
- Features:
  - Weatherproof cable glands
  - UV-resistant material
  - Drainage holes

**Additional considerations**:
- Use conformal coating on PCB
- Add silica gel packets for moisture control
- Mount NFC reader behind thin plastic (max 3mm)

### Security Considerations

1. **Tamper protection**:
   - Mount tamper switch to trigger when lid is opened
   - Consider adding a second switch for wall separation

2. **Cable routing**:
   - Run lock cables inside wall/conduit
   - Avoid exposed wiring

3. **Mounting**:
   - Use security screws (Torx or hex)
   - Mount on secure surface

---

## Troubleshooting

### Common Issues

| Symptom | Possible Cause | Solution |
|---------|---------------|----------|
| ESP32 not powering on | USB cable issue | Try different cable, check for data+power support |
| PN532 not detected | Wrong SPI mode | Check DIP switches: SW1=OFF, SW2=ON |
| PN532 not detected | Wiring error | Verify SCK, MISO, MOSI, SS connections |
| LEDs always on | Wrong polarity | Check LED orientation (anode to resistor) |
| LEDs not working | Open circuit | Verify resistor connections |
| Relay clicking but lock not working | Lock wiring | Check COM/NC connections, verify 12V |
| Relay not clicking | Transistor issue | Check 2N2222A orientation, base resistor |
| Buzzer silent | Driver circuit | Verify transistor wiring, check 5V supply |
| NFC not reading | Distance too far | Keep security key within 2cm of reader |
| Random resets | Power issue | Add more filtering capacitors, check supply |

### Debug Commands

Add these to your firmware for debugging:

```cpp
void printSystemInfo() {
    Serial.println("=== System Diagnostics ===");
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("CPU freq: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash size: %d bytes\n", ESP.getFlashChipSize());

    Serial.println("\n=== GPIO States ===");
    Serial.printf("GPIO 4  (IRQ):    %d\n", digitalRead(4));
    Serial.printf("GPIO 5  (SS):     %d\n", digitalRead(5));
    Serial.printf("GPIO 14 (Red):    %d\n", digitalRead(14));
    Serial.printf("GPIO 26 (Relay):  %d\n", digitalRead(26));
    Serial.printf("GPIO 27 (Green):  %d\n", digitalRead(27));
    Serial.printf("GPIO 32 (Buzzer): %d\n", digitalRead(32));
    Serial.printf("GPIO 33 (Tamper): %d\n", digitalRead(33));
}
```

### LED Error Codes

| Pattern | Meaning |
|---------|---------|
| Green solid | System ready |
| Green blinking | Processing authentication |
| Green 2 sec | Access granted |
| Red 3 blinks | Access denied |
| Red solid | System error |
| Red fast blink | Clone alert / Security warning |
| Both LEDs alternating | Initialization error |

---

## Safety Warnings

⚠️ **Electrical Safety**:
- Always disconnect power before making wiring changes
- Use appropriate wire gauges (18 AWG minimum for 12V lock circuit)
- Ensure proper insulation on all connections

⚠️ **Lock Safety**:
- Fail-secure locks remain locked during power failure
- Fail-safe locks unlock during power failure
- Choose appropriate type for your application
- Always provide manual override (physical key)

⚠️ **Fire Safety**:
- Use appropriate fuses or current limiting
- Don't exceed component ratings
- Ensure adequate ventilation

---

## Next Steps

After completing hardware assembly:

1. **Flash firmware**: See main [README.md](../README.md)
2. **Register security keys**: Use the registration tool
3. **Configure settings**: Edit `config.h` as needed
4. **Final testing**: Run full authentication tests
5. **Deployment**: Install in final location

