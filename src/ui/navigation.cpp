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


#include "navigation.h"
#include "home_screen.h"
#include "chat_screen.h"
#include "screens.h"
#include "screens_common.h"
#include "onboarding_screen.h"
#include "../hal/prefs.h"
#include "../mesh/contact_store.h"
#include <lvgl.h>
#include <cstdint>
#include <cstring>
#if SIGURDOS_TELEMETRY
#include "../diagnostics/telemetry.h"
#endif

namespace sigurdos::ui {

static Screen current = Screen::Home;

// ── Back history stack (circular, max 16 entries) ─────────
static constexpr int MAX_HISTORY = 16;
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
static char contact_detail_name[
    sigurdos::mesh::SIGURDOS_CONTACT_ID_BUFFER_LEN] = {};
static char repeater_detail_name[
    sigurdos::mesh::SIGURDOS_CONTACT_ID_BUFFER_LEN] = {};
static bool repeater_detail_skip_login = false;
static bool forced_navigation = false;
static Screen forced_screen = Screen::Home;

enum class PendingNavigationType : uint8_t {
    None,
    Forward,
    Back,
    Refresh,
};

struct PendingNavigation {
    PendingNavigationType type = PendingNavigationType::None;
    Screen source = Screen::Home;
    Screen target = Screen::Home;
};

static PendingNavigation pending_navigation;

static void clear_pending_navigation()
{
    pending_navigation.type = PendingNavigationType::None;
    pending_navigation.source = Screen::Home;
    pending_navigation.target = Screen::Home;
}

static bool copy_route_name(char* destination, size_t capacity, const char* name)
{
    if (!destination || capacity == 0 || !name || !name[0]) return false;
    std::strncpy(destination, name, capacity - 1);
    destination[capacity - 1] = '\0';
    return true;
}

bool is_pin_protected_route(Screen screen)
{
    switch (screen) {
    case Screen::Settings:
    case Screen::Terminal:
    case Screen::RadioSetup:
    case Screen::SettingsRadio:
    case Screen::SettingsGPS:
    case Screen::SettingsDisplay:
    case Screen::SettingsSystem:
    case Screen::NodeStats:
    case Screen::WiFiNetworks:
    case Screen::Bluetooth:
    case Screen::Regions:
    case Screen::CustomRadioSetup:
        return true;
    default:
        return false;
    }
}

static bool route_ready(Screen screen)
{
    if (!navigation_screen_valid(screen)) return false;
    if (screen == Screen::ContactDetail) return contact_detail_name[0] != '\0';
    if (screen == Screen::RepeaterDetail) return repeater_detail_name[0] != '\0';
    return true;
}

static bool route_needs_pin(Screen screen)
{
    return is_pin_protected_route(screen) &&
           sigurdos::prefs_get().device_pin != 0 &&
           !pin_grace_active();
}

static bool authorize_route(Screen screen, PendingNavigationType type)
{
    if (!route_needs_pin(screen)) return true;

    pending_navigation.type = type;
    pending_navigation.source = current;
    pending_navigation.target = screen;
    pin_entry_show(screen);
    return false;
}

static void dispatch_screen_unchecked(Screen screen)
{
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
    case Screen::NodeStats:       node_stats_screen_show(); break;
    case Screen::Telemetry:       telemetry_screen_show(); break;
    case Screen::NodeStatus:      node_status_screen_show(); break;
    case Screen::WiFiNetworks:    wifi_networks_screen_show(); break;
    case Screen::Bluetooth:       bluetooth_screen_show(); break;
    case Screen::Regions:        regions_screen_show();      break;
    case Screen::ContactDetail:
        if (contact_detail_name[0]) contact_detail_screen_show(contact_detail_name);
        break;
    case Screen::RepeaterDetail:
        if (repeater_detail_name[0]) {
            repeater_detail_screen_show(
                repeater_detail_name, repeater_detail_skip_login);
        }
        break;
    case Screen::CustomRadioSetup: custom_rf_screen_show(); break;
    case Screen::MessageSearch:  message_search_screen_show(); break;
    case Screen::MeshDashboard:    mesh_dashboard_screen_show(); break;
    default: break;
    }
}

static void report_transition(Screen previous, Screen target)
{
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_screen_transition(
        static_cast<uint8_t>(previous),
        static_cast<uint8_t>(target),
        lv_tick_get());
#else
    (void)previous;
    (void)target;
#endif
}

static void navigate_forward_unchecked(Screen screen)
{
    if (!route_ready(screen)) return;
    back_swipe_commit = 0;
    highlight_back_button(false);

    const Screen previous = current;
    push_history(current);
    current = screen;
    dispatch_screen_unchecked(screen);
    report_transition(previous, screen);
}

static void navigate_back_unchecked(Screen target)
{
    if (!route_ready(target) || history_empty() || history[history_top] != target) return;

    back_swipe_commit = 0;
    highlight_back_button(false);

    const Screen previous = current;
    pop_history();
    current = target;
    dispatch_screen_unchecked(target);
    report_transition(previous, target);
}

