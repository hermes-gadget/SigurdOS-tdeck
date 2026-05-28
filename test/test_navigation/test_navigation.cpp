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
 * Unit tests for screen navigation state machine
 * Tests: routing correctness, back navigation with history stack, same-screen guard
 *
 * NOTE: These tests validate the navigation logic by replicating
 * the stack-based history behavior. Full LVGL integration is tested
 * on-hardware since the mock layer doesn't render screens.
 */
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

// ── Replicate the navigation state machine for pure testing ──
enum class Screen {
    Home, Chat, Contacts, Channels, Network, Heard,
    Map, Advertise, Settings, Trace, Terminal,
    Signal, RadioSetup, Repeaters, Onboarding, ContactDetail, COUNT
};

enum class SlopOSEvent {
    Left, Right, Up, Down, Click
};

static Screen current = Screen::Home;

// Stack-based history (matches navigation.cpp)
static constexpr int MAX_HISTORY = 8;
static Screen history[MAX_HISTORY];
static int   history_top = -1;
static std::vector<std::string> nav_log;

// Two-swipe back state
static int back_swipe_commit = 0;

static void push_history(Screen s) {
    if (history_top < MAX_HISTORY - 1) {
        // Normal case: room on the stack
        history_top++;
        history[history_top] = s;
    } else {
        // Stack full: drop the oldest entry by shifting everything left
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            history[i] = history[i + 1];
        }
        history[MAX_HISTORY - 1] = s;
    }
}

static Screen pop_history() {
    if (history_top < 0) return Screen::Home;
    Screen s = history[history_top];
    history_top--;
    return s;
}

static bool history_empty() {
    return history_top < 0;
}

static bool can_go_back() {
    return !history_empty();
}

void navigate_to(Screen screen) {
    if (screen == current) return;
    back_swipe_commit = 0;
    push_history(current);
    current = screen;
    nav_log.push_back("nav:" + std::to_string((int)screen));
}

void go_back() {
    if (history_empty()) return;
    back_swipe_commit = 0;
    Screen target = pop_history();
    current = target;
    nav_log.push_back("back:" + std::to_string((int)target));
}

bool handle_back_swipe(SlopOSEvent event) {
    if (event != SlopOSEvent::Left) {
        back_swipe_commit = 0;
        return false;
    }

    back_swipe_commit++;
    if (back_swipe_commit >= 2) {
        back_swipe_commit = 0;
        go_back();
        return true;
    }

    return true; // first swipe consumed
}

void reset_nav() {
    current = Screen::Home;
    history_top = -1;
    back_swipe_commit = 0;
    nav_log.clear();
}

class NavigationTest : public ::testing::Test {
protected:
    void SetUp() override { reset_nav(); }
};

// ── Initial state ───────────────────────────────────────
TEST_F(NavigationTest, InitialStateIsHome) {
    EXPECT_EQ(current, Screen::Home);
    EXPECT_TRUE(history_empty());
    EXPECT_FALSE(can_go_back());
    EXPECT_TRUE(nav_log.empty());
}

// ── Forward navigation ──────────────────────────────────
TEST_F(NavigationTest, NavigateToChatUpdatesState) {
    navigate_to(Screen::Chat);
    EXPECT_EQ(current, Screen::Chat);
    EXPECT_FALSE(history_empty());
    EXPECT_TRUE(can_go_back());
    EXPECT_EQ(nav_log.size(), 1u);
}

TEST_F(NavigationTest, NavigateToSameScreenIsNoop) {
    navigate_to(Screen::Home);  // already home
    EXPECT_EQ(current, Screen::Home);
    EXPECT_TRUE(history_empty()); // no push
    EXPECT_FALSE(can_go_back());
    EXPECT_TRUE(nav_log.empty());
}

TEST_F(NavigationTest, NavigateToAllScreens) {
    std::vector<Screen> screens = {
        Screen::Chat, Screen::Contacts, Screen::Channels, Screen::Network,
        Screen::Heard, Screen::Map, Screen::Advertise, Screen::Settings,
        Screen::Trace, Screen::Terminal, Screen::Signal
    };

    for (auto s : screens) {
        reset_nav();
        navigate_to(s);
        EXPECT_EQ(current, s);
        EXPECT_FALSE(history_empty()); // Home pushed onto stack
    }
}

// ── Back navigation (stack-based) ────────────────────────
TEST_F(NavigationTest, GoBackReturnsToPrevious) {
    navigate_to(Screen::Chat);
    EXPECT_TRUE(can_go_back());
    go_back();
    EXPECT_EQ(current, Screen::Home);
    EXPECT_FALSE(can_go_back());
}

TEST_F(NavigationTest, GoBackFromHomeIsNoop) {
    go_back(); // stack empty, nowhere to go
    EXPECT_EQ(current, Screen::Home);
    EXPECT_FALSE(can_go_back());
}

