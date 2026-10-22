/**
 * FidoLock - Credential Store Module
 *
 * Persistent storage and management of FIDO2 credentials using LittleFS.
 */

#include "credential_store.h"
#include "../../utils/credential_hash.h"
#include "../../utils/log.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

static const char* TAG = "Storage";

bool CredentialStore::init() {
    // Initialize LittleFS.
    // The partition label "littlefs" must match the name column in partitions.csv.
    // LittleFS.begin() defaults to the label "spiffs", so we pass it explicitly.
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {  // true = format if mount fails
        LOG_E(TAG, "LittleFS mount failed");
        return false;
    }

    LOG_I(TAG, "LittleFS mounted successfully");

    // Create credentials directory if it doesn't exist
    if (!LittleFS.exists(CREDENTIALS_PATH)) {
        if (!LittleFS.mkdir(CREDENTIALS_PATH)) {
            LOG_E(TAG, "Failed to create credentials directory");
            return false;
        }
        LOG_I(TAG, "Created credentials directory");
    }

    // Log storage info
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    LOG_I(TAG, "Total=%u bytes, Used=%u bytes, Free=%u bytes",
          totalBytes, usedBytes, totalBytes - usedBytes);
    // Some build configurations disable info-level logging, which causes
    // the LOG_I macro to expand to nothing and leaves these variables unused.
    // Prevent compiler warnings by explicitly marking them as used.
    (void)totalBytes;
    (void)usedBytes;

    return true;
}

uint32_t CredentialStore::hashCredentialId(const uint8_t* credentialId,
                                             uint8_t credentialIdLen) {
    return fnv1a_hash(credentialId, credentialIdLen);
}

void CredentialStore::getFilename(const uint8_t* credentialId, uint8_t credentialIdLen,
                                   char* filename, size_t maxLen) {
    uint32_t hash = hashCredentialId(credentialId, credentialIdLen);
    snprintf(filename, maxLen, "%s/%08X.json", CREDENTIALS_PATH, hash);
}

StoredCredential* CredentialStore::findByCredentialId(const uint8_t* credentialId,
                                                       uint8_t credentialIdLen) {
    char filename[48];
    getFilename(credentialId, credentialIdLen, filename, sizeof(filename));

    if (!loadFromFile(filename, &_cachedCredential)) {
        _cacheValid = false;
        return nullptr;
    }

    // Verify credential ID matches (guard against hash collision)
    if (_cachedCredential.credentialIdLen != credentialIdLen ||
        memcmp(_cachedCredential.credentialId, credentialId, credentialIdLen) != 0) {
        LOG_W(TAG, "Credential ID hash collision detected");
        _cacheValid = false;
        return nullptr;
    }

    if (!_cachedCredential.enabled) {
        LOG_W(TAG, "Credential is disabled/revoked");
        return nullptr;
    }

    _cacheValid = true;
    return &_cachedCredential;
}

bool CredentialStore::loadFromFile(const char* filename,
                                    StoredCredential* credential) {
    File file = LittleFS.open(filename, "r");
    if (!file) {
        return false;
    }

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        LOG_E(TAG, "JSON parse error: %s", error.c_str());
        return false;
    }

    // Extract fields
    credential->uidLen = doc["uidLen"] | 0;
    if (credential->uidLen > 10) credential->uidLen = 10;

    JsonArray uidArray = doc["uid"];
    for (uint8_t i = 0; i < credential->uidLen; i++) {
        credential->uid[i] = uidArray[i] | 0;
    }

    // Public key (base64 or hex encoded)
    const char* pubKeyHex = doc["publicKey"];
    if (pubKeyHex && strlen(pubKeyHex) == 130) {  // 65 bytes * 2
        for (int i = 0; i < 65; i++) {
            char hex[3] = {pubKeyHex[i*2], pubKeyHex[i*2+1], 0};
            credential->publicKey[i] = strtol(hex, nullptr, 16);
        }
    }

    // Credential ID
    credential->credentialIdLen = doc["credentialIdLen"] | 0;
    if (credential->credentialIdLen > 64) credential->credentialIdLen = 64;

    const char* credIdHex = doc["credentialId"];
    if (credIdHex) {
        size_t hexLen = strlen(credIdHex);
        for (size_t i = 0; i < credential->credentialIdLen && i*2+1 < hexLen; i++) {
            char hex[3] = {credIdHex[i*2], credIdHex[i*2+1], 0};
            credential->credentialId[i] = strtol(hex, nullptr, 16);
        }
    }

    credential->lastCounter = doc["lastCounter"] | 0;
    credential->registrationTime = doc["registrationTime"] | 0;
    credential->enabled = doc["enabled"] | true;

    const char* userName = doc["userName"] | "Unknown";
    strncpy(credential->userName, userName, sizeof(credential->userName) - 1);
    credential->userName[sizeof(credential->userName) - 1] = '\0';

    LOG_I(TAG, "Loaded credential for user '%s'",
          credential->userName);
    return true;
}

