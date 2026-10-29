/**
 * FidoLock - Access Controller Unit Tests
 *
 * Tests the REAL AccessController through its driving port (IAccessController).
 * Hardware (time, GPIO) is replaced by hand-written fakes in test_hal.h.
 *
 * Composition root: each TEST_F SetUp() wires AccessController ← TestTimeProvider
 * and TestGPIOController.
 *
 * Hand-written fakes are used (not gmock) because the test needs to control
 * time/GPIO state, not assert call sequences. See test_hal/test_hal.h.
 */

#include <gtest/gtest.h>
#include <stdint.h>

// Hand-written HAL fakes
#include "../test_hal/test_hal.h"

// Real application module under test — config.h provides all pin/timing constants
#include "../../src/application/access/access_controller.h"

// ============================================================
// Test fixture
// ============================================================

class AccessControllerTest : public ::testing::Test {
protected:
    TestTimeProvider timeProvider;
    TestGPIOController gpio;
    AccessController* controller;

    void SetUp() override {
        timeProvider.setTime(0);
        gpio.reset();
        controller = new AccessController(timeProvider, gpio);
        controller->init();
    }

    void TearDown() override {
        delete controller;
        controller = nullptr;
    }
};

// ============================================================
// Initialization
// ============================================================

TEST_F(AccessControllerTest, InitConfiguresRelayPin) {
    EXPECT_EQ(GpioMode::Output, gpio.getPinMode(PIN_RELAY_CTRL));
}

TEST_F(AccessControllerTest, InitLocksRelay) {
    EXPECT_EQ(GpioLevel::Low, gpio.getPinState(PIN_RELAY_CTRL));
}

TEST_F(AccessControllerTest, InitNotUnlocked) {
    EXPECT_FALSE(controller->isUnlocked());
}

TEST_F(AccessControllerTest, InitNotLockedOut) {
    EXPECT_FALSE(controller->isLockedOut());
}

TEST_F(AccessControllerTest, InitZeroFailedAttempts) {
    EXPECT_EQ(0u, controller->getFailedAttempts());
}

// ============================================================
// Grant Access
// ============================================================

TEST_F(AccessControllerTest, GrantAccessActivatesRelay) {
    controller->grantAccess(5000);

    EXPECT_EQ(GpioLevel::High, gpio.getPinState(PIN_RELAY_CTRL));
    EXPECT_TRUE(controller->isUnlocked());
}

TEST_F(AccessControllerTest, GrantAccessBlockedWhenLockedOut) {
    controller->setLockoutPolicy(1, 60000);
    controller->recordFailedAttempt();

    controller->grantAccess(5000);

    EXPECT_EQ(GpioLevel::Low, gpio.getPinState(PIN_RELAY_CTRL));
    EXPECT_FALSE(controller->isUnlocked());
}

TEST_F(AccessControllerTest, GrantAccessResetsFailedAttempts) {
    controller->recordFailedAttempt();
    controller->recordFailedAttempt();
    EXPECT_EQ(2u, controller->getFailedAttempts());

    controller->grantAccess(5000);

    EXPECT_EQ(0u, controller->getFailedAttempts());
}

// ============================================================
// Lock / Unlock
// ============================================================

TEST_F(AccessControllerTest, LockDeactivatesRelay) {
    controller->grantAccess(5000);
    EXPECT_TRUE(controller->isUnlocked());

    controller->lock();

    EXPECT_EQ(GpioLevel::Low, gpio.getPinState(PIN_RELAY_CTRL));
    EXPECT_FALSE(controller->isUnlocked());
}

TEST_F(AccessControllerTest, AutoLockAfterDuration) {
    controller->grantAccess(5000);
    EXPECT_TRUE(controller->isUnlocked());

    timeProvider.advance(4999);
    controller->update();
    EXPECT_TRUE(controller->isUnlocked());

    timeProvider.advance(2);
    controller->update();
    EXPECT_FALSE(controller->isUnlocked());
}

