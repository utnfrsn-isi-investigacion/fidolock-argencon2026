/**
 * FidoLock - Crypto Utilities
 *
 * Pure functions for cryptographic operations that don't depend on hardware.
 * These can be tested directly without mocks.
 */

#ifndef FIDOLOCK_CRYPTO_UTILS_H
#define FIDOLOCK_CRYPTO_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace CryptoUtils {

/**
 * Helper function to copy and pad an integer component to 32 bytes.
 * Used for r and s components in DER signature parsing.
 * 
 * @param dest Destination buffer (32 bytes)
 * @param src Source data pointer
 * @param len Length of source data
 * @return true if successful, false if len > 32
 */
inline bool copyAndPadTo32Bytes(uint8_t* dest, const uint8_t* src, uint8_t len) {
    if (len < 32) {
        // Pad with leading zeros
        memset(dest, 0, 32 - len);
        memcpy(&dest[32 - len], src, len);
    } else if (len == 32) {
        memcpy(dest, src, 32);
    } else {
        return false;
    }
    return true;
}

/**
 * Parse a DER-encoded ECDSA signature into r and s components.
 *
 * DER format: 0x30 <total_len> 0x02 <r_len> <r_bytes> 0x02 <s_len> <s_bytes>
 *
 * @param der DER-encoded signature
 * @param derLen Length of DER signature
 * @param r Output buffer for r component (32 bytes)
 * @param s Output buffer for s component (32 bytes)
 * @return true if parsing succeeded
 */
inline bool parseDERSignature(const uint8_t* der, size_t derLen,
                               uint8_t* r, uint8_t* s) {
    // Minimum well-formed signature: 30 06 02 01 rr 02 01 ss = 8 bytes.
    if (derLen < 8 || der[0] != 0x30) {
        return false;
    }

    size_t offset = 2;  // Skip SEQUENCE tag (0x30) and total-length byte.

    // ---- INTEGER r ----
    if (offset >= derLen || der[offset++] != 0x02) {
        return false;
    }
    if (offset >= derLen) {
        return false;
    }
    uint8_t rLen = der[offset++];
    // The r bytes must lie within the buffer. (derLen - offset cannot
    // underflow: offset <= derLen here.)
    if (rLen == 0 || rLen > derLen - offset) {
        return false;
    }
    const uint8_t* rPtr = &der[offset];
    offset += rLen;  // Advance past the encoded r before reinterpreting length.

    // Handle leading zero (for positive numbers with high bit set)
    if (rLen == 33 && rPtr[0] == 0x00) {
        rPtr++;
        rLen = 32;
    }

    if (!copyAndPadTo32Bytes(r, rPtr, rLen)) {
        return false;
    }

    // ---- INTEGER s ----
    if (offset >= derLen || der[offset++] != 0x02) {
        return false;
    }
    if (offset >= derLen) {
        return false;
    }
    uint8_t sLen = der[offset++];
    if (sLen == 0 || sLen > derLen - offset) {
        return false;
    }
    const uint8_t* sPtr = &der[offset];

    if (sLen == 33 && sPtr[0] == 0x00) {
        sPtr++;
        sLen = 32;
    }

    if (!copyAndPadTo32Bytes(s, sPtr, sLen)) {
        return false;
    }

    return true;
}

/**
 * Parse a COSE public key into uncompressed format.
 *
 * @param coseKey COSE-encoded public key
 * @param coseLen Length of COSE key
 * @param publicKey Output buffer (65 bytes: 0x04 || x || y)
 * @return true if parsing succeeded
 */
inline bool parseCOSEPublicKey(const uint8_t* coseKey, size_t coseLen,
                                uint8_t* publicKey) {
    if (coseLen < 70) {
        return false;
    }

    // Set uncompressed point format marker
    publicKey[0] = 0x04;

    size_t offset = 0;

    // Expect CBOR map
    if ((coseKey[offset] & 0xE0) != 0xA0) {
        return false;
    }

    uint8_t numItems = coseKey[offset++] & 0x1F;
    bool hasX = false, hasY = false;

    for (uint8_t i = 0; i < numItems && offset < coseLen; i++) {
        // Read key (may be negative)
        int8_t key = 0;
        if ((coseKey[offset] & 0xE0) == 0x20) {  // Negative integer
            key = -((coseKey[offset] & 0x1F) + 1);
            offset++;
        } else if ((coseKey[offset] & 0xE0) == 0x00) {  // Positive integer
            key = coseKey[offset] & 0x1F;
            offset++;
        } else {
            offset++;
            continue;
        }

        // Read value
        if (key == -2) {  // x coordinate
            if ((coseKey[offset] & 0xE0) == 0x40) {  // Byte string
                uint8_t len = coseKey[offset++] & 0x1F;
                if (len == 24) len = coseKey[offset++];
                if (len == 32) {
                    memcpy(&publicKey[1], &coseKey[offset], 32);
                    hasX = true;
                }
                offset += len;
            }
        } else if (key == -3) {  // y coordinate
            if ((coseKey[offset] & 0xE0) == 0x40) {
                uint8_t len = coseKey[offset++] & 0x1F;
                if (len == 24) len = coseKey[offset++];
                if (len == 32) {
                    memcpy(&publicKey[33], &coseKey[offset], 32);
                    hasY = true;
                }
                offset += len;
            }
        } else {
            // Skip value
            if ((coseKey[offset] & 0xE0) == 0x40 ||
                (coseKey[offset] & 0xE0) == 0x60) {
                uint8_t len = coseKey[offset++] & 0x1F;
                if (len == 24) len = coseKey[offset++];
                offset += len;
            } else {
                offset++;
            }
        }
    }

    return hasX && hasY;
}

} // namespace CryptoUtils

#endif // FIDOLOCK_CRYPTO_UTILS_H