TEST_F(NavigationTest, DeepNavigationAndBack) {
    // Home → Chat → Settings → Terminal → back ×3 → Home
    navigate_to(Screen::Chat);      // stack: [Home]
    navigate_to(Screen::Settings);  // stack: [Home, Chat]
    navigate_to(Screen::Terminal);  // stack: [Home, Chat, Settings]
    EXPECT_EQ(current, Screen::Terminal);

    go_back();  // pop Settings → current=Settings
    EXPECT_EQ(current, Screen::Settings);

    go_back();  // pop Chat → current=Chat
    EXPECT_EQ(current, Screen::Chat);

    go_back();  // pop Home → current=Home
    EXPECT_EQ(current, Screen::Home);

    // Stack should be empty now
    EXPECT_TRUE(history_empty());
    EXPECT_FALSE(can_go_back());
}

// ── Rapid navigation ────────────────────────────────────
TEST_F(NavigationTest, RapidNavigationDoesNotLoseState) {
    for (int i = 0; i < 100; i++) {
        Screen s = (Screen)((i % 12) + 1); // cycle through all screens
        navigate_to(s);
    }
    // Should still have a valid state
    EXPECT_NE(current, Screen::COUNT);
}

// ── Stack overflow — linear stack drops oldest ───────────
TEST_F(NavigationTest, HistoryStackDropsOldestOnOverflow) {
    // Fill the stack with 10 entries (more than MAX_HISTORY=8)
    // Linear stack should drop the oldest (Home) and keep the newest 8
    navigate_to(Screen::Chat);      // push Home (1)
    navigate_to(Screen::Contacts);  // push Chat (2)
    navigate_to(Screen::Channels);  // push Contacts (3)
    navigate_to(Screen::Network);   // push Channels (4)
    navigate_to(Screen::Heard);     // push Network (5)
    navigate_to(Screen::Map);       // push Heard (6)
    navigate_to(Screen::Advertise); // push Map (7)
    navigate_to(Screen::Settings);  // push Advertise (8) — stack full
    navigate_to(Screen::Trace);     // push Settings → shift oldest (Home) out (9)
    navigate_to(Screen::Terminal);  // push Trace → shift oldest (Chat) out (10)
    EXPECT_EQ(current, Screen::Terminal);

    // Go back should trace through the last 8 screens in order
    go_back(); EXPECT_EQ(current, Screen::Trace);
    go_back(); EXPECT_EQ(current, Screen::Settings);
    go_back(); EXPECT_EQ(current, Screen::Advertise);
    go_back(); EXPECT_EQ(current, Screen::Map);
    go_back(); EXPECT_EQ(current, Screen::Heard);
    go_back(); EXPECT_EQ(current, Screen::Network);
    go_back(); EXPECT_EQ(current, Screen::Channels);
    go_back(); EXPECT_EQ(current, Screen::Contacts);
    // Should be exhausted now (home was dropped)
    EXPECT_FALSE(can_go_back());
}

// ── Stack overflow preserves back navigation count ───────
TEST_F(NavigationTest, OverflowBackSequenceFullCount) {
    // Navigate through 12 screens (well over 8-slot buffer)
    navigate_to(Screen::Chat);      // push Home (1)
    navigate_to(Screen::Contacts);  // push Chat (2)
    navigate_to(Screen::Channels);  // push Contacts (3)
    navigate_to(Screen::Network);   // push Channels (4)
    navigate_to(Screen::Heard);     // push Network (5)
    navigate_to(Screen::Map);       // push Heard (6)
    navigate_to(Screen::Advertise); // push Map (7)
    navigate_to(Screen::Settings);  // push Advertise (8) — stack full
    navigate_to(Screen::Trace);     // push Settings → shift oldest out (9)
    navigate_to(Screen::Terminal);  // push Trace (10)
    navigate_to(Screen::Signal);    // push Terminal (11)
    navigate_to(Screen::RadioSetup);// push Signal (12)

    // Should always have exactly MAX_HISTORY back steps available
    int back_count = 0;
    while (can_go_back()) {
        go_back();
        back_count++;
    }
    EXPECT_EQ(back_count, MAX_HISTORY);
}

// ── Navigation to every screen from every screen ────────
TEST_F(NavigationTest, AllScreenPairsWork) {
    for (int from = 0; from < (int)Screen::COUNT; from++) {
        for (int to = 0; to < (int)Screen::COUNT; to++) {
            reset_nav();
            navigate_to((Screen)from);
            navigate_to((Screen)to);
            if (from == to) {
                EXPECT_EQ(current, (Screen)from);
            } else {
                EXPECT_EQ(current, (Screen)to);
                // Stack should have: Home (if from != Home) + from
                EXPECT_FALSE(history_empty());
            }
        }
    }
}

// ── Screen count matches expected ───────────────────────
TEST_F(NavigationTest, ScreenCountIs16) {
    EXPECT_EQ((int)Screen::COUNT, 16);
}

