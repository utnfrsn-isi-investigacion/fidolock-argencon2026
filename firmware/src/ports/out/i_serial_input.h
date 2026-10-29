/**
 * FidoLock - Driven Port: Serial Input
 *
 * Driven port (out): the byte source the serial command handler reads from.
 * Mirrors IOutputStream on the input side so the admin command channel can be
 * exercised on host without a hardware UART. Rule 3 of Ports & Adapters —
 * Cockburn 2005.
 */

#ifndef FIDOLOCK_PORTS_OUT_I_SERIAL_INPUT_H
#define FIDOLOCK_PORTS_OUT_I_SERIAL_INPUT_H

class ISerialInput {
public:
    virtual ~ISerialInput() = default;

    /** @return true if at least one byte is available to read. */
    virtual bool available() = 0;

    /** @return the next byte (0-255), or -1 if none is available. */
    virtual int read() = 0;
};

#endif // FIDOLOCK_PORTS_OUT_I_SERIAL_INPUT_H
