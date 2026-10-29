/**
 * FidoLock - FNV-1a Credential Hash Utility
 *
 * Pure function used by CredentialStore to derive a deterministic filename
 * from a credential ID.
 *
 * Isolated here so it can be unit-tested on any platform without dragging
 * in LittleFS or the CredentialStore adapter.
 */

#ifndef FIDOLOCK_UTILS_CREDENTIAL_HASH_H
#define FIDOLOCK_UTILS_CREDENTIAL_HASH_H

#include <stdint.h>

/**
 * FNV-1a 32-bit hash of an arbitrary byte buffer.
 *
 * @param data   Pointer to the input bytes.
 * @param len    Number of bytes to hash.
 * @return       32-bit FNV-1a digest.
 */
inline uint32_t fnv1a_hash(const uint8_t* data, uint8_t len) {
    uint32_t hash = 2166136261U;  // FNV offset basis
    for (uint8_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619U;  // FNV prime
    }
    return hash;
}

#endif // FIDOLOCK_UTILS_CREDENTIAL_HASH_H