// ── Screen enum values are contiguous ───────────────────
TEST_F(NavigationTest, ScreenEnumValuesAreContiguous) {
    EXPECT_EQ((int)Screen::Home, 0);
    EXPECT_EQ((int)Screen::Chat, 1);
    EXPECT_EQ((int)Screen::Signal, 11);
    EXPECT_EQ((int)Screen::COUNT, 16);
}

// ── Back-swipe (two-swipe commit) ─────────────────────────
TEST_F(NavigationTest, SingleLeftSwipeIsConsumedNoBack) {
    navigate_to(Screen::Chat);
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));
    // First swipe consumed but no navigation
    EXPECT_EQ(current, Screen::Chat);
    EXPECT_TRUE(can_go_back());
    EXPECT_EQ(nav_log.size(), 1u); // only the forward nav
}

TEST_F(NavigationTest, TwoLeftSwipesTriggersGoBack) {
    navigate_to(Screen::Chat);
    handle_back_swipe(SlopOSEvent::Left);  // first: neutralise
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));  // second: navigate back
    EXPECT_EQ(current, Screen::Home);
    EXPECT_EQ(nav_log.size(), 2u); // nav + back
}

TEST_F(NavigationTest, NonLeftEventResetsBackSwipeCounter) {
    navigate_to(Screen::Chat);
    handle_back_swipe(SlopOSEvent::Left);  // first: neutralise
    handle_back_swipe(SlopOSEvent::Up);    // resets counter
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));  // first again after reset
    EXPECT_EQ(current, Screen::Chat); // still on Chat
    // Second swipe now should work
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));
    EXPECT_EQ(current, Screen::Home);
}

TEST_F(NavigationTest, NavigateToResetsBackSwipeCounter) {
    navigate_to(Screen::Chat);
    handle_back_swipe(SlopOSEvent::Left);  // first swipe
    navigate_to(Screen::Settings);         // should reset counter
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));  // first swipe on Settings
    EXPECT_EQ(current, Screen::Settings);
}

TEST_F(NavigationTest, GoBackResetsBackSwipeCounter) {
    navigate_to(Screen::Chat);
    navigate_to(Screen::Settings);
    handle_back_swipe(SlopOSEvent::Left);  // first swipe
    // Second swipe triggers go_back AND resets counter
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));
    EXPECT_EQ(current, Screen::Chat); // went back from Settings
    // Counter is 0 after go_back, so next Left is a fresh first swipe
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));
    EXPECT_EQ(current, Screen::Chat); // first swipe on Chat, still here
}

TEST_F(NavigationTest, BackSwipeFromHomeDoesNothing) {
    // On Home: can_go_back() is false, handle_back_swipe should not crash
    // First swipe consumed, no-op
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));
    EXPECT_EQ(current, Screen::Home);
    // Second swipe: go_back() check history_empty, returns early
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));
    EXPECT_EQ(current, Screen::Home);
}

TEST_F(NavigationTest, UpDownRightEventsDontTriggerBack) {
    navigate_to(Screen::Chat);
    EXPECT_FALSE(handle_back_swipe(SlopOSEvent::Up));
    EXPECT_FALSE(handle_back_swipe(SlopOSEvent::Down));
    EXPECT_FALSE(handle_back_swipe(SlopOSEvent::Right));
    EXPECT_FALSE(handle_back_swipe(SlopOSEvent::Click));
    EXPECT_EQ(current, Screen::Chat);
    EXPECT_EQ(nav_log.size(), 1u);
}

TEST_F(NavigationTest, BackSwipeFromAnyNonHomeScreen) {
    std::vector<Screen> screens = {
        Screen::Chat, Screen::Contacts, Screen::Channels, Screen::Network,
        Screen::Heard, Screen::Map, Screen::Advertise, Screen::Settings,
        Screen::Trace, Screen::Terminal, Screen::Signal
    };

    for (auto s : screens) {
        reset_nav();
        navigate_to(s);
        handle_back_swipe(SlopOSEvent::Left);  // first: neutralise
        ASSERT_TRUE(handle_back_swipe(SlopOSEvent::Left));  // second: back
        EXPECT_EQ(current, Screen::Home)
            << "Failed for screen " << (int)s;
    }
}

TEST_F(NavigationTest, BackSwipeRapidTripleLeftDoesNotDoubleBack) {
    navigate_to(Screen::Chat);
    navigate_to(Screen::Settings);
    // Three rapid lefts: swipe1 consumed, swipe2 → back to Chat, swipe3 → first on Chat
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));   // 1: consumed
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));   // 2: back to Chat
    EXPECT_EQ(current, Screen::Chat);
    // Counter was reset by go_back, so swipe3 starts fresh
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));   // 3: first on Chat
    EXPECT_EQ(current, Screen::Chat); // still on Chat
    EXPECT_TRUE(handle_back_swipe(SlopOSEvent::Left));   // 4: second → back to Home
    EXPECT_EQ(current, Screen::Home);
}

} // anonymous namespace
