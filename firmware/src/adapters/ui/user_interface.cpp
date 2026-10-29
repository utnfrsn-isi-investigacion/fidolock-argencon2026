/**
 * FidoLock - User Interface Module
 *
 * Controls dual LEDs (green/red) and buzzer for user feedback.
 */

#include "user_interface.h"
#include "../../utils/log.h"

static const char* TAG = "UI";

bool UserInterface::init() {
    // Setup PWM for LEDs and buzzer
    setupPWM();

    // Initialize buzzer pin (will use PWM)
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    // Turn off LEDs initially
    ledsOff();

    LOG_I(TAG, "Interface initialized");
    return true;
}

void UserInterface::setupPWM() {
    // Configure LED PWM channels
    ledcSetup(LED_PWM_CHANNEL_GREEN, LED_PWM_FREQUENCY, LED_PWM_RESOLUTION);
    ledcSetup(LED_PWM_CHANNEL_RED, LED_PWM_FREQUENCY, LED_PWM_RESOLUTION);

    // Attach pins to channels
    ledcAttachPin(PIN_LED_GREEN, LED_PWM_CHANNEL_GREEN);
    ledcAttachPin(PIN_LED_RED, LED_PWM_CHANNEL_RED);

    // Configure buzzer PWM channel
    ledcSetup(BUZZER_PWM_CHANNEL, BUZZER_FREQUENCY, LED_PWM_RESOLUTION);
    ledcAttachPin(PIN_BUZZER, BUZZER_PWM_CHANNEL);
}

void UserInterface::setGreenLED(uint8_t brightness) {
    ledcWrite(LED_PWM_CHANNEL_GREEN, brightness);
}

void UserInterface::setRedLED(uint8_t brightness) {
    ledcWrite(LED_PWM_CHANNEL_RED, brightness);
}

void UserInterface::ledsOff() {
    setGreenLED(0);
    setRedLED(0);
}

void UserInterface::setBuzzerPWM(uint16_t frequency, uint8_t duty) {
    if (frequency > 0 && duty > 0) {
        ledcWriteTone(BUZZER_PWM_CHANNEL, frequency);
        ledcWrite(BUZZER_PWM_CHANNEL, duty);
    } else {
        ledcWrite(BUZZER_PWM_CHANNEL, 0);
    }
}

void UserInterface::stopBuzzer() {
    ledcWrite(BUZZER_PWM_CHANNEL, 0);
    _buzzerActive = false;
}

void UserInterface::update() {
    uint32_t now = millis();

    // Handle buzzer timeout
    if (_buzzerActive && (now - _buzzerStart >= _buzzerDuration)) {
        stopBuzzer();
    }

    // Handle state-specific animations
    switch (_state) {
        case UI_STATE_IDLE:
            // Dim green indicates ready
            setGreenLED(30);
            setRedLED(0);
            break;

        case UI_STATE_PROCESSING:
            // Blink green LED
            if (now - _lastBlink >= LED_BLINK_INTERVAL_MS) {
                _lastBlink = now;
                _blinkOn = !_blinkOn;
                setGreenLED(_blinkOn ? 255 : 50);
                setRedLED(0);
            }
            break;

        case UI_STATE_ERROR:
            // Blink red LED (3 times then stay off)
            if (_blinkCount < 6) {
                if (now - _lastBlink >= LED_BLINK_INTERVAL_MS) {
                    _lastBlink = now;
                    _blinkOn = !_blinkOn;
                    _blinkCount++;
                    setGreenLED(0);
                    setRedLED(_blinkOn ? 255 : 0);
                }
            }
            break;

        case UI_STATE_CLONE_ALERT:
            // Fast red blink (alarm pattern)
            if (now - _lastBlink >= LED_BLINK_INTERVAL_MS / 2) {
                _lastBlink = now;
                _blinkOn = !_blinkOn;
                setGreenLED(0);
                setRedLED(_blinkOn ? 255 : 0);
            }
            break;

        case UI_STATE_LOCKOUT:
            // Slow red blink, green off: distinct from the dim-green ready
            // state and from the faster error and clone-alert patterns
            if (now - _lastBlink >= LOCKOUT_BLINK_INTERVAL_MS) {
                _lastBlink = now;
                _blinkOn = !_blinkOn;
                setGreenLED(0);
                setRedLED(_blinkOn ? 255 : 0);
            }
            break;

        case UI_STATE_SUCCESS:
            // Solid green - auto-return to idle after 2 seconds
            setGreenLED(255);
            setRedLED(0);
            if (now - _stateStart >= 2000) {
                setIdle();
            }
            break;

        default:
            break;
    }
}

