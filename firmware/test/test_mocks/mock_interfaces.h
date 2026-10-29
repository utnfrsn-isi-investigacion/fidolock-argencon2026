/**
 * FidoLock - gmock adapters for domain driven ports.
 *
 * Use when the test needs to assert what was called and with what arguments.
 * For time/GPIO state control without call-sequence assertions, use the
 * hand-written fakes in test_hal/test_hal.h instead.
 *
 * Ports & Adapters rule: test doubles are adapters, not
 * re-implementations of domain logic. Each mock below implements a port
 * interface and nothing more.
 */

#ifndef FIDOLOCK_TEST_MOCKS_MOCK_INTERFACES_H
#define FIDOLOCK_TEST_MOCKS_MOCK_INTERFACES_H

#include <gmock/gmock.h>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <functional>

#include "../../src/ports/out/i_nfc_reader.h"
#include "../../src/ports/out/i_credential_store.h"
#include "../../src/ports/out/i_crypto_engine.h"
#include "../../src/ports/out/i_output_stream.h"
#include "../../src/ports/out/i_serial_input.h"
#include "../../src/ports/out/i_user_interface.h"
#include "../../src/ports/out/i_system_control.h"
#include "../../src/ports/in/i_access_controller.h"
#include "../../src/ports/in/i_ctap2_handler.h"

// ============================================================
// MockNFCReader — driven port: INFCReader
// ============================================================
class MockNFCReader : public INFCReader {
public:
    MOCK_METHOD(bool, init, (), (override));
    MOCK_METHOD(bool, detectCard, (uint8_t* uid, uint8_t* uidLength), (override));
    MOCK_METHOD(bool, transceive,
                (uint8_t* sendBuf, uint8_t sendLen,
                 uint8_t* recvBuf, uint8_t* recvLen, uint16_t timeout),
                (override));
    MOCK_METHOD(bool, sendAPDU,
                (uint8_t* apdu, size_t apduLen,
                 uint8_t* response, size_t* responseLen),
                (override));
    MOCK_METHOD(void, powerDown, (), (override));
    MOCK_METHOD(void, wakeUp, (), (override));
    MOCK_METHOD(bool, isCardPresent, (), (override));
};

// ============================================================
// MockCredentialStore — driven port: ICredentialStore
// ============================================================
class MockCredentialStore : public ICredentialStore {
public:
    MOCK_METHOD(bool, init, (), (override));
    MOCK_METHOD(StoredCredential*, findByCredentialId,
                (const uint8_t* credentialId, uint8_t credentialIdLen),
                (override));
    MOCK_METHOD(bool, save, (const StoredCredential* credential), (override));
    MOCK_METHOD(bool, remove,
                (const uint8_t* credentialId, uint8_t credentialIdLen),
                (override));
    MOCK_METHOD(uint32_t, getCount, (), (override));
    MOCK_METHOD(void, listAll, (std::function<void(StoredCredential*)> callback), (override));
    MOCK_METHOD(bool, format, (), (override));
    MOCK_METHOD(uint32_t, getFreeSpace, (), (override));
    MOCK_METHOD(bool, updateCounter,
                (const uint8_t* credentialId, uint8_t credentialIdLen,
                 uint32_t newCounter),
                (override));
};

// ============================================================
// MockCryptoEngine — driven port: ICryptoEngine
// ============================================================
class MockCryptoEngine : public ICryptoEngine {
public:
    MOCK_METHOD(bool, init, (), (override));
    MOCK_METHOD(bool, generateChallenge, (uint8_t* challenge, size_t len), (override));
    MOCK_METHOD(bool, sha256,
                (const uint8_t* data, size_t len, uint8_t* hash),
                (override));
    MOCK_METHOD(bool, verifyECDSA,
                (const uint8_t* publicKey, const uint8_t* hash,
                 const uint8_t* signature, size_t sigLen),
                (override));
    MOCK_METHOD(bool, parseCOSEPublicKey,
                (const uint8_t* coseKey, size_t coseLen, uint8_t* publicKey),
                (override));
    MOCK_METHOD(bool, getRandomBytes, (uint8_t* buffer, size_t len), (override));
    MOCK_METHOD(bool, parseDERSignature,
                (const uint8_t* der, size_t derLen,
                 uint8_t* r, uint8_t* s),
                (override));
};

