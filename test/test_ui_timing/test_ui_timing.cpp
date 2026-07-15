// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include <cstdint>

#include "ui/ui.h"

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

} // anonymous namespace
