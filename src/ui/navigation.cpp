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
#include "navigation_state.h"
#include "home_screen.h"
#include "chat_screen.h"
#include "screens.h"
#include "onboarding_screen.h"
#include <lvgl.h>
#if SIGURDOS_TELEMETRY
#include "../diagnostics/telemetry.h"
#endif

namespace sigurdos::ui {

static NavigationState navigation_state;

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
    case Screen::NodeStats:       node_stats_screen_show(); break;
    case Screen::Telemetry:       telemetry_screen_show(); break;
    case Screen::NodeStatus:      node_status_screen_show(); break;
    case Screen::WiFiNetworks:    wifi_networks_screen_show(); break;
    case Screen::Bluetooth:       bluetooth_screen_show(); break;
    case Screen::Regions:        regions_screen_show();      break;
    default: break;
    }
}

void navigate_to(Screen screen)
{
#if SIGURDOS_TELEMETRY
    const Screen previous = navigation_state.current();
#endif
    if (!navigation_state.navigateTo(screen)) return;
    highlight_back_button(false);

    dispatch_screen(screen);

#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_screen_transition(
        static_cast<uint8_t>(previous),
        static_cast<uint8_t>(screen),
        lv_tick_get());
#endif
}

void go_back()
{
#if SIGURDOS_TELEMETRY
    const Screen previous = navigation_state.current();
#endif
    if (!navigation_state.goBack()) return;
    highlight_back_button(false);
    const Screen target = navigation_state.current();

    dispatch_screen(target);

#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_screen_transition(
        static_cast<uint8_t>(previous),
        static_cast<uint8_t>(target),
        lv_tick_get());
#endif
}

bool can_go_back()
{
    return navigation_state.canGoBack();
}

Screen current_screen()
{
    return navigation_state.current();
}

void refresh_current_screen()
{
    dispatch_screen(navigation_state.current());
}

// ════════════════════════════════════════════════════
// Universal back-swipe (two-swipe commit)
// ════════════════════════════════════════════════════
bool handle_back_swipe(SigurdOSTrackballEvent event)
{
#if SIGURDOS_TELEMETRY
    const Screen previous = navigation_state.current();
#endif
    const BackSwipeResult result = navigation_state.handleBackSwipe(
        event == SigurdOSTrackballEvent::Left);
    if (result == BackSwipeResult::Ignored) {
        highlight_back_button(false);
        return false;
    }
    if (result == BackSwipeResult::Navigated) {
        highlight_back_button(false);
        dispatch_screen(navigation_state.current());
#if SIGURDOS_TELEMETRY
        sigurdos::telemetry::report_screen_transition(
            static_cast<uint8_t>(previous),
            static_cast<uint8_t>(navigation_state.current()),
            lv_tick_get());
#endif
    } else if (result == BackSwipeResult::Completed) {
        highlight_back_button(false);
    } else {
        highlight_back_button(true);
    }
    return true;
}

} // namespace sigurdos::ui
