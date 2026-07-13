// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.


/**
 * Unit tests for theme color constants
 * Validates the Discord-inspired dark palette
 */
#include <gtest/gtest.h>
#include <cstdint>
#include <cmath>

// Include the actual theme header (compile-time constant validation)
#include "ui/theme.h"

namespace {

class ThemeTest : public ::testing::Test {};

// ── Background colors are dark ──────────────────────────
TEST_F(ThemeTest, BackgroundColorsAreDark) {
    using namespace sigurdos::theme;
    // All background colors should have low brightness (< 0x30 per channel)
    auto is_dark = [](uint32_t c) {
        return ((c >> 16) & 0xFF) < 0x30 &&
               ((c >> 8)  & 0xFF) < 0x60 &&
               (c & 0xFF)        <= 0x60;
    };
    EXPECT_TRUE(is_dark(BG_PRIMARY));
    EXPECT_TRUE(is_dark(BG_SECONDARY));
    EXPECT_TRUE(is_dark(BG_TERTIARY));
    EXPECT_TRUE(is_dark(BG_INPUT));
}

// ── Accent colors are vibrant ───────────────────────────
TEST_F(ThemeTest, AccentColorsAreBright) {
    using namespace sigurdos::theme;
    // Accent colors should have at least one channel > 0xA0
    auto is_vibrant = [](uint32_t c) {
        return ((c >> 16) & 0xFF) > 0xA0 ||
               ((c >> 8)  & 0xFF) > 0xA0 ||
               (c & 0xFF)        > 0xA0;
    };
    EXPECT_TRUE(is_vibrant(ACCENT));
    EXPECT_TRUE(is_vibrant(ACCENT_GREEN));
    EXPECT_TRUE(is_vibrant(ACCENT_RED));
    EXPECT_TRUE(is_vibrant(ACCENT_ORANGE));
}

// ── Text colors are readable ─────────────────────────────
TEST_F(ThemeTest, TextColorsAreLight) {
    using namespace sigurdos::theme;
    auto is_light = [](uint32_t c) {
        int sum = ((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF);
        return sum > 350; // average > ~117 per channel
    };
    EXPECT_TRUE(is_light(TEXT_PRIMARY));
}

TEST_F(ThemeTest, MutedTextIsDimmerThanPrimary) {
    using namespace sigurdos::theme;
    auto brightness = [](uint32_t c) {
        return ((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF);
    };
    EXPECT_GT(brightness(TEXT_PRIMARY), brightness(TEXT_SECONDARY));
    EXPECT_GT(brightness(TEXT_SECONDARY), brightness(TEXT_MUTED));
}

TEST_F(ThemeTest, MutedTextHasReadableDarkSurfaceContrast) {
    using namespace sigurdos::theme;
    auto channel = [](uint8_t v) -> double {
        double c = v / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    auto luminance = [&](uint32_t c) -> double {
        return 0.2126 * channel((c >> 16) & 0xFF)
             + 0.7152 * channel((c >> 8) & 0xFF)
             + 0.0722 * channel(c & 0xFF);
    };
    auto contrast = [&](uint32_t a, uint32_t b) -> double {
        double la = luminance(a);
        double lb = luminance(b);
        double hi = la > lb ? la : lb;
        double lo = la > lb ? lb : la;
        return (hi + 0.05) / (lo + 0.05);
    };

    EXPECT_GE(contrast(TEXT_MUTED, BG_PRIMARY), 4.5);
    EXPECT_GE(contrast(TEXT_MUTED, BG_SECONDARY), 4.5);
    EXPECT_GE(contrast(TEXT_MUTED, BG_TERTIARY), 4.5);
    EXPECT_GE(contrast(TEXT_MUTED, BG_INPUT), 4.5);
}

TEST_F(ThemeTest, AccentForegroundMeetsNormalTextContrastForEveryTheme) {
    using namespace sigurdos::theme;
    auto channel = [](uint8_t v) -> double {
        const double c = v / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    auto luminance = [&](uint32_t c) -> double {
        return 0.2126 * channel((c >> 16) & 0xFF)
             + 0.7152 * channel((c >> 8) & 0xFF)
             + 0.0722 * channel(c & 0xFF);
    };
    auto contrast = [&](uint32_t a, uint32_t b) -> double {
        const double la = luminance(a);
        const double lb = luminance(b);
        const double hi = la > lb ? la : lb;
        const double lo = la > lb ? lb : la;
        return (hi + 0.05) / (lo + 0.05);
    };

    for (const auto& theme : THEMES) {
        const uint32_t foreground = contrast_foreground(theme.accent);
        EXPECT_TRUE(foreground == 0x000000 || foreground == 0xFFFFFF);
        EXPECT_GE(contrast(foreground, theme.accent), 4.5) << theme.name;
    }
}

// ── All constants are non-zero ──────────────────────────
TEST_F(ThemeTest, AllConstantsNonZero) {
    using namespace sigurdos::theme;
    EXPECT_NE(BG_PRIMARY,     0U);
    EXPECT_NE(BG_SECONDARY,   0U);
    EXPECT_NE(BG_TERTIARY,    0U);
    EXPECT_NE(BG_INPUT,       0U);
    EXPECT_NE(ACCENT,         0U);
    EXPECT_NE(ACCENT_HOVER,   0U);
    EXPECT_NE(ACCENT_GREEN,   0U);
    EXPECT_NE(ACCENT_RED,     0U);
    EXPECT_NE(ACCENT_ORANGE,  0U);
    EXPECT_NE(ACCENT_YELLOW,  0U);
    EXPECT_NE(TEXT_PRIMARY,   0U);
    EXPECT_NE(TEXT_SECONDARY, 0U);
    EXPECT_NE(TEXT_MUTED,     0U);
    EXPECT_NE(TEXT_LINK,      0U);
    EXPECT_NE(CHANNEL_HASH,   0U);
    EXPECT_NE(CHANNEL_ACTIVE, 0U);
}

// ── Color uniqueness ────────────────────────────────────
TEST_F(ThemeTest, ThemeApplyUpdatesRuntimeColors) {
    using namespace sigurdos::theme;

    theme_apply(2);
    EXPECT_EQ(BG_PRIMARY, THEMES[2].bg_primary);
    EXPECT_EQ(BG_SECONDARY, THEMES[2].bg_secondary);
    EXPECT_EQ(BG_TERTIARY, THEMES[2].bg_tertiary);
    EXPECT_EQ(BG_INPUT, THEMES[2].bg_input);
    EXPECT_EQ(ACCENT, THEMES[2].accent);
    EXPECT_EQ(ACCENT_HOVER, THEMES[2].accent_hover);
    EXPECT_EQ(ACCENT_FOREGROUND, contrast_foreground(THEMES[2].accent));
    EXPECT_EQ(CHANNEL_HASH, THEMES[2].channel_hash);

    theme_apply(255);
    EXPECT_EQ(BG_PRIMARY, THEMES[0].bg_primary);
    EXPECT_EQ(ACCENT, THEMES[0].accent);
    EXPECT_EQ(ACCENT_FOREGROUND, contrast_foreground(THEMES[0].accent));
    EXPECT_EQ(CHANNEL_HASH, THEMES[0].channel_hash);
}

TEST_F(ThemeTest, AccentColorsAreDistinct) {
    using namespace sigurdos::theme;
    EXPECT_NE(ACCENT, ACCENT_GREEN);
    EXPECT_NE(ACCENT, ACCENT_RED);
    EXPECT_NE(ACCENT_GREEN, ACCENT_RED);
    EXPECT_NE(ACCENT_ORANGE, ACCENT_YELLOW);
}

TEST_F(ThemeTest, BackgroundColorsAreDistinct) {
    using namespace sigurdos::theme;
    EXPECT_NE(BG_PRIMARY, BG_SECONDARY);
    EXPECT_NE(BG_PRIMARY, BG_TERTIARY);
    EXPECT_NE(BG_SECONDARY, BG_TERTIARY);
}

} // anonymous namespace
