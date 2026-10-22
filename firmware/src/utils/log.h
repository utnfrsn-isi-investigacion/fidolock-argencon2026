/**
 * FidoLock - Logging
 *
 * Single platform-dispatch point for the logging macros. On Arduino/ESP32
 * targets the `log_e/w/i/d` macros come from the Arduino-ESP32 core
 * (esp32-hal-log.h, level-gated at compile time by CORE_DEBUG_LEVEL in
 * platformio.ini: 0=None 1=Error 2=Warn 3=Info 4=Debug 5=Verbose). On the
 * native test build they fall back to plain stdout printf.
 *
 * This is the only portability conditional in production code: everything
 * else stays platform-agnostic behind the ports in ports/out/.
 */

#ifndef FIDOLOCK_UTILS_LOG_H
#define FIDOLOCK_UTILS_LOG_H

#include "../config.h"  // DEBUG_SERIAL flag for the DEBUG_PRINT* macros

#if defined(ARDUINO)
    #include <Arduino.h>  // provides log_e/w/i/d
#else
    #include <cstdio>
    #define log_e(fmt, ...) (void)printf("[E] " fmt "\n", ##__VA_ARGS__)
    #define log_w(fmt, ...) (void)printf("[W] " fmt "\n", ##__VA_ARGS__)
    #define log_i(fmt, ...) (void)printf("[I] " fmt "\n", ##__VA_ARGS__)
    #define log_d(fmt, ...) (void)printf("[D] " fmt "\n", ##__VA_ARGS__)
#endif

// Allow tests to override debug macros
#ifndef DEBUG_PRINT
    #if DEBUG_SERIAL && defined(ARDUINO)
        // Output format: [<ms>][<L>][<file>:<line>] <func>(): <message>
        #define DEBUG_PRINT(x)      log_i("%s", String(x).c_str())
        #define DEBUG_PRINTLN(x)    log_i("%s", String(x).c_str())
        #define DEBUG_PRINTF(fmt, ...) log_i(fmt, ##__VA_ARGS__)
    #else
        #define DEBUG_PRINT(x)
        #define DEBUG_PRINTLN(x)
        #define DEBUG_PRINTF(...)
    #endif
#endif

// Dedicated level-aware helpers — first argument is a tag constant or string literal.
// Define 'static const char* TAG = "MyModule";' at the top of each .cpp file.
// e.g.  LOG_E(TAG, "Failed: 0x%02X", code);
#define LOG_E(tag, fmt, ...) log_e("[%s] " fmt, tag, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) log_w("[%s] " fmt, tag, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) log_i("[%s] " fmt, tag, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) log_d("[%s] " fmt, tag, ##__VA_ARGS__)

#endif // FIDOLOCK_UTILS_LOG_H