void navigate_to(Screen screen)
{
    if (!route_ready(screen)) return;
    if (screen == current) return;
    if (forced_navigation && screen != forced_screen) return;
    if (pending_navigation.type != PendingNavigationType::None) return;
    if (!authorize_route(screen, PendingNavigationType::Forward)) return;
    navigate_forward_unchecked(screen);
}

void navigate_to_forced(Screen screen)
{
    // Forced navigation is intentionally narrow: it is the first-boot setup
    // root, not a general way to bypass PIN authorization or history.
    if (screen != Screen::Onboarding || !route_ready(screen)) return;
    clear_pending_navigation();
    history_top = -1;
    back_swipe_commit = 0;
    forced_navigation = true;
    forced_screen = screen;
    current = screen;
    highlight_back_button(false);
    dispatch_screen_unchecked(screen);
}

bool navigation_is_forced() { return forced_navigation; }

void navigate_to_contact_detail(const char* contact_name)
{
    if (forced_navigation) return;
    if (pending_navigation.type != PendingNavigationType::None) return;
    if (!copy_route_name(contact_detail_name, sizeof(contact_detail_name), contact_name)) return;
    if (current == Screen::ContactDetail) {
        contact_detail_screen_show(contact_detail_name);
        return;
    }
    navigate_to(Screen::ContactDetail);
}

void navigate_to_repeater_detail(const char* contact_name, bool skip_login)
{
    if (forced_navigation) return;
    if (pending_navigation.type != PendingNavigationType::None) return;
    if (!copy_route_name(repeater_detail_name, sizeof(repeater_detail_name), contact_name)) return;
    repeater_detail_skip_login = skip_login;
    if (current == Screen::RepeaterDetail) {
        repeater_detail_screen_show(repeater_detail_name, repeater_detail_skip_login);
        return;
    }
    navigate_to(Screen::RepeaterDetail);
}

void navigate_to_custom_radio_setup()
{
    navigate_to(Screen::CustomRadioSetup);
}

void go_back()
{
    if (forced_navigation) return;
    if (pending_navigation.type != PendingNavigationType::None) return;
    if (history_empty()) return; // nowhere to go back to

    const Screen target = history[history_top];
    if (!authorize_route(target, PendingNavigationType::Back)) return;
    navigate_back_unchecked(target);
}

bool can_go_back()
{
    return !forced_navigation && !history_empty();
}

Screen current_screen()
{
    return current;
}

void refresh_current_screen()
{
    if (!route_ready(current)) return;
    if (pending_navigation.type != PendingNavigationType::None) return;
    if (!authorize_route(current, PendingNavigationType::Refresh)) return;
    dispatch_screen_unchecked(current);
}

bool refresh_current_screen_if(Screen expected)
{
    if (!screen_refresh_matches(expected, current)) return false;
    refresh_current_screen();
    return true;
}

void navigation_pin_unlocked(Screen target_screen)
{
    const PendingNavigation pending = pending_navigation;
    clear_pending_navigation();
    if (!route_ready(target_screen) ||
        (forced_navigation && target_screen != forced_screen)) {
        return;
    }

    // Screen-local checks remain as defence in depth.  If a renderer was
    // invoked outside the router, there is no pending operation to resume.
    if (pending.type == PendingNavigationType::None) {
        if (current == target_screen) {
            dispatch_screen_unchecked(target_screen);
        } else {
            navigate_forward_unchecked(target_screen);
        }
        return;
    }

    // A stale PIN screen must never resume a newer/different denied route.
    if (pending.target != target_screen || pending.source != current) return;

    switch (pending.type) {
    case PendingNavigationType::Forward:
        if (target_screen != current) navigate_forward_unchecked(target_screen);
        break;
    case PendingNavigationType::Back:
        if (!history_empty() && history[history_top] == target_screen) {
            navigate_back_unchecked(target_screen);
        }
        break;
    case PendingNavigationType::Refresh:
        if (current == target_screen) dispatch_screen_unchecked(target_screen);
        break;
    case PendingNavigationType::None:
        break;
    }
}

void navigation_pin_cancelled()
{
    clear_pending_navigation();
}

#ifdef SIGURDOS_NAVIGATION_TEST
void navigation_reset_for_test(Screen initial)
{
    current = navigation_screen_valid(initial) ? initial : Screen::Home;
    history_top = -1;
    back_swipe_commit = 0;
    contact_detail_name[0] = '\0';
    repeater_detail_name[0] = '\0';
    repeater_detail_skip_login = false;
    forced_navigation = false;
    forced_screen = Screen::Home;
    clear_pending_navigation();
}
#endif

// ════════════════════════════════════════════════════
// Universal back-swipe (two-swipe commit)
// ════════════════════════════════════════════════════
bool handle_back_swipe(SigurdOSTrackballEvent event)
{
    // Any non-Left event resets the counter and clears visual feedback
    if (event != SigurdOSTrackballEvent::Left) {
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

} // namespace sigurdos::ui
