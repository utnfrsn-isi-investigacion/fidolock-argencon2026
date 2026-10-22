/**
 * FidoLock - Arduino Output Stream Adapter
 *
 * Production adapter that wraps Arduino's HardwareSerial (Serial) to
 * satisfy the IOutputStream driven port.
 *
 * This is a hardware adapter: it is only ever included from the composition
 * root (main.cpp) and is excluded from the native build by build_src_filter,
 * so it never reaches the host toolchain. Rule 5 of Ports & Adapters
 * (ADR 008): hardware adapters live here; the port interface lives in
 * ports/out/i_output_stream.h.
 */

#ifndef FIDOLOCK_ADAPTERS_HAL_ARDUINO_OUTPUT_STREAM_H
#define FIDOLOCK_ADAPTERS_HAL_ARDUINO_OUTPUT_STREAM_H

#include <Arduino.h>
#include "../../ports/out/i_output_stream.h"

class ArduinoOutputStream : public IOutputStream {
public:
    void print(const char* s) override    { Serial.print(s); }
    void println(const char* s) override  { Serial.println(s); }

protected:
    void vprintfImpl(const char* fmt, va_list args) override {
        char buf[256];
        vsnprintf(buf, sizeof(buf), fmt, args);
        Serial.print(buf);
    }
};

#endif // FIDOLOCK_ADAPTERS_HAL_ARDUINO_OUTPUT_STREAM_H
