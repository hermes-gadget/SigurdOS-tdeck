#pragma once

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

#include "../hal/trackball.h"

namespace slopos::ui {

// Screen identifiers for navigation
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
    ContactDetail,
    SettingsRadio,
    SettingsGPS,
    SettingsDisplay,
    SettingsSystem,
    NodeStatus,
    COUNT
};

// Navigate to a screen
void navigate_to(Screen screen);

// Go back to previous screen
void go_back();

// Return true when the navigation stack has a previous screen.
bool can_go_back();

// Return the screen currently owned by the navigation stack.
Screen current_screen();

// Universal back-swipe gesture (two-swipe commit).
// Call this from the trackball dispatch loop for screens that don't have
// their own Left handler. The first Left event is consumed (neutralise),
// the second Left triggers go_back(). Non-Left events reset the counter.
// Returns true if the event was consumed.
bool handle_back_swipe(SlopOSTrackballEvent event);

} // namespace slopos::ui
