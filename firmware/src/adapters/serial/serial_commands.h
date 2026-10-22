/**
 * FidoLock - Serial Command Handler
 *
 * Handles serial commands for credential management and configuration.
 *
 * Supported commands:
 *   REG:{json}     - Register a new credential
 *   LIST           - List all credentials
 *   DELETE:<uid>   - Delete credential by UID
 *   INFO           - Get device information
 *   CLEAR_ALL      - Clear all credentials
 *
 * @author UTN FRSN - Research Project
 * @version 0.1.0
 */

#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <stdint.h>
#include <stddef.h>
#include "../../config.h"
#include "../../ports/out/i_credential_store.h"
#include "../../ports/out/i_output_stream.h"
#include "../../ports/out/i_serial_input.h"
#include "../../ports/out/i_time_provider.h"
#include "../../ports/in/i_serial_commands.h"

// Command buffer size
#define CMD_BUFFER_SIZE 1024

// Command prefixes
#define CMD_REGISTER    "REG:"
#define CMD_LIST        "LIST"
#define CMD_DELETE      "DELETE:"
#define CMD_INFO        "INFO"
#define CMD_CLEAR_ALL   "CLEAR_ALL"

class SerialCommandHandler : public ISerialCommands {
public:
    /**
     * Initialize the command handler
     * @param store Pointer to credential store (driven port)
     * @return true if initialization successful
     */
    /**
     * @param store  Credential store (driven port).
     * @param out    Output stream for all serial responses (driven port).
     * @param in     Byte source for incoming commands (driven port).
     * @param time   Time provider, used for credential registration timestamps.
     */
    bool init(ICredentialStore* store, IOutputStream& out,
              ISerialInput& in, ITimeProvider& time);

    /**
     * Process incoming serial data
     * Call this in the main loop
     */
    void update() override;

    /**
     * Check if a command is currently being processed
     */
    bool isBusy() const override { return _busy; }

    /**
     * Process a complete command line (public for testing)
     */
    void processCommand(const char* cmd);

private:
    ICredentialStore* _store = nullptr;
    IOutputStream*    _out   = nullptr;
    ISerialInput*     _in    = nullptr;
    ITimeProvider*    _time  = nullptr;
    char _buffer[CMD_BUFFER_SIZE];
    size_t _bufferPos;
    bool _busy;

    /**
     * Handle REG: command (register credential)
     */
    void handleRegister(const char* jsonData);

    /**
     * Handle LIST command
     */
    void handleList();

    /**
     * Handle DELETE: command
     */
    void handleDelete(const char* uid);

    /**
     * Handle INFO command
     */
    void handleInfo();

    /**
     * Handle CLEAR_ALL command
     */
    void handleClearAll();

    /**
     * Parse hex string to bytes
     */
    bool hexToBytes(const char* hex, uint8_t* bytes, size_t maxLen, size_t* outLen);

    /**
     * Send response
     */
    void sendResponse(const char* status, const char* message = nullptr);
};

#endif // SERIAL_COMMANDS_H
