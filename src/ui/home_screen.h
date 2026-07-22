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
#include <cstddef>
#include <cstdio>

namespace sigurdos::ui {

inline bool home_screen_format_unread_badge(char* out, size_t out_size, int unread)
{
    if (!out || out_size == 0) return false;
    const int written = unread > 99
        ? std::snprintf(out, out_size, "99+")
        : std::snprintf(out, out_size, "%d", unread < 0 ? 0 : unread);
    return written >= 0 && static_cast<size_t>(written) < out_size;
}

struct HomeTileFilters {
    int chat_filter;
    bool rooms_only;
};

inline HomeTileFilters home_tile_filters(int tile_index) {
    // CHATS=0, DMs=1, ROOMS=2; every other tile resets both filters.
    return {tile_index == 0 ? 1 : (tile_index == 1 ? 2 : 0), tile_index == 2};
}

void home_screen_create();
void home_screen_show();
void home_screen_handle_trackball(SigurdOSTrackballEvent event);
void home_screen_update_battery(int pct);
void home_screen_update_time(const char* time_str);
void home_screen_update_channels();
void home_screen_update_badges();

} // namespace sigurdos::ui
