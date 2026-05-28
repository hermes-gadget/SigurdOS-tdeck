// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SlopOS-TDeck.
//
// SlopOS-TDeck is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SlopOS-TDeck is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SlopOS-TDeck.  If not, see <https://www.gnu.org/licenses/>.


/**
 * Integration build test
 * Verifies that all core headers can be included together
 * without conflicts and that key constants are consistent.
 */
#include <gtest/gtest.h>

// Include all core headers
#include "hal/tdeck_pins.h"
#include "hal/tdeck_board.h"
#include "hal/battery.h"
#include "hal/display.h"
#include "hal/touch.h"
#include "hal/keyboard.h"
#include "hal/trackball.h"
#include "hal/gps.h"
#include "hal/sdcard.h"
#include "mesh/mesh_wrapper.h"
#include "ui/theme.h"
#include "ui/navigation.h"
#include "ui/home_screen.h"
#include "ui/chat_screen.h"
#include "ui/screens.h"
#include "ui/ui.h"

namespace {

class BuildIntegrationTest : public ::testing::Test {};

// ── All headers compile together ────────────────────────
TEST_F(BuildIntegrationTest, AllHeadersIncludable) {
    // If we got here, all headers compiled without errors
    SUCCEED();
}

// ── Cross-module constant consistency ───────────────────
TEST_F(BuildIntegrationTest, DisplayDimensionsConsistent) {
    // TFT dimensions should match what LVGL expects
    EXPECT_EQ(TFT_WIDTH, 320);
    EXPECT_EQ(TFT_HEIGHT, 240);
}

// ── Function declarations are consistent across modules ──
TEST_F(BuildIntegrationTest, BatteryAPIExists) {
    using init_fn = void (*)();
    using mv_fn = uint16_t (*)();
    using pct_fn = uint8_t (*)();

    (void)static_cast<init_fn>(slopos_battery_init);
    (void)static_cast<mv_fn>(slopos_battery_mv);
    (void)static_cast<pct_fn>(slopos_battery_pct);
    SUCCEED();
}

TEST_F(BuildIntegrationTest, DisplayAPIExists) {
    using init_fn = bool (*)();
    using loop_fn = void (*)();
    using ms_fn = uint32_t (*)();

    (void)static_cast<init_fn>(slopos_display_init);
    (void)static_cast<loop_fn>(slopos_display_loop);
    (void)static_cast<ms_fn>(slopos_display_millis);
    SUCCEED();
}

// ── Screen count matches between modules ────────────────
TEST_F(BuildIntegrationTest, ScreenCountConsistent) {
    // navigation.h defines screens (Home + app screens)
    EXPECT_EQ((int)slopos::ui::Screen::COUNT, 16);
}

// ── LVGL config sanity ──────────────────────────────────
TEST_F(BuildIntegrationTest, LVGLColorDepthIs16) {
    // Our lv_conf.h sets LV_COLOR_DEPTH to 16
    #ifdef LV_COLOR_DEPTH
    EXPECT_EQ(LV_COLOR_DEPTH, 16);
    #endif
}

// ── T-Deck board uses ESP32 platform ────────────────────
TEST_F(BuildIntegrationTest, ESP32PlatformDefined) {
    // When building for ESP32, ESP32_PLATFORM should be set
    // On native test, it won't be — which is expected
    #ifdef ESP32_PLATFORM
    SUCCEED();
    #else
    GTEST_SKIP() << "ESP32_PLATFORM not defined in native test (expected)";
    #endif
}

} // anonymous namespace
