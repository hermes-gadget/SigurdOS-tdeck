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


#include "navigation.h"
#include "home_screen.h"
#include "chat_screen.h"
#include "screens.h"
#include "onboarding_screen.h"
#include <lvgl.h>

namespace slopos::ui {

static Screen current = Screen::Home;

// ── Back history stack (circular, max 8 entries) ─────────
static constexpr int MAX_HISTORY = 8;
static Screen history[MAX_HISTORY];
static int   history_top = -1;  // index of top (empty stack before any nav)

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

static int back_swipe_commit = 0; // counter for two-swipe commit

static void dispatch_screen(Screen screen) {
    switch (screen) {
    case Screen::Home:       home_screen_show();       break;
    case Screen::Chat:       chat_screen_show();       break;
    case Screen::Contacts:   contacts_screen_show();   break;
    case Screen::Channels:   channels_screen_show();  break;
    case Screen::Network:    finder_screen_show();    break;
    case Screen::Heard:      heard_screen_show();      break;
    case Screen::Map:        map_screen_show();        break;
    case Screen::Advertise:  advertise_screen_show();  break;
    case Screen::Settings:   settings_screen_show();   break;
    case Screen::Trace:      trace_screen_show();      break;
    case Screen::Terminal:   terminal_screen_show();   break;
    case Screen::Signal:     signal_screen_show();     break;
    case Screen::RadioSetup: radio_setup_screen_show(); break;
    case Screen::Repeaters:  repeaters_screen_show();   break;
    case Screen::Onboarding: onboarding_screen_show(); break;
    case Screen::SettingsRadio:   settings_radio_show();   break;
    case Screen::SettingsGPS:     settings_gps_show();     break;
    case Screen::SettingsDisplay: settings_display_show(); break;
    case Screen::SettingsSystem:  settings_system_show();  break;
    default: break;
    }
}

void navigate_to(Screen screen)
{
    if (screen == current) return;

    back_swipe_commit = 0; // reset back-swipe state on new navigation
    highlight_back_button(false);

    // Push current screen onto history before navigating away
    push_history(current);
    current = screen;

    dispatch_screen(screen);
}

void go_back()
{
    if (history_empty()) return; // nowhere to go back to

    back_swipe_commit = 0; // reset back-swipe state on back navigation
    highlight_back_button(false);

    Screen target = pop_history();
    // Navigate directly without pushing current (we're going back, not forward)
    current = target;

    dispatch_screen(target);
}

bool can_go_back()
{
    return !history_empty();
}

Screen current_screen()
{
    return current;
}

// ════════════════════════════════════════════════════
// Universal back-swipe (two-swipe commit)
// ════════════════════════════════════════════════════
bool handle_back_swipe(SlopOSTrackballEvent event)
{
    // Any non-Left event resets the counter and clears visual feedback
    if (event != SlopOSTrackballEvent::Left) {
        back_swipe_commit = 0;
        highlight_back_button(false);
        return false;
    }

    back_swipe_commit++;
    if (back_swipe_commit >= 2) {
        back_swipe_commit = 0;
        highlight_back_button(false);
        go_back();
        return true;
    }

    // First left swipe: show visual feedback on the back button
    highlight_back_button(true);
    return true;
}

} // namespace slopos::ui