TEST_F(AccessControllerTest, EmergencyUnlockBypassesLockout) {
    controller->setLockoutPolicy(1, 60000);
    controller->recordFailedAttempt();
    EXPECT_TRUE(controller->isLockedOut());

    controller->emergencyUnlock();

    EXPECT_TRUE(controller->isUnlocked());
}

// ============================================================
// Lockout Policy
// ============================================================

TEST_F(AccessControllerTest, LockoutAfterMaxAttempts) {
    controller->setLockoutPolicy(3, 60000);

    controller->recordFailedAttempt();
    EXPECT_FALSE(controller->isLockedOut());

    controller->recordFailedAttempt();
    EXPECT_FALSE(controller->isLockedOut());

    controller->recordFailedAttempt();
    EXPECT_TRUE(controller->isLockedOut());
}

TEST_F(AccessControllerTest, LockoutExpiresAfterDuration) {
    controller->setLockoutPolicy(1, 60000);
    controller->recordFailedAttempt();
    EXPECT_TRUE(controller->isLockedOut());

    timeProvider.advance(59999);
    EXPECT_TRUE(controller->isLockedOut());

    timeProvider.advance(2);
    EXPECT_FALSE(controller->isLockedOut());
    EXPECT_EQ(0u, controller->getFailedAttempts());
}

TEST_F(AccessControllerTest, LockoutRemainingTime) {
    controller->setLockoutPolicy(1, 120000);
    controller->recordFailedAttempt();

    EXPECT_EQ(120000u, controller->getLockoutRemaining());

    timeProvider.advance(30000);
    EXPECT_EQ(90000u, controller->getLockoutRemaining());

    timeProvider.advance(90000);
    EXPECT_EQ(0u, controller->getLockoutRemaining());
}

TEST_F(AccessControllerTest, DenyAccessRecordsFailedAttempt) {
    controller->denyAccess();
    EXPECT_EQ(1u, controller->getFailedAttempts());

    controller->denyAccess();
    EXPECT_EQ(2u, controller->getFailedAttempts());
}

TEST_F(AccessControllerTest, DenyAccessTriggersLockout) {
    controller->setLockoutPolicy(2, 60000);

    controller->denyAccess();
    EXPECT_FALSE(controller->isLockedOut());

    controller->denyAccess();
    EXPECT_TRUE(controller->isLockedOut());
}

TEST_F(AccessControllerTest, ResetFailedAttemptsClearsLockout) {
    controller->setLockoutPolicy(1, 60000);
    controller->recordFailedAttempt();
    EXPECT_TRUE(controller->isLockedOut());

    controller->resetFailedAttempts();

    EXPECT_FALSE(controller->isLockedOut());
    EXPECT_EQ(0u, controller->getFailedAttempts());
}

// ============================================================
// Edge Cases
// ============================================================

TEST_F(AccessControllerTest, MultipleLockoutCycles) {
    controller->setLockoutPolicy(2, 10000);

    controller->recordFailedAttempt();
    controller->recordFailedAttempt();
    EXPECT_TRUE(controller->isLockedOut());

    timeProvider.advance(10001);
    EXPECT_FALSE(controller->isLockedOut());

    controller->recordFailedAttempt();
    controller->recordFailedAttempt();
    EXPECT_TRUE(controller->isLockedOut());
}

TEST_F(AccessControllerTest, VeryLongLockoutDuration) {
    controller->setLockoutPolicy(1, 0xFFFFFFFF);
    controller->recordFailedAttempt();
    EXPECT_TRUE(controller->isLockedOut());

    timeProvider.advance(1000000);
    EXPECT_TRUE(controller->isLockedOut());
}

// ============================================================
// GPIO Verification
// ============================================================

TEST_F(AccessControllerTest, GpioToggledOnAccessCycle) {
    EXPECT_EQ(GpioLevel::Low, gpio.getPinState(PIN_RELAY_CTRL));

    controller->grantAccess(5000);
    EXPECT_EQ(GpioLevel::High, gpio.getPinState(PIN_RELAY_CTRL));

    controller->lock();
    EXPECT_EQ(GpioLevel::Low, gpio.getPinState(PIN_RELAY_CTRL));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