void UserInterface::setIdle() {
    _state = UI_STATE_IDLE;
    _stateStart = millis();

    // Dim green indicates ready
    setGreenLED(30);
    setRedLED(0);

    LOG_I(TAG, "State -> IDLE");
}

void UserInterface::showProcessing() {
    _state = UI_STATE_PROCESSING;
    _stateStart = millis();
    _lastBlink = millis();
    _blinkOn = true;

    setGreenLED(255);
    setRedLED(0);

    LOG_I(TAG, "State -> PROCESSING");
}

void UserInterface::showSuccess() {
    _state = UI_STATE_SUCCESS;
    _stateStart = millis();

    // Solid green
    setGreenLED(255);
    setRedLED(0);

    // Short beep
    beepSuccess();

    LOG_I(TAG, "State -> SUCCESS");
}

void UserInterface::showError() {
    _state = UI_STATE_ERROR;
    _stateStart = millis();
    _lastBlink = millis();
    _blinkOn = true;
    _blinkCount = 0;

    setGreenLED(0);
    setRedLED(255);

    // Long beep
    beepError();

    LOG_E(TAG, "State -> ERROR");
}

void UserInterface::showCloneAlert() {
    _state = UI_STATE_CLONE_ALERT;
    _stateStart = millis();
    _lastBlink = millis();
    _blinkOn = true;

    setGreenLED(0);
    setRedLED(255);

    // Alarm beeps
    beepAlarm();

    LOG_W(TAG, "State -> CLONE_ALERT");
}

void UserInterface::showLockout() {
    _state = UI_STATE_LOCKOUT;
    _stateStart = millis();
    _lastBlink = millis();
    _blinkOn = true;

    setGreenLED(0);
    setRedLED(255);

    beepLockout();

    LOG_W(TAG, "State -> LOCKOUT");
}

void UserInterface::showStartup() {
    _state = UI_STATE_STARTUP;
    _stateStart = millis();

    // Cycle through LEDs
    setGreenLED(255);
    setRedLED(0);
    delay(200);
    setGreenLED(0);
    setRedLED(255);
    delay(200);
    setGreenLED(255);
    setRedLED(255);  // Both = yellow/orange
    delay(200);
    ledsOff();

    // Startup beep sequence
    beep(100, 1000);
    delay(100);
    beep(100, 1500);
    delay(100);
    beep(100, 2000);

    LOG_I(TAG, "Startup sequence complete");
}

void UserInterface::beep(uint16_t durationMs, uint16_t frequency) {
    setBuzzerPWM(frequency, 128);  // 50% duty cycle
    _buzzerActive = true;
    _buzzerStart = millis();
    _buzzerDuration = durationMs;
}

void UserInterface::beepSuccess() {
    beep(BEEP_SHORT_MS, BUZZER_FREQUENCY);
}

void UserInterface::beepError() {
    beep(BEEP_LONG_MS, BUZZER_FREQUENCY / 2);  // Lower frequency for error
}

void UserInterface::beepAlarm() {
    // Three rapid beeps
    for (int i = 0; i < 3; i++) {
        beep(100, 3000);  // High frequency
        delay(150);
        stopBuzzer();
        delay(50);
    }
}

void UserInterface::beepLockout() {
    // Two short low beeps
    for (int i = 0; i < 2; i++) {
        beep(150, BUZZER_FREQUENCY / 2);
        delay(150);
        stopBuzzer();
        delay(100);
    }
}

UIState UserInterface::getState() {
    return _state;
}
