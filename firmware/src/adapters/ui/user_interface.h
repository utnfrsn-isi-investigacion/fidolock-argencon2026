/**
 * FidoLock - User Interface Module
 *
 * Controls dual LEDs (green/red) and buzzer for user feedback.
 */

#ifndef FIDOLOCK_USER_INTERFACE_H
#define FIDOLOCK_USER_INTERFACE_H

#include <Arduino.h>
#include "../../config.h"
#include "../../ports/out/i_user_interface.h"

// UI States
enum UIState {
    UI_STATE_OFF,
    UI_STATE_IDLE,
    UI_STATE_PROCESSING,
    UI_STATE_SUCCESS,
    UI_STATE_ERROR,
    UI_STATE_CLONE_ALERT,
    UI_STATE_LOCKOUT,
    UI_STATE_STARTUP
};

class UserInterface : public IUserInterface {
public:
    /**
     * Initialize user interface (LEDs and buzzer)
     * @return true if initialization was successful
     */
    bool init();

    void update() override;
    void setIdle() override;
    void showProcessing() override;
    void showSuccess() override;
    void showError() override;
    void showCloneAlert() override;
    void showLockout() override;

    /**
     * Show startup sequence (called from setup(), not part of IUserInterface).
     */
    void showStartup();

    /**
     * Set green LED state
     * @param brightness PWM value 0-255 (0=off, 255=full)
     */
    void setGreenLED(uint8_t brightness);

    /**
     * Set red LED state
     * @param brightness PWM value 0-255 (0=off, 255=full)
     */
    void setRedLED(uint8_t brightness);

    /**
     * Turn off all LEDs
     */
    void ledsOff();

    /**
     * Emit a beep
     * @param durationMs Duration in ms
     * @param frequency Frequency in Hz (default: BUZZER_FREQUENCY)
     */
    void beep(uint16_t durationMs, uint16_t frequency = BUZZER_FREQUENCY);

    /**
     * Emit short beep (success)
     */
    void beepSuccess();

    /**
     * Emit long beep (error)
     */
    void beepError();

    /**
     * Emit alarm pattern
     */
    void beepAlarm();

    /**
     * Emit lockout pattern (two short low beeps)
     */
    void beepLockout();

    /**
     * Get current state
     * @return Current UI state
     */
    UIState getState();

private:
    UIState _state = UI_STATE_OFF;
    uint32_t _stateStart = 0;
    uint32_t _lastBlink = 0;
    bool _blinkOn = false;
    uint8_t _blinkCount = 0;

    // Buzzer
    bool _buzzerActive = false;
    uint32_t _buzzerStart = 0;
    uint16_t _buzzerDuration = 0;

    // PWM helpers
    void setupPWM();
    void setBuzzerPWM(uint16_t frequency, uint8_t duty);
    void stopBuzzer();
};

#endif // FIDOLOCK_USER_INTERFACE_H
