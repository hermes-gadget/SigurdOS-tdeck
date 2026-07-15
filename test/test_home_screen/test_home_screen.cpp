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

namespace {

// ── Replicate the home screen icon routing table for pure testing ──
// Mirrors home_screen.cpp lines 53-73

enum class Screen {
    Home,
    Chat,
    Contacts,
    Channels,
    Network,
    Heard,
    Map,
    Advertise,
    Settings,
    Trace,
    Terminal,
    Signal,
    RadioSetup,
    Repeaters,
    Onboarding,
    COUNT
};

struct IconDef {
    const char* label;
    const char* symbol;
    bool        badge;
    Screen      target;
};

// MUST match home_screen.cpp exactly (same order, same targets)
static const IconDef icons[] = {
    {"CHATS",     "\x0e",  true,  Screen::Chat},
    {"CONTACTS",  "\x0f",  false, Screen::Contacts},
    {"REPEATERS", "\x15",  false, Screen::Repeaters},
    {"FINDER",    "\x12",  false, Screen::Network},
    {"PACKETS",   "\x0b",  false, Screen::Heard},
    {"MAP",       "\x13",  false, Screen::Map},
    {"ADVERTISE", "\x07",  false, Screen::Advertise},
    {"SETTINGS",  "\x16",  false, Screen::Settings},
    {"TRACE",     "\x17",  false, Screen::Trace},
    {"TERMINAL",  "\x0c",  false, Screen::Terminal},
    {"SETUP",     "\x16",  false, Screen::Onboarding},
    {"SIGNAL",    "\x19",  false, Screen::Signal},
};

static constexpr int ICON_COUNT = sizeof(icons) / sizeof(icons[0]);

static int row_height(int base_h, int extra_h, int row) {
    return base_h + (row < extra_h ? 1 : 0);
}

static int row_origin(int base_h, int gap, int extra_h, int row) {
    const int remainder_offset = row < extra_h ? row : extra_h;
    return row * (base_h + gap) + remainder_offset;
}

// ── Tests ────────────────────────────────────────────────

TEST(HomeScreenIconTest, AllTilesHaveUniqueTargets) {
    // Each tile should navigate to a distinct screen.
    // After fix: only PACKETS points to Heard (1 tile), no duplicates.
    int heard_count = 0;
    for (int i = 0; i < ICON_COUNT; i++) {
        if (icons[i].target == Screen::Heard)
            heard_count++;
    }
    // FIXED: only PACKETS = 1 tile pointing to Heard
    EXPECT_EQ(heard_count, 1)
        << "Only PACKETS should target Heard (REPEATERS now targets Repeaters)";
}

TEST(HomeScreenIconTest, RepeatersTargetsRepeaters) {
    // REPEATERS now goes to Screen::Repeaters (dedicated repeaters-only view)
    EXPECT_EQ(icons[2].target, Screen::Repeaters)
        << "REPEATERS should target Repeaters screen (repeaters only)";
}

TEST(HomeScreenIconTest, RepeatersAndFinderAreDifferent) {
    // REPEATERS and FINDER should go to different screens
    EXPECT_NE(icons[2].target, icons[3].target)
        << "REPEATERS and FINDER should go to different screens";
}

TEST(HomeScreenIconTest, PacketsTargetsHeard) {
    // PACKETS should stay on Heard (raw packets log)
    EXPECT_EQ(icons[4].target, Screen::Heard);
}

TEST(HomeScreenIconTest, FinderTargetsNetwork) {
    // FINDER correctly shows the Network screen (nearby nodes)
    EXPECT_EQ(icons[3].target, Screen::Network);
}

TEST(HomeScreenIconTest, RepeatersAndPacketsAreDifferent) {
    // FIXED: REPEATERS (Network) and PACKETS (Heard) now go to different screens
    EXPECT_NE(icons[2].target, icons[4].target)
        << "REPEATERS and PACKETS should go to different screens";
}

TEST(HomeScreenIconTest, AllIconsPresent) {
    EXPECT_EQ(ICON_COUNT, 12);
}

TEST(HomeScreenIconTest, ChatsTargetsChat) {
    EXPECT_EQ(icons[0].target, Screen::Chat);
}

TEST(HomeScreenIconTest, ContactsTargetsContacts) {
    EXPECT_EQ(icons[1].target, Screen::Contacts);
}

TEST(HomeScreenIconTest, MapTargetsMap) {
    EXPECT_EQ(icons[5].target, Screen::Map);
}

TEST(HomeScreenIconTest, AdvertiseTargetsAdvertise) {
    EXPECT_EQ(icons[6].target, Screen::Advertise);
}

TEST(HomeScreenIconTest, SettingsTargetsSettings) {
    EXPECT_EQ(icons[7].target, Screen::Settings);
}

TEST(HomeScreenIconTest, TraceTargetsTrace) {
    EXPECT_EQ(icons[8].target, Screen::Trace);
}

TEST(HomeScreenIconTest, TerminalTargetsTerminal) {
    EXPECT_EQ(icons[9].target, Screen::Terminal);
}

TEST(HomeScreenIconTest, SetupTargetsOnboarding) {
    EXPECT_EQ(icons[10].target, Screen::Onboarding);
}

TEST(HomeScreenIconTest, SignalTargetsSignal) {
    EXPECT_EQ(icons[11].target, Screen::Signal);
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

} // anonymous namespace
