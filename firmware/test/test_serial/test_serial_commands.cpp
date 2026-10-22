/**
 * FidoLock - Serial Command Handler Unit Tests
 *
 * Tests the REAL SerialCommandHandler through its driving port (ISerialCommands).
 * The credential store is replaced by MockCredentialStore.
 * Output is captured by FakeOutputStream.
 *
 * Composition root: each TEST_F SetUp() wires
 *   SerialCommandHandler ← MockCredentialStore + FakeOutputStream.
 *
 * Rule 5 (ADR 008): no local re-implementations of production parsing logic.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string.h>
#include <stdint.h>

#include "../test_hal/test_hal.h"
#include "../test_mocks/mock_interfaces.h"

// Real adapter module under test
#include "../../src/adapters/serial/serial_commands.h"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AnyNumber;

// ============================================================
// Test fixture: real SerialCommandHandler wired to MockCredentialStore
// Composition root per ADR 008, Rule 4.
// ============================================================

class SerialCommandHandlerTest : public ::testing::Test {
protected:
    MockCredentialStore mockStore;
    FakeOutputStream    fakeOut;
    FakeSerialInput     fakeIn;
    TestTimeProvider    timeProvider;
    SerialCommandHandler handler;

    void SetUp() override {
        fakeOut.reset();
        fakeIn.reset();
        handler.init(&mockStore, fakeOut, fakeIn, timeProvider);
    }

    const char* serialOutput() {
        return fakeOut.output().c_str();
    }
};

// ============================================================
// LIST command — calls store->listAll()
// ============================================================

TEST_F(SerialCommandHandlerTest, ListCallsStoreListAll) {
    EXPECT_CALL(mockStore, listAll(_)).Times(1);

    handler.processCommand("LIST");
}

TEST_F(SerialCommandHandlerTest, ListPrintsEndMarker) {
    EXPECT_CALL(mockStore, listAll(_)).Times(1);

    handler.processCommand("LIST");

    EXPECT_NE(nullptr, strstr(serialOutput(), "END:count="));
}

// ============================================================
// DELETE command — calls store->remove()
// ============================================================

TEST_F(SerialCommandHandlerTest, DeleteCallsStoreRemoveOnSuccess) {
    EXPECT_CALL(mockStore, remove(_, _)).WillOnce(Return(true));

    handler.processCommand("DELETE:0102030405060708");
}

TEST_F(SerialCommandHandlerTest, DeleteRespondsOkOnSuccess) {
    EXPECT_CALL(mockStore, remove(_, _)).WillOnce(Return(true));

    handler.processCommand("DELETE:0102030405060708");

    EXPECT_NE(nullptr, strstr(serialOutput(), "OK"));
}

TEST_F(SerialCommandHandlerTest, DeleteRespondsErrorWhenNotFound) {
    EXPECT_CALL(mockStore, remove(_, _)).WillOnce(Return(false));

    handler.processCommand("DELETE:0102030405060708");

    EXPECT_NE(nullptr, strstr(serialOutput(), "ERROR"));
}

TEST_F(SerialCommandHandlerTest, DeleteRespondsErrorOnInvalidHex) {
    // No store interaction expected for malformed input
    EXPECT_CALL(mockStore, remove(_, _)).Times(0);

    handler.processCommand("DELETE:ZZZZZZ");

    EXPECT_NE(nullptr, strstr(serialOutput(), "ERROR"));
}

// ============================================================
// REG command — calls store->save()
// ============================================================

TEST_F(SerialCommandHandlerTest, RegisterCallsStoreSaveOnValidInput) {
    EXPECT_CALL(mockStore, save(_)).WillOnce(Return(true));

    // Minimal valid registration JSON with 64-byte credId (128 hex chars)
    // and 65-byte public key (130 hex chars, starts with 04)
    const char* credId128 =
        "0102030405060708090a0b0c0d0e0f10"
        "1112131415161718191a1b1c1d1e1f20"
        "2122232425262728292a2b2c2d2e2f30"
        "3132333435363738393a3b3c3d3e3f40";
    const char* pubKey130 =
        "04"
        "0102030405060708090a0b0c0d0e0f10"
        "1112131415161718191a1b1c1d1e1f20"
        "2122232425262728292a2b2c2d2e2f30"
        "3132333435363738393a3b3c3d3e3f40";  // 130 hex chars = 65 bytes

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "REG:{\"credential_id\":\"%s\",\"public_key\":\"%s\",\"user_name\":\"alice\"}",
             credId128, pubKey130);

    handler.processCommand(cmd);
}

TEST_F(SerialCommandHandlerTest, RegisterRespondsErrorOnInvalidJson) {
    EXPECT_CALL(mockStore, save(_)).Times(0);

    handler.processCommand("REG:not_valid_json");

    EXPECT_NE(nullptr, strstr(serialOutput(), "ERROR"));
}

TEST_F(SerialCommandHandlerTest, RegisterRespondsErrorOnMissingFields) {
    EXPECT_CALL(mockStore, save(_)).Times(0);

    handler.processCommand("REG:{\"user_name\":\"alice\"}");

    EXPECT_NE(nullptr, strstr(serialOutput(), "ERROR"));
}

// ============================================================
// CLEAR_ALL command — delegates to store->format()
// ============================================================

TEST_F(SerialCommandHandlerTest, ClearAllCallsStoreFormat) {
    EXPECT_CALL(mockStore, format()).WillOnce(Return(true));

    handler.processCommand("CLEAR_ALL");
}

TEST_F(SerialCommandHandlerTest, ClearAllRespondsOk) {
    EXPECT_CALL(mockStore, format()).WillOnce(Return(true));

    handler.processCommand("CLEAR_ALL");

    EXPECT_NE(nullptr, strstr(serialOutput(), "OK"));
}

// ============================================================
// INFO command — delegates to store->getCount() / getFreeSpace()
// ============================================================

TEST_F(SerialCommandHandlerTest, InfoCallsStoreGetCount) {
    EXPECT_CALL(mockStore, getCount()).WillOnce(Return(3));
    EXPECT_CALL(mockStore, getFreeSpace()).WillOnce(Return(0));

    handler.processCommand("INFO");
}

TEST_F(SerialCommandHandlerTest, InfoPrintsInfoPrefix) {
    EXPECT_CALL(mockStore, getCount()).WillOnce(Return(0));
    EXPECT_CALL(mockStore, getFreeSpace()).WillOnce(Return(0));

    handler.processCommand("INFO");

    EXPECT_NE(nullptr, strstr(serialOutput(), "INFO:"));
}

// ============================================================
// Unknown / malformed commands
// ============================================================

TEST_F(SerialCommandHandlerTest, UnknownCommandRespondsError) {
    EXPECT_CALL(mockStore, save(_)).Times(0);
    EXPECT_CALL(mockStore, listAll(_)).Times(0);
    EXPECT_CALL(mockStore, remove(_, _)).Times(0);
    EXPECT_CALL(mockStore, format()).Times(0);

    handler.processCommand("UNKNOWN_CMD");

    EXPECT_NE(nullptr, strstr(serialOutput(), "ERROR"));
}

TEST_F(SerialCommandHandlerTest, EmptyCommandIgnored) {
    // No store interaction for empty input
    EXPECT_CALL(mockStore, save(_)).Times(0);
    EXPECT_CALL(mockStore, listAll(_)).Times(0);

    handler.processCommand("");
}

// ============================================================
// isBusy state
// ============================================================

TEST_F(SerialCommandHandlerTest, NotBusyInitially) {
    EXPECT_FALSE(handler.isBusy());
}

// ============================================================
// update() — drains the injected ISerialInput byte source.
// Previously a no-op stub in unit-test builds; now exercised
// through the real line-buffering code path (ISerialInput port).
// ============================================================

TEST_F(SerialCommandHandlerTest, UpdateProcessesNewlineTerminatedLine) {
    EXPECT_CALL(mockStore, listAll(_)).Times(1);

    fakeIn.feed("LIST\n");
    handler.update();

    EXPECT_NE(nullptr, strstr(serialOutput(), "END:count="));
}

TEST_F(SerialCommandHandlerTest, UpdateBuffersUntilTerminator) {
    EXPECT_CALL(mockStore, listAll(_)).Times(1);

    // Bytes delivered without a terminator must not dispatch yet...
    fakeIn.feed("LI");
    handler.update();
    EXPECT_EQ(nullptr, strstr(serialOutput(), "END:count="));

    // ...and the command completes once the newline arrives.
    fakeIn.feed("ST\r");
    handler.update();
    EXPECT_NE(nullptr, strstr(serialOutput(), "END:count="));
}

TEST_F(SerialCommandHandlerTest, UpdateWithNoBytesIsNoOp) {
    EXPECT_CALL(mockStore, listAll(_)).Times(0);

    handler.update();  // empty byte source — must not crash or dispatch
}

int main(int argc, char** argv) {
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
