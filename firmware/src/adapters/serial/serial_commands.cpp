/**
 * FidoLock - Serial Command Handler Implementation
 *
 * @author UTN FRSN - Research Project
 * @version 0.1.0
 */

#include "serial_commands.h"
#include <ArduinoJson.h>

bool SerialCommandHandler::init(ICredentialStore* store, IOutputStream& out,
                                ISerialInput& in, ITimeProvider& time) {
    _store = store;
    _out   = &out;
    _in    = &in;
    _time  = &time;
    _bufferPos = 0;
    _busy = false;
    memset(_buffer, 0, CMD_BUFFER_SIZE);

    _out->println("Serial command handler initialized");
    return true;
}

// Drains the injected byte source (ISerialInput) one line at a time. The
// hardware UART is one adapter behind that port (ArduinoSerialInput); a fake
// byte source drives the same code path under test.
void SerialCommandHandler::update() {
    while (_in && _in->available()) {
        int next = _in->read();
        if (next < 0) {
            break;
        }
        char c = (char)next;

        // End of command
        if (c == '\n' || c == '\r') {
            if (_bufferPos > 0) {
                _buffer[_bufferPos] = '\0';
                _busy = true;
                processCommand(_buffer);
                _busy = false;
                _bufferPos = 0;
                memset(_buffer, 0, CMD_BUFFER_SIZE);
            }
        }
        // Add to buffer
        else if (_bufferPos < CMD_BUFFER_SIZE - 1) {
            _buffer[_bufferPos++] = c;
        }
    }
}

void SerialCommandHandler::processCommand(const char* cmd) {
    // Skip empty commands
    if (strlen(cmd) == 0) return;

    // REG: command
    if (strncmp(cmd, CMD_REGISTER, strlen(CMD_REGISTER)) == 0) {
        handleRegister(cmd + strlen(CMD_REGISTER));
    }
    // LIST command
    else if (strcmp(cmd, CMD_LIST) == 0) {
        handleList();
    }
    // DELETE: command
    else if (strncmp(cmd, CMD_DELETE, strlen(CMD_DELETE)) == 0) {
        handleDelete(cmd + strlen(CMD_DELETE));
    }
    // INFO command
    else if (strcmp(cmd, CMD_INFO) == 0) {
        handleInfo();
    }
    // CLEAR_ALL command
    else if (strcmp(cmd, CMD_CLEAR_ALL) == 0) {
        handleClearAll();
    }
    // Unknown command
    else {
        sendResponse("ERROR", "Unknown command");
    }
}

void SerialCommandHandler::handleRegister(const char* jsonData) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error) {
        sendResponse("ERROR", "Invalid JSON");
        return;
    }

    // Extract fields
    const char* credIdHex = doc["credential_id"];
    const char* pubKeyHex = doc["public_key"];
    const char* userName = doc["user_name"];

    if (!credIdHex || !pubKeyHex || !userName) {
        sendResponse("ERROR", "Missing required fields");
        return;
    }

    // Create credential structure
    StoredCredential credential;
    memset(&credential, 0, sizeof(StoredCredential));

    // Parse credential ID
    size_t credIdLen;
    if (!hexToBytes(credIdHex, credential.credentialId, sizeof(credential.credentialId), &credIdLen)) {
        sendResponse("ERROR", "Invalid credential ID format");
        return;
    }
    credential.credentialIdLen = credIdLen;

    // Parse public key
    size_t pubKeyLen;
    uint8_t pubKeyTemp[65];
    if (!hexToBytes(pubKeyHex, pubKeyTemp, sizeof(pubKeyTemp), &pubKeyLen)) {
        sendResponse("ERROR", "Invalid public key format");
        return;
    }
    if (pubKeyLen != 65) {
        sendResponse("ERROR", "Public key must be 65 bytes");
        return;
    }
    memcpy(credential.publicKey, pubKeyTemp, 65);

    // Copy user name
    strncpy(credential.userName, userName, sizeof(credential.userName) - 1);

    // Set defaults
    credential.lastCounter = 0;
    credential.registrationTime = (_time ? _time->millis() : 0) / 1000;
    credential.enabled = true;

    // Save credential
    if (_store->save(&credential)) {
        _out->printf("Registered: %s\n", userName);
        sendResponse("OK", "Credential registered");
    } else {
        sendResponse("ERROR", "Failed to save credential");
    }
}

void SerialCommandHandler::handleList() {
    int listCount = 0;
    _store->listAll([this, &listCount](StoredCredential* cred) {
        JsonDocument doc;
        char credIdHex[129] = {};
        for (uint8_t i = 0; i < cred->credentialIdLen && i < 64; i++) {
            snprintf(credIdHex + i * 2, 3, "%02X", cred->credentialId[i]);
        }
        doc["credential_id"] = credIdHex;
        doc["user_name"]      = cred->userName;
        doc["last_counter"]   = cred->lastCounter;
        doc["enabled"]        = cred->enabled;
        doc["registration_time"] = cred->registrationTime;

        // 128 hex chars of credential ID plus the remaining fields can exceed
        // 256 bytes at their maximum lengths.
        char jsonBuf[320];
        serializeJson(doc, jsonBuf, sizeof(jsonBuf));
        _out->print("CRED:");
        _out->println(jsonBuf);
        listCount++;
    });
    _out->printf("END:count=%d\n", listCount);
}

void SerialCommandHandler::handleDelete(const char* credIdHex) {
    uint8_t credId[64];
    size_t credIdLen;

    if (!hexToBytes(credIdHex, credId, sizeof(credId), &credIdLen)) {
        sendResponse("ERROR", "Invalid credential ID format");
        return;
    }

    if (_store->remove(credId, credIdLen)) {
        sendResponse("OK", "Credential deleted");
    } else {
        sendResponse("ERROR", "Credential not found");
    }
}

void SerialCommandHandler::handleInfo() {
    JsonDocument doc;

    doc["version"]          = "0.1.0";
    doc["rp_id"]            = FIDOLOCK_RP_ID;
    doc["credential_count"] = _store->getCount();
    doc["free_storage"]     = _store->getFreeSpace();

    char jsonBuf[256];
    serializeJson(doc, jsonBuf, sizeof(jsonBuf));
    _out->print("INFO:");
    _out->println(jsonBuf);
    sendResponse("OK");
}

void SerialCommandHandler::handleClearAll() {
    if (_store->format()) {
        sendResponse("OK", "All credentials cleared");
    } else {
        sendResponse("ERROR", "Failed to clear credentials");
    }
}

bool SerialCommandHandler::hexToBytes(const char* hex, uint8_t* bytes, size_t maxLen, size_t* outLen) {
    size_t hexLen = strlen(hex);

    // Must be even length
    if (hexLen % 2 != 0) return false;

    size_t byteLen = hexLen / 2;
    if (byteLen > maxLen) return false;

    for (size_t i = 0; i < byteLen; i++) {
        char byteStr[3] = { hex[i*2], hex[i*2+1], '\0' };
        char* endPtr;
        bytes[i] = (uint8_t)strtol(byteStr, &endPtr, 16);

        if (*endPtr != '\0') return false;
    }

    *outLen = byteLen;
    return true;
}

void SerialCommandHandler::sendResponse(const char* status, const char* message) {
    _out->print(status);
    if (message) {
        _out->print(":");
        _out->print(message);
    }
    _out->println();
}
