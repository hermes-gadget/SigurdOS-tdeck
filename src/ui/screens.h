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

#include <lvgl.h>
#include "../hal/trackball.h"

namespace sigurdos::ui {
void heard_screen_show();
void contacts_screen_show();
void contacts_screen_set_filter(int adv_type); // -1=all, else ADV_TYPE_*
void contact_detail_screen_show(const char* contact_name);
void signal_screen_show();
void map_screen_show();
bool map_screen_handle_trackball(SigurdOSTrackballEvent event);
void settings_screen_show();
void terminal_screen_show();
void term_dump_log();
void term_clear_log();
void term_submit(const char* text);
lv_obj_t* term_get_input();
void trace_screen_show();
void channels_screen_show();
void finder_screen_show();
void repeaters_screen_show();
void repeater_detail_screen_show(const char* contact_name, bool skip_login = false);
void admin_cmd_show(const char* contact_name);
void advertise_screen_show();
void radio_setup_screen_show();
void custom_rf_screen_show();
void settings_radio_show();
void settings_gps_show();
void settings_display_show();
void settings_system_show();
void telemetry_screen_show();
void node_stats_screen_show();
void node_status_screen_show();
void wifi_networks_screen_show();
void bluetooth_screen_show();
void regions_screen_show();
void update_wifi_status();

// Highlight the current screen's back button border for back-swipe visual feedback.
// Passing true sets a 2px accent border; false reverts to the default divider border.
void highlight_back_button(bool show);

// Clear the saved back-button reference (call when switching to a screen without
// Called by home_screen (and others) to null dangling pointer refs.
void screens_clear_back_btn();
// Null the WiFi icon pointer so update_wifi_status() doesn't
// dereference a freed label after screen transitions.
void screens_clear_wifi_icon();
} // namespace sigurdos::ui
