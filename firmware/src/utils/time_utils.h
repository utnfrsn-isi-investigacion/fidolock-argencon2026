/**
 * FidoLock - Time Utilities
 *
 * Pure functions for time-based security checks.
 * No Arduino/hardware dependencies — usable in native unit tests.
 */

#ifndef FIDOLOCK_TIME_UTILS_H
#define FIDOLOCK_TIME_UTILS_H

#include <stdint.h>
#include <stdbool.h>

namespace TimeUtils {

/**
 * Returns true when the elapsed time since `startMs` has exceeded `windowMs`.
 *
 * The check is strictly greater-than so that a response arriving at
 * exactly the window boundary is still accepted (boundary is inclusive).
 *
 * @param now      Current tick in milliseconds (e.g. millis())
 * @param startMs  Tick at which the timed interval began
 * @param windowMs Maximum allowed elapsed time in milliseconds
 */
inline bool hasElapsed(uint32_t now, uint32_t startMs, uint32_t windowMs) {
    return (now - startMs) > windowMs;
}

} // namespace TimeUtils

#endif  // FIDOLOCK_TIME_UTILS_H
