/**
 * FidoLock - Domain: Stored Credential
 *
 * Pure domain object with zero includes outside <stdint.h>/<stddef.h>.
 */

#ifndef FIDOLOCK_DOMAIN_CREDENTIAL_H
#define FIDOLOCK_DOMAIN_CREDENTIAL_H

#include <stdint.h>
#include <stddef.h>

/**
 * Stored credential structure.
 *
 * IMPORTANT: This struct is serialised directly to LittleFS JSON.
 * Any field addition, removal, or reordering silently corrupts all stored
 * credentials.
 */
struct StoredCredential {
    uint8_t uid[10];                // NFC card UID
    uint8_t uidLen;
    uint8_t publicKey[65];          // ECDSA public key (uncompressed, 0x04 || x || y)
    uint8_t credentialId[64];       // FIDO2 credential ID
    uint8_t credentialIdLen;
    uint32_t lastCounter;           // Last valid counter (replay protection)
    uint32_t registrationTime;      // Registration timestamp
    char userName[32];              // User display name
    bool enabled;                   // Credential active flag
};

#endif // FIDOLOCK_DOMAIN_CREDENTIAL_H
