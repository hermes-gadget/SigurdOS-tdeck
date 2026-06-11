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
#include "navigation.h"

namespace sigurdos::ui {

// Shared screen infrastructure (implementation in screens_common.cpp).
// make_screen_full — builds the consistent top bar (back button, title,
// time, signal dots) and bottom bar (device name, WiFi icon, battery).
lv_obj_t* make_screen_full(const char* title);

// Load a fully built screen object (lv_scr_load wrapper).
void show_screen(lv_obj_t* scr);

// Device PIN gate — true while a previous unlock is within the grace window.
bool pin_grace_active();
// Show the PIN entry screen; loads target_screen on successful entry.
void pin_entry_show(Screen target_screen);

} // namespace sigurdos::ui
