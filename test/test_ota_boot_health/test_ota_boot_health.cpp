// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "hal/ota_boot_health.h"

namespace {

using sigurdos::ota_boot_health::HEALTH_WINDOW_MS;
using sigurdos::ota_boot_health::PolicyState;
using sigurdos::ota_boot_health::healthWindowElapsed;
using sigurdos::ota_boot_health::shouldMarkValid;

TEST(OtaBootHealthPolicy, NonOtaBootNeverNeedsValidation) {
    const PolicyState state{false, true, true, 100};
    EXPECT_FALSE(shouldMarkValid(state, 100 + HEALTH_WINDOW_MS));
}

TEST(OtaBootHealthPolicy, RequiresCoreAndCompleteMainLoop) {
    PolicyState state{true, false, false, 100};
    const uint32_t deadline = 100 + HEALTH_WINDOW_MS;

    EXPECT_FALSE(shouldMarkValid(state, deadline));
    state.core_ready = true;
    EXPECT_FALSE(shouldMarkValid(state, deadline));
    state.loop_completed = true;
    EXPECT_TRUE(shouldMarkValid(state, deadline));
}

TEST(OtaBootHealthPolicy, KeepsImagePendingForConfiguredWindow) {
    const PolicyState state{true, true, true, 500};

    EXPECT_FALSE(shouldMarkValid(state, 500 + HEALTH_WINDOW_MS - 1));
    EXPECT_TRUE(shouldMarkValid(state, 500 + HEALTH_WINDOW_MS));
}

TEST(OtaBootHealthPolicy, WindowComparisonHandlesMillisWrap) {
    const uint32_t started = 0xFFFFFF00U;

    EXPECT_FALSE(healthWindowElapsed(started, started + HEALTH_WINDOW_MS - 1));
    EXPECT_TRUE(healthWindowElapsed(started, started + HEALTH_WINDOW_MS));
}

}  // namespace
