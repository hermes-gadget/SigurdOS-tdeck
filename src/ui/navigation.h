#pragma once

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

#include "../hal/trackball.h"

namespace sigurdos::ui {

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
    NodeStats,
    Telemetry,
    NodeStatus,
    WiFiNetworks,
    Bluetooth,
    Regions,
    RepeaterDetail,
    CustomRadioSetup,
    MessageSearch,
    MeshDashboard,
    FileBrowser,
    COUNT
};

inline bool screen_refresh_matches(Screen expected, Screen active)
{
    return expected == active;
}

inline bool navigation_screen_valid(Screen screen)
{
    const int value = static_cast<int>(screen);
    return value >= 0 && value < static_cast<int>(Screen::COUNT);
}

// Routes that expose device configuration, credentials, destructive actions,
// or identity controls.  Authorization is enforced by the router for every
// entry path rather than relying on individual screen renderers.
bool is_pin_protected_route(Screen screen);
// Navigate to a screen
void navigate_to(Screen screen);
// Make first-boot Onboarding the navigation root until setup restarts the
// device. Other destinations are rejected so this cannot bypass PIN gates.
void navigate_to_forced(Screen screen);
bool navigation_is_forced();

// Parameterized nested routes. These retain the route arguments so Back can
// re-dispatch a detail screen without bypassing navigation history.
void navigate_to_contact_detail(const char* contact_name);
void navigate_to_repeater_detail(const char* contact_name, bool skip_login = false);
void navigate_to_custom_radio_setup();

// Go back to previous screen
void go_back();

// Return true when the navigation stack has a previous screen.
bool can_go_back();

// Return the screen currently owned by the navigation stack.
Screen current_screen();

// Re-dispatch the current screen without modifying history.
// Used after theme/style changes to rebuild widgets with new globals.
void refresh_current_screen();

// Re-dispatch only when the caller's screen is still current. Deferred LVGL
// callbacks use this to avoid replacing an unrelated screen generation.
bool refresh_current_screen_if(Screen expected);

// PIN-screen completion hooks.  The router keeps the denied operation pending
// so a successful unlock resumes the exact forward/back/refresh request
// without changing history before authorization.
void navigation_pin_unlocked(Screen target_screen);
void navigation_pin_cancelled();

#ifdef SIGURDOS_NAVIGATION_TEST
// Reset file-scope router state for production-router native tests only.
void navigation_reset_for_test(Screen initial = Screen::Home);
#endif

// Universal back-swipe gesture (two-swipe commit).
// Call this from the trackball dispatch loop for screens that don't have
// their own Left handler. The first Left event is consumed (neutralise),
// the second Left triggers go_back(). Non-Left events reset the counter.
// Returns true if the event was consumed.
bool handle_back_swipe(SigurdOSTrackballEvent event);

} // namespace sigurdos::ui
