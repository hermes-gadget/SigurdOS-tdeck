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

#include <lvgl.h>

namespace slopos::ui {
void heard_screen_show();
void contacts_screen_show();
void contact_detail_screen_show(const char* contact_name);
void signal_screen_show();
void map_screen_show();
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
void advertise_screen_show();
void radio_setup_screen_show();
void custom_rf_screen_show();

// Highlight the current screen's back button border for back-swipe visual feedback.
// Passing true sets a 2px accent border; false reverts to the default divider border.
void highlight_back_button(bool show);

// Clear the saved back-button reference (call when switching to a screen without
// a back button, e.g. Home, Chat, Onboarding).
void screens_clear_back_btn();
} // namespace slopos::ui
