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
 * Unit tests for home screen icon routing
 *
 * Verifies that each home screen tile navigates to its intended screen.
 * The REPEATERS tile was incorrectly mapped to Screen::Heard (same as PACKETS)
 * instead of Screen::Network (Finder's screen which shows nearby nodes).
 */
#include <gtest/gtest.h>
#include <cstring>
#include "ui/home_screen.h"

namespace {

using sigurdos::ui::home_tile_filters;

TEST(HomeScreenActivationTest, TouchAndTrackballShareStableFilterPolicy) {
    EXPECT_EQ(home_tile_filters(0).chat_filter, 1);
    EXPECT_EQ(home_tile_filters(1).chat_filter, 2);
    EXPECT_TRUE(home_tile_filters(2).rooms_only);
    EXPECT_EQ(home_tile_filters(3).chat_filter, 0);
    EXPECT_FALSE(home_tile_filters(3).rooms_only);
}

static int row_height(int base_h, int extra_h, int row) {
    return base_h + (row < extra_h ? 1 : 0);
}

static int row_origin(int base_h, int gap, int extra_h, int row) {
    const int remainder_offset = row < extra_h ? row : extra_h;
    return row * (base_h + gap) + remainder_offset;
}

// ── Tests ────────────────────────────────────────────────

TEST(HomeScreenRouteTest, ProductionTableContainsEveryTile) {
    EXPECT_EQ(sigurdos::ui::homeRouteCount(), sigurdos::ui::HOME_ROUTE_COUNT);
    for (std::size_t index = 0; index < sigurdos::ui::HOME_ROUTE_COUNT; ++index) {
        EXPECT_NE(sigurdos::ui::homeRouteAt(index), nullptr) << index;
    }
    EXPECT_EQ(sigurdos::ui::homeRouteAt(sigurdos::ui::HOME_ROUTE_COUNT), nullptr);
}

TEST(HomeScreenRouteTest, ProductionTargetsMatchUserFacingRoutes) {
    using sigurdos::ui::Screen;
    EXPECT_EQ(sigurdos::ui::findHomeRoute("CHATS")->target, Screen::Chat);
    EXPECT_EQ(sigurdos::ui::findHomeRoute("DMs")->target, Screen::Chat);
    EXPECT_EQ(sigurdos::ui::findHomeRoute("ROOMS")->target, Screen::Contacts);
    EXPECT_EQ(sigurdos::ui::findHomeRoute("CONTACTS")->target, Screen::Contacts);
    EXPECT_EQ(sigurdos::ui::findHomeRoute("REPEATERS")->target, Screen::Repeaters);
    EXPECT_EQ(sigurdos::ui::findHomeRoute("PACKETS")->target, Screen::Heard);
    EXPECT_EQ(sigurdos::ui::findHomeRoute("MAP")->target, Screen::Map);
    EXPECT_EQ(sigurdos::ui::findHomeRoute("SETTINGS")->target, Screen::Settings);
    EXPECT_EQ(sigurdos::ui::findHomeRoute("SIGNAL")->target, Screen::Signal);
    EXPECT_EQ(sigurdos::ui::findHomeRoute("missing"), nullptr);
}

TEST(HomeScreenRouteTest, ProductionTableOwnsFilterPolicy) {
    EXPECT_EQ(sigurdos::ui::findHomeRoute("CHATS")->filters.chat_filter, 1);
    EXPECT_EQ(sigurdos::ui::findHomeRoute("DMs")->filters.chat_filter, 2);
    EXPECT_TRUE(sigurdos::ui::findHomeRoute("ROOMS")->filters.rooms_only);
    EXPECT_FALSE(sigurdos::ui::findHomeRoute("CONTACTS")->filters.rooms_only);
}

TEST(HomeScreenGridTest, SingleRemainderPixelOffsetsEveryFollowingRow) {
    constexpr int base_h = 62;
    constexpr int gap = 3;
    constexpr int extra_h = 1;

    EXPECT_EQ(row_origin(base_h, gap, extra_h, 0), 0);
    EXPECT_EQ(row_origin(base_h, gap, extra_h, 1), 66);
    EXPECT_EQ(row_origin(base_h, gap, extra_h, 2), 131);
    for (int row = 0; row < 2; ++row) {
        EXPECT_EQ(row_origin(base_h, gap, extra_h, row + 1),
                  row_origin(base_h, gap, extra_h, row) +
                      row_height(base_h, extra_h, row) + gap);
    }
}

TEST(HomeScreenGridTest, MultipleRemainderPixelsAccumulateByRow) {
    constexpr int base_h = 10;
    constexpr int gap = 3;
    constexpr int extra_h = 2;

    EXPECT_EQ(row_origin(base_h, gap, extra_h, 0), 0);
    EXPECT_EQ(row_origin(base_h, gap, extra_h, 1), 14);
    EXPECT_EQ(row_origin(base_h, gap, extra_h, 2), 28);
    EXPECT_EQ(row_origin(base_h, gap, extra_h, 3), 41);
}

TEST(HomeScreenBadgeTest, IndicatesUnreadOverflow) {
    char text[8];
    EXPECT_TRUE(sigurdos::ui::home_screen_format_unread_badge(text, sizeof(text), 99));
    EXPECT_STREQ(text, "99");
    EXPECT_TRUE(sigurdos::ui::home_screen_format_unread_badge(text, sizeof(text), 100));
    EXPECT_STREQ(text, "99+");
}

} // anonymous namespace
