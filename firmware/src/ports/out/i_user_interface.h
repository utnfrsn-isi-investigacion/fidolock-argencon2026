/**
 * FidoLock - Driven Port: User Interface
 *
 * Driven port (out): the SPI the application calls out to the UI adapter.
 * Rule 3 of Ports & Adapters — Cockburn 2005.
 */

#ifndef FIDOLOCK_PORTS_OUT_I_USER_INTERFACE_H
#define FIDOLOCK_PORTS_OUT_I_USER_INTERFACE_H

class IUserInterface {
public:
    virtual ~IUserInterface() = default;

    /** Call once per main-loop iteration to drive blink/beep sequences. */
    virtual void update() = 0;

    /** Ready — waiting for card. */
    virtual void setIdle() = 0;

    /** NFC card detected; authentication in progress. */
    virtual void showProcessing() = 0;

    /** Authentication succeeded; access granted. */
    virtual void showSuccess() = 0;

    /** Authentication failed or system error. */
    virtual void showError() = 0;

    /** Counter replay detected — possible credential clone. */
    virtual void showCloneAlert() = 0;

    /**
     * Failed-attempt lockout active: access is withheld even for valid keys.
     * Persists (in place of the idle indication) until another state is shown.
     */
    virtual void showLockout() = 0;
};

#endif // FIDOLOCK_PORTS_OUT_I_USER_INTERFACE_H
