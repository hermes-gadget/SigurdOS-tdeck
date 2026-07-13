// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include <cstdint>

#include "ui/ui.h"
#include "ui/finder_refresh_state.h"

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

TEST(FinderRefreshStateTest, ActiveSnapshotTracksElapsedSecondAndResults) {
    const auto start = sigurdos::ui::finder_refresh_snapshot(true, 3000, true, 30000, 0);
    const auto one_second = sigurdos::ui::finder_refresh_snapshot(true, 1999, true, 29000, 1);

    EXPECT_EQ(start.phase, sigurdos::ui::FinderRefreshPhase::Active);
    EXPECT_EQ(start.displayed_seconds, 0u);
    EXPECT_TRUE(start.needsPolling());
    EXPECT_EQ(one_second.displayed_seconds, 1u);
    EXPECT_EQ(one_second.result_count, 1);
    EXPECT_NE(start, one_second);
}

TEST(FinderRefreshStateTest, CooldownSnapshotUsesCeilingSeconds) {
    const auto snapshot = sigurdos::ui::finder_refresh_snapshot(false, 0, true, 29001, 2);
    EXPECT_EQ(snapshot.phase, sigurdos::ui::FinderRefreshPhase::Cooldown);
    EXPECT_EQ(snapshot.displayed_seconds, 30u);
    EXPECT_TRUE(snapshot.needsPolling());
}

TEST(FinderRefreshStateTest, ReadySnapshotStopsPolling) {
    const auto snapshot = sigurdos::ui::finder_refresh_snapshot(false, 0, false, 0, 2);
    EXPECT_EQ(snapshot.phase, sigurdos::ui::FinderRefreshPhase::Ready);
    EXPECT_EQ(snapshot.displayed_seconds, 0u);
    EXPECT_FALSE(snapshot.needsPolling());
}

} // anonymous namespace