// ============================================================
// MockUserInterface — driven port: IUserInterface
// ============================================================
class MockUserInterface : public IUserInterface {
public:
    MOCK_METHOD(void, update,        (), (override));
    MOCK_METHOD(void, setIdle,       (), (override));
    MOCK_METHOD(void, showProcessing,(), (override));
    MOCK_METHOD(void, showSuccess,   (), (override));
    MOCK_METHOD(void, showError,     (), (override));
    MOCK_METHOD(void, showCloneAlert,(), (override));
    MOCK_METHOD(void, showLockout,   (), (override));
};

// ============================================================
// MockSystemControl — driven port: ISystemControl
// ============================================================
class MockSystemControl : public ISystemControl {
public:
    MOCK_METHOD(void, delayMs, (uint32_t ms), (override));
    MOCK_METHOD(void, restart, (),            (override));
};

// ============================================================
// MockAccessController — driving port: IAccessController
// ============================================================
class MockAccessController : public IAccessController {
public:
    MOCK_METHOD(bool,     init,               (), (override));
    MOCK_METHOD(void,     grantAccess,        (uint32_t durationMs), (override));
    MOCK_METHOD(void,     denyAccess,         (), (override));
    MOCK_METHOD(void,     emergencyUnlock,    (), (override));
    MOCK_METHOD(void,     lock,               (), (override));
    MOCK_METHOD(bool,     isUnlocked,         (), (override));
    MOCK_METHOD(void,     setLockoutPolicy,   (uint8_t maxAttempts, uint32_t lockoutDuration), (override));
    MOCK_METHOD(void,     recordFailedAttempt,(), (override));
    MOCK_METHOD(bool,     isLockedOut,        (), (override));
    MOCK_METHOD(uint32_t, getLockoutRemaining,(), (override));
    MOCK_METHOD(void,     resetFailedAttempts,(), (override));
    MOCK_METHOD(void,     update,             (), (override));
    MOCK_METHOD(uint8_t,  getFailedAttempts,  (), (override));
};

// ============================================================
// MockCTAP2Handler — driving port: ICTAP2Handler
// ============================================================
class MockCTAP2Handler : public ICTAP2Handler {
public:
    MOCK_METHOD(int, getInfo,
                (INFCReader* nfc, uint8_t* info, size_t* infoLen),
                (override));
    MOCK_METHOD(int, getAssertion,
                (INFCReader* nfc, const char* rpId, uint8_t* clientDataHash,
                 CTAP2Response* response),
                (override));
    MOCK_METHOD(int, makeCredential,
                (INFCReader* nfc, const char* rpId, const char* rpName,
                 uint8_t* userId, uint8_t userIdLen, const char* userName,
                 uint8_t* clientDataHash, uint8_t* credentialId,
                 uint8_t* credentialIdLen, uint8_t* publicKey),
                (override));
};

// ============================================================
// FakeOutputStream — hand-written fake that captures output.
//
// Use this (rather than a strict gmock mock) when tests need to
// inspect what was written to the output stream.  For tests that
// only care about call counts / arguments, derive a gmock mock
// from IOutputStream instead.
// ============================================================
class FakeOutputStream : public IOutputStream {
public:
    void reset() { _buf.clear(); }
    const std::string& output() const { return _buf; }

    void print(const char* s) override   { if (s) _buf += s; }
    void println(const char* s) override { if (s) _buf += s; _buf += '\n'; }

protected:
    void vprintfImpl(const char* fmt, va_list args) override {
        char tmp[512];
        vsnprintf(tmp, sizeof(tmp), fmt, args);
        _buf += tmp;
    }

private:
    std::string _buf;
};

// ============================================================
// FakeSerialInput — hand-written fake that replays queued bytes.
//
// Symmetric to FakeOutputStream: feed() enqueues bytes that the
// SerialCommandHandler will drain via update(), letting the admin
// command channel be exercised on host without a hardware UART.
// ============================================================
class FakeSerialInput : public ISerialInput {
public:
    void feed(const char* s) { if (s) _data += s; }
    void reset() { _data.clear(); _pos = 0; }

    bool available() override { return _pos < _data.size(); }
    int  read() override {
        if (_pos >= _data.size()) return -1;
        return (unsigned char)_data[_pos++];
    }

private:
    std::string _data;
    size_t      _pos = 0;
};

#endif // FIDOLOCK_TEST_MOCKS_MOCK_INTERFACES_H
