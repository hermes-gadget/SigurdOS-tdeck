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
#include "../diagnostics/log.h"
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

// Layout constants (from responsive.h)
using namespace responsive;

void show_screen(lv_obj_t* scr)
{
    static constexpr size_t LVGL_ASYNC_DELETE_LOW_WATERMARK = 4 * 1024U;

    // Use auto_del=false for ALL screen loads and manage outgoing-screen
    // deletion manually. Previously, mixing auto_del=true/false across
    // different screen types caused LVGL's internal screen-load state
    // machine (d->scr_to_load, d->prev_scr, d->del_prev) to deadlock
    // after ~4-5 round-trips between animated and instant screen loads.
    //
    // For NONE animation (this path): the load completes synchronously
    // so lv_obj_del_async() is safe immediately.
    lv_obj_t* old_scr = lv_screen_active();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    if (old_scr && old_scr != scr) {
        lv_mem_monitor_t before;
        lv_mem_monitor(&before);
        lv_obj_del_async(old_scr);

        lv_mem_monitor_t after;
        lv_mem_monitor(&after);
        SIG_LOGD("[ui] async delete old=%p lvgl_free=%u largest=%u used=%u%% frag=%u%%",
                 static_cast<void*>(old_scr),
                 (unsigned)after.free_size,
                 (unsigned)after.free_biggest_size,
                 (unsigned)after.used_pct,
                 (unsigned)after.frag_pct);

        // A successful lv_obj_del_async() allocates both callback info and a
        // one-shot timer. If free space did not fall, LVGL likely discarded
        // the request after an allocation failure and the old root will leak.
        if (after.free_size >= before.free_size) {
            SIG_LOGW("[ui] async delete may not be scheduled old=%p lvgl_free=%u largest=%u",
                     static_cast<void*>(old_scr),
                     (unsigned)after.free_size,
                     (unsigned)after.free_biggest_size);
        }
        if (after.free_biggest_size < LVGL_ASYNC_DELETE_LOW_WATERMARK) {
            SIG_LOGW("[ui] LVGL pool low after async delete old=%p largest=%u threshold=%u",
                     static_cast<void*>(old_scr),
                     (unsigned)after.free_biggest_size,
                     (unsigned)LVGL_ASYNC_DELETE_LOW_WATERMARK);
        }
    }
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
