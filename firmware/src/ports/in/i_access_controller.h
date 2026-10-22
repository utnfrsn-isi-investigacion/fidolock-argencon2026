/**
 * FidoLock - Driving Port: Access Controller
 *
 * Driving port (in): the API external actors call into the application
 * to control physical access. Rule 3 of Ports & Adapters — Cockburn 2005.
 */

#ifndef FIDOLOCK_PORTS_IN_I_ACCESS_CONTROLLER_H
#define FIDOLOCK_PORTS_IN_I_ACCESS_CONTROLLER_H

#include <stdint.h>
#include <stddef.h>

class IAccessController {
public:
    virtual ~IAccessController() = default;

    virtual bool init() = 0;
    virtual void grantAccess(uint32_t durationMs) = 0;
    virtual void denyAccess() = 0;
    virtual void emergencyUnlock() = 0;
    virtual void lock() = 0;
    virtual bool isUnlocked() = 0;
    virtual void setLockoutPolicy(uint8_t maxAttempts, uint32_t lockoutDuration) = 0;
    virtual void recordFailedAttempt() = 0;
    virtual bool isLockedOut() = 0;
    virtual uint32_t getLockoutRemaining() = 0;
    virtual void resetFailedAttempts() = 0;
    virtual void update() = 0;
    virtual uint8_t getFailedAttempts() = 0;
};

#endif // FIDOLOCK_PORTS_IN_I_ACCESS_CONTROLLER_H
