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


#include "screens.h"
#include "screens_common.h"
#include "navigation.h"
#include "theme.h"
#include "responsive.h"
#include "contact_paging.h"
#include "home_screen.h"
#include "chat_screen.h"
#include "../hal/tdeck_pins.h"
#include "../hal/battery.h"
#include "../hal/sdcard.h"
#include "../hal/wifi_ota.h"
#include "../hal/github_ota.h"
#include "../hal/gps.h"
#include "../hal/launcher_env.h"
#include "../hal/prefs.h"
#include "../hal/display.h"
#include "../hal/keyboard.h"
#include "../hal/display.h"
#include "../mesh/mesh_wrapper.h"
#include "../mesh/regions.h"
#include "../mesh/channel_validation.h"
#include "../mesh/public_channel.h"
#include "../app/map_renderer.h"
#include "../fonts/emoji_font.h"
#include "../app/qr_show.h"
#include <MeshCore.h>
#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <functional>
#include <new>
#include <SPIFFS.h>

namespace sigurdos::ui {

using namespace theme;

// ── Layout constants (from responsive.h) ──────────────────
using namespace responsive;
// TOP_BAR_H, BOT_BAR_H, DIVIDER_H, CONTENT_Y, CONTENT_H — all from responsive.h

void show_screen(lv_obj_t* scr)
{
    lv_scr_load(scr);
}

// Update the text inside a settings row button (used after live time set)
void update_row_label(lv_obj_t* row, const char* new_text)
{
    if (!row) return;
    uint32_t n = lv_obj_get_child_cnt(row);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* ch = lv_obj_get_child(row, i);
        if (lv_obj_check_type(ch, &lv_label_class)) {
            lv_label_set_text(ch, new_text);
            return;
        }
    }
}

} // namespace sigurdos::ui
