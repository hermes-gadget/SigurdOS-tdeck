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

void home_screen_create();
void home_screen_show();
void home_screen_handle_trackball(SigurdOSTrackballEvent event);
void home_screen_update_battery(int pct);
void home_screen_update_time(const char* time_str);
void home_screen_update_channels();
void home_screen_update_badges();

} // namespace sigurdos::ui
