// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include "ui/pin_gate_policy.h"

using namespace sigurdos::ui;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(PinAttemptStateTest, ReentryDoesNotResetFailures) {
    PinAttemptState state;
    state.record_failure(10);
    state.record_failure(20);
    EXPECT_EQ(state.remaining(), 1);
    EXPECT_TRUE(state.can_attempt(25));
    EXPECT_EQ(state.remaining(), 1);
}

TEST(PinAttemptStateTest, ThirdFailureLocksUntilCooldown) {
    PinAttemptState state;
    state.record_failure(10);
    state.record_failure(20);
    state.record_failure(30);
    EXPECT_FALSE(state.can_attempt(30 + PIN_LOCKOUT_MS - 1));
    EXPECT_TRUE(state.can_attempt(30 + PIN_LOCKOUT_MS));
    EXPECT_EQ(state.remaining(), PIN_MAX_FAILURES);
}

TEST(PinAttemptStateTest, CooldownComparisonHandlesMillisWrap) {
    PinAttemptState state;
    state.record_failure(UINT32_MAX - 20);
    state.record_failure(UINT32_MAX - 10);
    state.record_failure(UINT32_MAX - 5);
    EXPECT_FALSE(state.can_attempt(10));
    EXPECT_TRUE(state.can_attempt(PIN_LOCKOUT_MS));
}

TEST(PinAttemptStateTest, SuccessResetRestoresBudget) {
    PinAttemptState state;
    state.record_failure(1);
    state.reset();
    EXPECT_EQ(state.remaining(), PIN_MAX_FAILURES);
}

TEST(PinValuePolicyTest, RejectsZeroPinAndRequiresConfirmation) {
    EXPECT_FALSE(pin_value_valid("0000"));
    EXPECT_FALSE(pin_value_valid("123"));
    EXPECT_FALSE(pin_value_valid("12a4"));
    EXPECT_TRUE(pin_value_valid("1234"));
    EXPECT_FALSE(pin_confirmation_valid("1234", "1235"));
    EXPECT_TRUE(pin_confirmation_valid("1234", "1234"));
}

TEST(PinModalInputTest, TrackballNavigatesAndActivatesModalControls) {
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::Up), PinModalAction::FocusPrevious);
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::Left), PinModalAction::FocusPrevious);
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::Down), PinModalAction::FocusNext);
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::Right), PinModalAction::FocusNext);
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::Click), PinModalAction::Activate);
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::None), PinModalAction::None);
}
