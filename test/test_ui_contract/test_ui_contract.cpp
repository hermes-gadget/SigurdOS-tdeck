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

#include <gtest/gtest.h>

#include "ui/home_screen.h"
#include "ui/bluetooth_help.h"
#include "ui/screens.h"
#include "ui/ui.h"

namespace {

TEST(UIContractTest, StartupApisStayStable) {
    using void_fn = void (*)();
    using status_fn = void (*)(const char*);
    using event_fn = bool (*)(SigurdOSTrackballEvent);

    (void)static_cast<void_fn>(sigurdos::ui::init);
    (void)static_cast<status_fn>(sigurdos::ui::set_boot_status);
    (void)static_cast<void_fn>(sigurdos::ui::load_persisted_state);
    (void)static_cast<void_fn>(sigurdos::ui::loop);
    (void)static_cast<event_fn>(sigurdos::ui::handle_trackball_event);
    SUCCEED();
}

TEST(UIContractTest, HomeScreenApisStayStable) {
    using void_fn = void (*)();
    using event_fn = void (*)(SigurdOSTrackballEvent);
    using battery_fn = void (*)(int);
    using text_fn = void (*)(const char*);

    (void)static_cast<void_fn>(sigurdos::ui::home_screen_create);
    (void)static_cast<void_fn>(sigurdos::ui::home_screen_show);
    (void)static_cast<event_fn>(sigurdos::ui::home_screen_handle_trackball);
    (void)static_cast<battery_fn>(sigurdos::ui::home_screen_update_battery);
    (void)static_cast<text_fn>(sigurdos::ui::home_screen_update_time);
    (void)static_cast<void_fn>(sigurdos::ui::home_screen_update_channels);
    (void)static_cast<void_fn>(sigurdos::ui::home_screen_update_badges);
    SUCCEED();
}

TEST(UIContractTest, PrimaryScreenShowApisStayStable) {
    using void_fn = void (*)();
    using map_event_fn = bool (*)(SigurdOSTrackballEvent);

    (void)static_cast<void_fn>(sigurdos::ui::heard_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::contacts_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::signal_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::map_screen_show);
    (void)static_cast<map_event_fn>(sigurdos::ui::map_screen_handle_trackball);
    (void)static_cast<void_fn>(sigurdos::ui::settings_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::trace_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::channels_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::finder_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::repeaters_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::advertise_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::radio_setup_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::custom_rf_screen_show);
    SUCCEED();
}

TEST(UIContractTest, DetailAndSettingsScreenApisStayStable) {
    using void_fn = void (*)();
    using filter_fn = void (*)(int);
    using contact_fn = void (*)(const char*);
    using repeater_fn = void (*)(const char*, bool);

    (void)static_cast<filter_fn>(sigurdos::ui::contacts_screen_set_filter);
    (void)static_cast<contact_fn>(sigurdos::ui::contact_detail_screen_show);
    (void)static_cast<repeater_fn>(sigurdos::ui::repeater_detail_screen_show);
    (void)static_cast<contact_fn>(sigurdos::ui::admin_cmd_show);
    (void)static_cast<void_fn>(sigurdos::ui::settings_radio_show);
    (void)static_cast<void_fn>(sigurdos::ui::settings_gps_show);
    (void)static_cast<void_fn>(sigurdos::ui::settings_display_show);
    (void)static_cast<void_fn>(sigurdos::ui::settings_system_show);
    SUCCEED();
}

TEST(UIContractTest, TelemetryStatusAndWifiScreenApisStayStable) {
    using void_fn = void (*)();

    (void)static_cast<void_fn>(sigurdos::ui::telemetry_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::node_stats_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::node_status_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::wifi_networks_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::regions_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::update_wifi_status);
    SUCCEED();
}

TEST(UIContractTest, BluetoothExplainsDeviceAuthoredMessageLimit) {
    EXPECT_STREQ(
        "Messages typed on T-Deck stay here.\n"
        "The official app will not show them.",
        sigurdos::ui::bluetooth_device_authored_notice());
}

TEST(UIContractTest, TerminalAndBackButtonApisStayStable) {
    using void_fn = void (*)();
    using submit_fn = void (*)(const char*);
    using input_fn = lv_obj_t* (*)();
    using highlight_fn = void (*)(bool);

    (void)static_cast<void_fn>(sigurdos::ui::terminal_screen_show);
    (void)static_cast<void_fn>(sigurdos::ui::term_dump_log);
    (void)static_cast<void_fn>(sigurdos::ui::term_clear_log);
    (void)static_cast<submit_fn>(sigurdos::ui::term_submit);
    (void)static_cast<input_fn>(sigurdos::ui::term_get_input);
    (void)static_cast<highlight_fn>(sigurdos::ui::highlight_back_button);
    (void)static_cast<void_fn>(sigurdos::ui::screens_clear_back_btn);
    (void)static_cast<void_fn>(sigurdos::ui::screens_clear_wifi_icon);
    SUCCEED();
}

} // namespace
