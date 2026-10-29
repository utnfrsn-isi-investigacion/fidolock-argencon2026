/**
 * FidoLock - Physical Access Control System
 *
 * Composition root only. Business logic lives in application/.
 *
 * @author UTN FRSN - Research Project
 * @version 0.1.0
 */

#include <Arduino.h>
#include "config.h"
#include "adapters/nfc/nfc_reader.h"
#include "application/ctap2/ctap2_handler.h"
#include "adapters/crypto/crypto_engine.h"
#include "adapters/storage/credential_store.h"
#include "application/access/access_controller.h"
#include "adapters/hal/arduino_hal.h"
#include "adapters/ui/user_interface.h"
#include "adapters/serial/serial_commands.h"
#include "adapters/serial/arduino_serial_input.h"
#include "adapters/hal/arduino_output_stream.h"
#include "application/orchestrator/fidolock_orchestrator.h"

// ---------------------------------------------------------------------------
// Adapters
// ---------------------------------------------------------------------------
ArduinoTimeProvider   timeProvider;
ArduinoGPIOController gpioController;
ArduinoOutputStream   serialOut;
ArduinoSerialInput    serialIn;
ArduinoSystemControl  systemControl;
ArduinoBtRadio        btRadio;

// ---------------------------------------------------------------------------
// Infrastructure services
// ---------------------------------------------------------------------------
NFCReader        nfcReader;
CryptoEngine     cryptoEngine(btRadio, systemControl);
CredentialStore  credentialStore;
UserInterface    ui;

// ---------------------------------------------------------------------------
// Application services
// ---------------------------------------------------------------------------
AccessController accessController(timeProvider, gpioController);
CTAP2Handler     ctap2Handler;

// ---------------------------------------------------------------------------
// Orchestrator — wired at the composition root
// ---------------------------------------------------------------------------
FidoLockOrchestrator orchestrator(
    nfcReader, ctap2Handler, cryptoEngine, credentialStore,
    accessController, ui, timeProvider, systemControl, serialOut);

// Serial command handler (independent input channel)
SerialCommandHandler serialHandler;

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=================================");
    Serial.println("  FidoLock v0.1.0");
    Serial.println("  FIDO2 Access Control");
    Serial.println("=================================\n");

    Serial.print("Initializing UI... ");
    if (!ui.init()) {
        Serial.println("ERROR");
        while (1) delay(1000);
    }
    Serial.println("OK");
    ui.showStartup();

    Serial.print("Initializing storage... ");
    if (!credentialStore.init()) {
        Serial.println("ERROR");
        ui.showError();
        while (1) delay(1000);
    }
    Serial.println("OK");

    Serial.print("Initializing crypto... ");
    if (!cryptoEngine.init()) {
        Serial.println("ERROR");
        ui.showError();
        while (1) delay(1000);
    }
    Serial.println("OK");

    Serial.print("Initializing NFC (PN532)... ");
    if (!nfcReader.init()) {
        Serial.println("ERROR");
        ui.showError();
        while (1) delay(1000);
    }
    Serial.println("OK");

    Serial.print("Initializing access controller... ");
    if (!accessController.init()) {
        Serial.println("ERROR");
        ui.showError();
        while (1) delay(1000);
    }
    Serial.println("OK");

    Serial.print("Initializing serial commands... ");
    if (!serialHandler.init(&credentialStore, serialOut, serialIn, timeProvider)) {
        Serial.println("ERROR");
    } else {
        Serial.println("OK");
    }

    Serial.println("\nSystem ready. Waiting for YubiKey...\n");
    Serial.println("Serial commands available: REG, LIST, DELETE, INFO, CLEAR_ALL\n");
    ui.setIdle();
}

void loop() {
    orchestrator.tick();
    accessController.update();  // drives auto-lock and lockout expiry timers
    serialHandler.update();
    delay(10);
}
