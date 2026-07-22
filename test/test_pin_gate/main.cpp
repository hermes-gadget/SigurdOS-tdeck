// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include "ui/pin_gate_policy.h"

using namespace sigurdos::ui;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(PinGatePolicyTest, EscalationPersistsUntilSuccessfulUnlock) {
    PinGateState state;
    pinGateRecordFailure(state, 10);
    pinGateRecordFailure(state, 20);
    EXPECT_EQ(state.failures, 2);
    EXPECT_EQ(state.lockout_ms, 1000U);
    EXPECT_TRUE(pinGateLocked(state, 25));
    EXPECT_FALSE(pinGateLocked(state, 1020));
    EXPECT_EQ(state.failures, 2);

    pinGateRecordSuccess(state, 1021);
    EXPECT_EQ(state.failures, 0);
    EXPECT_TRUE(pinGateGraceActive(state, 1022, 300000));
}

TEST(PinValuePolicyTest, RejectsZeroPinAndRequiresConfirmation) {
    EXPECT_FALSE(pin_value_valid("0000"));
    EXPECT_FALSE(pin_value_valid("0123"));
    EXPECT_FALSE(pin_value_valid("123"));
    EXPECT_FALSE(pin_value_valid("1234567"));
    EXPECT_FALSE(pin_value_valid("12a4"));
    EXPECT_TRUE(pin_value_valid("1234"));
    EXPECT_TRUE(pin_value_valid("12345"));
    EXPECT_TRUE(pin_value_valid("123456"));
    EXPECT_FALSE(pin_confirmation_valid("1234", "1235"));
    EXPECT_TRUE(pin_confirmation_valid("1234", "1234"));
    EXPECT_TRUE(pin_confirmation_valid("123456", "123456"));
}

TEST(PinModalInputTest, TrackballNavigatesAndActivatesModalControls) {
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::Up), PinModalAction::FocusPrevious);
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::Left), PinModalAction::FocusPrevious);
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::Down), PinModalAction::FocusNext);
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::Right), PinModalAction::FocusNext);
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::Click), PinModalAction::Activate);
    EXPECT_EQ(pin_modal_action(SigurdOSTrackballEvent::None), PinModalAction::None);
}