bool CredentialStore::saveToFile(const char* filename,
                                  const StoredCredential* credential) {
    JsonDocument doc;

    // UID as array
    JsonArray uidArray = doc["uid"].to<JsonArray>();
    for (uint8_t i = 0; i < credential->uidLen; i++) {
        uidArray.add(credential->uid[i]);
    }
    doc["uidLen"] = credential->uidLen;

    // Public key as hex string
    char pubKeyHex[131];
    for (int i = 0; i < 65; i++) {
        snprintf(&pubKeyHex[i*2], 3, "%02X", credential->publicKey[i]);
    }
    doc["publicKey"] = pubKeyHex;

    // Credential ID as hex string
    char credIdHex[129];
    for (uint8_t i = 0; i < credential->credentialIdLen; i++) {
        snprintf(&credIdHex[i*2], 3, "%02X", credential->credentialId[i]);
    }
    credIdHex[credential->credentialIdLen * 2] = '\0';
    doc["credentialId"] = credIdHex;
    doc["credentialIdLen"] = credential->credentialIdLen;

    doc["lastCounter"] = credential->lastCounter;
    doc["registrationTime"] = credential->registrationTime;
    doc["enabled"] = credential->enabled;
    doc["userName"] = credential->userName;

    // Write to file
    File file = LittleFS.open(filename, "w");
    if (!file) {
        LOG_E(TAG, "Failed to open file for writing");
        return false;
    }

    size_t written = serializeJson(doc, file);
    file.close();

    if (written == 0) {
        LOG_E(TAG, "Failed to write JSON");
        return false;
    }

    LOG_I(TAG, "Saved credential (%u bytes)", written);
    return true;
}

bool CredentialStore::save(const StoredCredential* credential) {
    char filename[48];
    getFilename(credential->credentialId, credential->credentialIdLen,
                filename, sizeof(filename));
    return saveToFile(filename, credential);
}

bool CredentialStore::remove(const uint8_t* credentialId, uint8_t credentialIdLen) {
    char filename[48];
    getFilename(credentialId, credentialIdLen, filename, sizeof(filename));

    if (!LittleFS.exists(filename)) {
        LOG_W(TAG, "Credential not found for removal");
        return false;
    }

    if (!LittleFS.remove(filename)) {
        LOG_E(TAG, "Failed to remove credential");
        return false;
    }

    LOG_I(TAG, "Credential removed successfully");
    _cacheValid = false;
    return true;
}

uint32_t CredentialStore::getCount() {
    uint32_t count = 0;

    File root = LittleFS.open(CREDENTIALS_PATH);
    if (!root || !root.isDirectory()) {
        return 0;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            count++;
        }
        file = root.openNextFile();
    }

    root.close();
    return count;
}

void CredentialStore::listAll(std::function<void(StoredCredential*)> callback) {
    File root = LittleFS.open(CREDENTIALS_PATH);
    if (!root || !root.isDirectory()) {
        return;
    }

    StoredCredential cred;
    File file = root.openNextFile();

    while (file) {
        if (!file.isDirectory()) {
            String filename = String(CREDENTIALS_PATH) + "/" + file.name();
            if (loadFromFile(filename.c_str(), &cred)) {
                callback(&cred);
            }
        }
        file = root.openNextFile();
    }

    root.close();
}

bool CredentialStore::format() {
    // Remove all files in credentials directory
    File root = LittleFS.open(CREDENTIALS_PATH);
    if (!root || !root.isDirectory()) {
        return false;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = String(CREDENTIALS_PATH) + "/" + file.name();
            LittleFS.remove(filename);
        }
        file = root.openNextFile();
    }

    root.close();
    _cacheValid = false;

    LOG_I(TAG, "All credentials removed");
    return true;
}

uint32_t CredentialStore::getFreeSpace() {
    return LittleFS.totalBytes() - LittleFS.usedBytes();
}

bool CredentialStore::updateCounter(const uint8_t* credentialId, uint8_t credentialIdLen,
                                     uint32_t newCounter) {
    StoredCredential* cred = findByCredentialId(credentialId, credentialIdLen);
    if (!cred) {
        return false;
    }

    cred->lastCounter = newCounter;
    return save(cred);
}
