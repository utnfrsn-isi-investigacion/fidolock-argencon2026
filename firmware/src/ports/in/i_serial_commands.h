/**
 * FidoLock - Driving Port: Serial Commands
 *
 * Driving port (in): the API the application exposes for serial command
 * processing. Rule 3 of Ports & Adapters — Cockburn 2005.
 */

#ifndef FIDOLOCK_PORTS_IN_I_SERIAL_COMMANDS_H
#define FIDOLOCK_PORTS_IN_I_SERIAL_COMMANDS_H

class ISerialCommands {
public:
    virtual ~ISerialCommands() = default;

    /**
     * Process incoming serial data. Call this in the main loop.
     */
    virtual void update() = 0;

    /**
     * Check if a command is currently being processed.
     */
    virtual bool isBusy() const = 0;
};

#endif // FIDOLOCK_PORTS_IN_I_SERIAL_COMMANDS_H
