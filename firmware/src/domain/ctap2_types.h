/**
 * FidoLock - Domain: CTAP2 Types
 *
 * CTAP2 response structure and status codes.
 * Zero includes outside <stdint.h>/<stddef.h>.
 */

#ifndef FIDOLOCK_DOMAIN_CTAP2_TYPES_H
#define FIDOLOCK_DOMAIN_CTAP2_TYPES_H

#include <stdint.h>
#include <stddef.h>

/**
 * CTAP2 response structure for assertion / makeCredential operations.
 */
struct CTAP2Response {
    uint8_t status;                 // CTAP2 status code
    uint8_t authData[37];           // Authenticator data
    uint8_t rpIdHash[32];           // authData[0..31]: SHA-256(rpId)
    uint8_t signature[72];          // DER-encoded signature
    uint8_t signatureLen;
    uint32_t counter;               // Usage counter
    bool userPresent;               // User presence flag
    uint8_t credentialId[64];       // Credential ID used (from response key 0x01)
    uint8_t credentialIdLen;        // 0 if not present in response
};

// CTAP2 Status Codes
#define CTAP2_OK                    0x00
#define CTAP2_ERR_INVALID_COMMAND   0x01
#define CTAP2_ERR_INVALID_PARAMETER 0x02
#define CTAP2_ERR_INVALID_LENGTH    0x03
#define CTAP2_ERR_INVALID_SEQ       0x04
#define CTAP2_ERR_TIMEOUT           0x05
#define CTAP2_ERR_CHANNEL_BUSY      0x06
#define CTAP2_ERR_NO_CREDENTIALS    0x2E
#define CTAP2_ERR_NOT_ALLOWED       0x30
#define CTAP2_ERR_PIN_REQUIRED      0x36

#endif // FIDOLOCK_DOMAIN_CTAP2_TYPES_H
