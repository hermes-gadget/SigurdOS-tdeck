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

#include "../screens.h"
#include "../screens_common.h"
#include "../theme.h"
#include "../responsive.h"
#include "../../mesh/mesh_wrapper.h"
#include <lvgl.h>
#include <cstdio>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ════════════════════════════════════════════════════════
// Bluetooth companion status
// ════════════════════════════════════════════════════════
static void bluetooth_state_text(char* buf, size_t len)
{
    if (!buf || len == 0) return;
    if (!sigurdos::mesh::companionBleAvailable()) {
        snprintf(buf, len, "  BLE: Not built");
    } else if (sigurdos::mesh::companionBleConnected()) {
        snprintf(buf, len, "  BLE: Connected");
    } else if (sigurdos::mesh::companionBleEnabled()) {
        snprintf(buf, len, "  BLE: Advertising");
    } else {
        snprintf(buf, len, "  BLE: Off");
    }
}

void bluetooth_screen_show()
{
    lv_obj_t* scr = make_screen_full("Bluetooth");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    char buf[96];
    bluetooth_state_text(buf, sizeof(buf));
    lv_obj_t* row_state = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
    lv_obj_set_style_bg_color(row_state, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(row_state, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(row_state, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(row_state, [](lv_event_t* e) {
        lv_obj_t* row = (lv_obj_t*)lv_event_get_user_data(e);
        if (!sigurdos::mesh::companionBleAvailable()) return;
        bool next = !sigurdos::mesh::companionBleEnabled();
        sigurdos::mesh::companionBleSetEnabled(next);
        char state_buf[96];
        bluetooth_state_text(state_buf, sizeof(state_buf));
        update_row_label(row, state_buf);
    }, LV_EVENT_CLICKED, row_state);

    snprintf(buf, sizeof(buf), "  App: %s", sigurdos::mesh::companionBleConnected() ? "Connected" : "Not connected");
    lv_obj_t* row_conn = lv_list_add_btn(list, LV_SYMBOL_OK, buf);
    lv_obj_set_style_bg_color(row_conn, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(row_conn, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(row_conn, lv_color_hex(TEXT_PRIMARY), 0);

    snprintf(buf, sizeof(buf), "  PIN: %06lu", (unsigned long)sigurdos::mesh::companionBlePin());
    lv_obj_t* row_pin = lv_list_add_btn(list, LV_SYMBOL_EDIT, buf);
    lv_obj_set_style_bg_color(row_pin, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(row_pin, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(row_pin, lv_color_hex(TEXT_PRIMARY), 0);

    uint32_t last = sigurdos::mesh::companionBleLastSyncTime();
    if (last == 0) {
        snprintf(buf, sizeof(buf), "  Last sync: Never");
    } else {
        uint32_t sec = last % 86400;
        snprintf(buf, sizeof(buf), "  Last sync: %02lu:%02lu",
                 (unsigned long)((sec / 3600) % 24),
                 (unsigned long)((sec / 60) % 60));
    }
    lv_obj_t* row_sync = lv_list_add_btn(list, LV_SYMBOL_REFRESH, buf);
    lv_obj_set_style_bg_color(row_sync, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(row_sync, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(row_sync, lv_color_hex(TEXT_PRIMARY), 0);

    show_screen(scr);
}

} // namespace sigurdos::ui
