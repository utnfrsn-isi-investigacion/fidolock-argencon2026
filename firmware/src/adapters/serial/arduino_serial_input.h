/**
 * FidoLock - Arduino Serial Input Adapter
 *
 * Production adapter that wraps Arduino's HardwareSerial (Serial) to satisfy
 * the ISerialInput driven port. Symmetric to ArduinoOutputStream.
 *
 * This is a hardware adapter: it is only ever included from the composition
 * root (main.cpp) and is excluded from the native build by build_src_filter,
 * so it never reaches the host toolchain. Ports & Adapters rule:
 * hardware adapters live here; the port interface lives in
 * ports/out/i_serial_input.h.
 */

#ifndef FIDOLOCK_ADAPTERS_SERIAL_ARDUINO_SERIAL_INPUT_H
#define FIDOLOCK_ADAPTERS_SERIAL_ARDUINO_SERIAL_INPUT_H

#include <Arduino.h>
#include "../../ports/out/i_serial_input.h"

class ArduinoSerialInput : public ISerialInput {
public:
    bool available() override { return Serial.available() > 0; }
    int  read()      override { return Serial.read(); }
};

#endif // FIDOLOCK_ADAPTERS_SERIAL_ARDUINO_SERIAL_INPUT_H
