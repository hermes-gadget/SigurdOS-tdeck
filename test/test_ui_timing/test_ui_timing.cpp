// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include <cstdint>

#include "ui/ui.h"
#include "ui/response_wait_state.h"

namespace {

using sigurdos::ui::UI_SPLASH_DURATION_MS;
using sigurdos::ui::ui_splash_transition_elapsed;

TEST(UITiming, SplashDoesNotTransitionBeforeDelay) {
    EXPECT_FALSE(ui_splash_transition_elapsed(2999, 1000));
}

TEST(UITiming, SplashDoesNotTransitionAtExactDelay) {
    EXPECT_FALSE(ui_splash_transition_elapsed(1000 + UI_SPLASH_DURATION_MS, 1000));
}

TEST(UITiming, SplashTransitionsAfterDelay) {
    EXPECT_TRUE(ui_splash_transition_elapsed(1000 + UI_SPLASH_DURATION_MS + 1, 1000));
}

TEST(UITiming, CustomDurationUsesSameStrictBoundary) {
    EXPECT_FALSE(ui_splash_transition_elapsed(150, 100, 50));
    EXPECT_TRUE(ui_splash_transition_elapsed(151, 100, 50));
}

TEST(UITiming, SplashTimingHandlesMillisRollover) {
    const uint32_t start = UINT32_MAX - 100u;
    EXPECT_FALSE(ui_splash_transition_elapsed(99, start, 200));
    EXPECT_TRUE(ui_splash_transition_elapsed(100, start, 200));
}

TEST(ResponseWaitStateTest, StartsInLoadingPhase) {
    sigurdos::ui::ResponseWaitState state;
    state.ensureStarted(100);
    EXPECT_EQ(state.phase(500, false), sigurdos::ui::ResponseWaitPhase::Loading);
}

TEST(ResponseWaitStateTest, ResponseBecomesReadyBeforeTimeout) {
    sigurdos::ui::ResponseWaitState state;
    state.begin(100, true);
    EXPECT_EQ(state.phase(500, true), sigurdos::ui::ResponseWaitPhase::Ready);
}

TEST(ResponseWaitStateTest, SendFailureIsReportedImmediately) {
    sigurdos::ui::ResponseWaitState state;
    state.begin(100, false);
    EXPECT_EQ(state.phase(100, false), sigurdos::ui::ResponseWaitPhase::SendFailed);
}

TEST(ResponseWaitStateTest, TimesOutAtExactDeadline) {
    sigurdos::ui::ResponseWaitState state;
    state.begin(100, true);
    EXPECT_EQ(state.phase(100 + sigurdos::ui::RESPONSE_WAIT_TIMEOUT_MS - 1, false),
              sigurdos::ui::ResponseWaitPhase::Loading);
    EXPECT_EQ(state.phase(100 + sigurdos::ui::RESPONSE_WAIT_TIMEOUT_MS, false),
              sigurdos::ui::ResponseWaitPhase::TimedOut);
}

TEST(ResponseWaitStateTest, TimeoutMathHandlesMillisRollover) {
    sigurdos::ui::ResponseWaitState state;
    state.begin(UINT32_MAX - 100u, true);
    EXPECT_EQ(state.phase(98, false, 200), sigurdos::ui::ResponseWaitPhase::Loading);
    EXPECT_EQ(state.phase(99, false, 200), sigurdos::ui::ResponseWaitPhase::TimedOut);
}

} // anonymous namespace
