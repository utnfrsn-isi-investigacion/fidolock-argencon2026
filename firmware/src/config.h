/**
 * FidoLock - System Configuration
 *
 * This file contains all configuration constants for the
 * physical access control system.
 */

#ifndef FIDOLOCK_CONFIG_H
#define FIDOLOCK_CONFIG_H

// =============================================================================
// Firmware Version
// =============================================================================
#define FIDOLOCK_VERSION_MAJOR  0
#define FIDOLOCK_VERSION_MINOR  1
#define FIDOLOCK_VERSION_PATCH  0
#define FIDOLOCK_VERSION        "0.1.0"

// =============================================================================
// FIDO2/CTAP2 Configuration
// =============================================================================
#define FIDOLOCK_RP_ID          "fidolock.local"
#define FIDOLOCK_RP_NAME        "FidoLock Access Control"

// Challenge timeout (ms)
#define CHALLENGE_TIMEOUT_MS    5000

// Challenge RNG: how long the boot-time Bluetooth RF window stays open before
// the CTR-DRBG draws its seed, letting the RF subsystem's high-speed ADC feed
// the hardware RNG (ESP32 TRM v5.8 §18; residual risks in Section IV-B
// of the paper).
#define RNG_RF_SETTLE_MS        100

// =============================================================================
// GPIO Pins - ESP32 DevKit V1
// =============================================================================

// SPI for PN532 NFC Reader
#ifndef PIN_SPI_SCK                     // Pin 11 on the right of ESP32S 38 pins
#define PIN_SPI_SCK             18      // SPI Clock
#endif
#ifndef PIN_SPI_MISO                    // Pin 12 on the right of ESP32S 38 pins
#define PIN_SPI_MISO            19      // Master In Slave Out (PN532 -> ESP32)
#endif
#ifndef PIN_SPI_MOSI                    // Pin 18 on the right of ESP32S 38 pins
#define PIN_SPI_MOSI            23      // Master Out Slave In (ESP32 -> PN532)
#endif
#ifndef PIN_SPI_SS                      // Pin 10 on the right of ESP32S 38 pins
#define PIN_SPI_SS              5       // Slave Select (Chip Select, active LOW)
#endif
#ifndef PIN_PN532_IRQ                   // Pin 7 on the right of ESP32S 38 pins
#define PIN_PN532_IRQ           4       // Interrupt Request (optional)
#endif

// Lock Control (Relay via 2N2222A transistor)
#ifndef PIN_RELAY_CTRL
#define PIN_RELAY_CTRL          26      // Base of Q1 via 1k resistor
#endif

// Status LEDs (active HIGH with 330 ohm resistors)
#ifndef PIN_LED_GREEN
#define PIN_LED_GREEN           27      // Success indicator
#endif
#ifndef PIN_LED_RED
#define PIN_LED_RED             14      // Error indicator
#endif

// Buzzer (via 2N2222A transistor)
#ifndef PIN_BUZZER
#define PIN_BUZZER              32      // Base of Q2 via 1k resistor
#endif

// Tamper Detection Switch
#ifndef PIN_TAMPER_SW
#define PIN_TAMPER_SW           33      // Pull-up 10k, LOW when case closed
#endif

// =============================================================================
// NFC Configuration (SPI Mode)
// =============================================================================
#define NFC_SPI_CLOCK           1000000     // 1MHz SPI clock
#define NFC_DETECT_TIMEOUT_MS   2000  // allow slower tags (YubiKey NFC can take longer to power up)
#define NFC_READ_TIMEOUT_MS     200
#define NFC_COMMAND_TIMEOUT_MS  2000

// =============================================================================
// Access Control Configuration
// =============================================================================
#ifndef ACCESS_DURATION_MS
#define ACCESS_DURATION_MS      5000        // Lock open duration (ms)
#endif
#ifndef MAX_FAILED_ATTEMPTS
#define MAX_FAILED_ATTEMPTS     5           // Attempts before lockout
#endif
#ifndef LOCKOUT_DURATION_MS
#define LOCKOUT_DURATION_MS     300000      // Lockout duration (5 min)
#endif

// =============================================================================
// Storage Configuration
// =============================================================================
#define CREDENTIALS_PATH        "/credentials"
#define CONFIG_FILE             "/config.json"
#define LOG_FILE                "/logs/access.log"
#define MAX_CREDENTIALS         100         // On-disk LittleFS store ceiling
#define MAX_LOG_SIZE            65536       // 64KB

// Maximum length of a FIDO2 credential ID, in bytes (CTAP2 allows up to 64).
#define MAX_CREDENTIAL_ID_LEN   64

// =============================================================================
// UI Configuration
// =============================================================================
#define LED_PWM_FREQUENCY       5000
#define LED_PWM_RESOLUTION      8           // 8 bits (0-255)
#define LED_PWM_CHANNEL_GREEN   0
#define LED_PWM_CHANNEL_RED     1
#define BUZZER_PWM_CHANNEL      2
#define BUZZER_FREQUENCY        2000        // 2kHz

// Feedback durations (ms)
#define BEEP_SHORT_MS           100
#define BEEP_LONG_MS            500
#define LED_BLINK_INTERVAL_MS   200
#define LOCKOUT_BLINK_INTERVAL_MS 1000      // Slow red blink while locked out

// =============================================================================
// Debug Configuration
// =============================================================================
#define DEBUG_SERIAL            1
#ifndef DEBUG_NFC
#define DEBUG_NFC               0
#endif
#define DEBUG_CTAP2             1
#define DEBUG_CRYPTO            0           // Warning: may expose sensitive data

#endif // FIDOLOCK_CONFIG_H
