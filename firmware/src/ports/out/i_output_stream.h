/**
 * FidoLock - Driven Port: Output Stream
 *
 * Driven port (out): abstracts character output so production code
 * (e.g. SerialCommandHandler) does not directly reference Arduino's
 * Serial object.  A hardware adapter wraps Serial; a capturing mock
 * collects output for assertions in unit tests.
 *
 * Rule 3 of Ports & Adapters (ADR 008).
 */

#ifndef FIDOLOCK_PORTS_OUT_I_OUTPUT_STREAM_H
#define FIDOLOCK_PORTS_OUT_I_OUTPUT_STREAM_H

#include <stdarg.h>

class IOutputStream {
public:
    virtual ~IOutputStream() = default;

    virtual void print(const char* s) = 0;
    virtual void println(const char* s = "") = 0;

    /**
     * Formatted print.  Implementations call vsnprintf internally.
     * Declared non-virtual so the variadic wrapper can delegate to the
     * single virtual hook vprintfImpl().
     */
    void printf(const char* fmt, ...) {  // NOLINT(cert-dcl50-cpp)
        va_list args;
        va_start(args, fmt);
        vprintfImpl(fmt, args);
        va_end(args);
    }

protected:
    virtual void vprintfImpl(const char* fmt, va_list args) = 0;
};

#endif // FIDOLOCK_PORTS_OUT_I_OUTPUT_STREAM_H
