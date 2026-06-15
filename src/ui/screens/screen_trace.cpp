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
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstdint>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ════════════════════════════════════════════════════════
// Trace — path tracing
// ════════════════════════════════════════════════════════
static lv_obj_t* trace_result_label = nullptr;
static lv_timer_t* g_trace_poll_timer = nullptr;

static void trace_screen_delete_cb(lv_event_t*) {
    if (g_trace_poll_timer) {
        lv_timer_del(g_trace_poll_timer);
        g_trace_poll_timer = nullptr;
    }
    trace_result_label = nullptr;
}

void trace_screen_show()
{
    lv_obj_t* scr = make_screen_full("Trace Route");
    sigurdos::mesh::clearTraceResult();
    trace_result_label = nullptr;

    lv_obj_add_event_cb(scr, trace_screen_delete_cb, LV_EVENT_DELETE, nullptr);

    char names[32][32];
    int total = sigurdos::mesh::exportContacts(names, 32);

    lv_obj_t* info = lv_label_create(scr);
    lv_obj_set_width(info, CONTENT_W);
    lv_obj_set_style_pad_left(info, 8, 0);
    lv_obj_set_style_pad_right(info, 8, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(info, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);

    if (total == 0) {
        lv_label_set_text(info,
            "No contacts discovered. Wait for adverts or incoming messages.");
        show_screen(scr);
        return;
    }

    lv_label_set_text(info, "Tap a contact to trace the path.");

    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - 26);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 24);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    static constexpr int ROW_H = 32;

    for (int i = 0; i < total; i++) {
        bool has_path = sigurdos::mesh::contactHasPath(i);

        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(ACCENT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        // Icon
        lv_obj_t* icon = lv_label_create(row);
        lv_label_set_text(icon, has_path ? LV_SYMBOL_GPS : LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(icon, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(icon, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 6, 0);

        // Name
        lv_obj_t* name_l = lv_label_create(row);
        lv_label_set_text(name_l, names[i]);
        lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(name_l, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(name_l, LV_ALIGN_LEFT_MID, 28, 0);

        // Path status
        lv_obj_t* status_l = lv_label_create(row);
        lv_label_set_text(status_l, has_path ? "[path known]" : "[no path]");
        lv_obj_set_style_text_color(status_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(status_l, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(status_l, LV_ALIGN_RIGHT_MID, -6, 0);

        if (has_path) {
            int contact_idx = i;
            lv_obj_add_event_cb(row, [](lv_event_t* e) {
                int idx = (int)(intptr_t)lv_event_get_user_data(e);
                uint32_t tag;
                if (sigurdos::mesh::sendTrace(idx, &tag)) {
                    lv_obj_t* scr_ref = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
                    lv_obj_t* result_lbl = lv_label_create(scr_ref);
                    lv_obj_set_style_text_color(result_lbl, lv_color_hex(ACCENT), 0);
                    lv_obj_set_style_text_font(result_lbl, emoji_wrapped_montserrat_10, 0);
                    lv_obj_align(result_lbl, LV_ALIGN_BOTTOM_MID, 0,
                                 -(BOT_BAR_H + DIVIDER_H + 24));
                    lv_label_set_text(result_lbl, "Trace sent, waiting...");
                    // Delete previous label if it exists
                    if (trace_result_label) {
                        lv_obj_del_async(trace_result_label);
                        trace_result_label = nullptr;
                    }
                    trace_result_label = result_lbl;

                    g_trace_poll_timer = lv_timer_create([](lv_timer_t* t) {
                        if (!trace_result_label) { lv_timer_del(t); return; }
                        if (sigurdos::mesh::hasTraceResult()) {
                            uint8_t len = sigurdos::mesh::getTracePathLen();
                            if (len > 64) len = 64;  // MAX_PATH_SIZE
                            uint8_t snrs[64], hashes[64];
                            sigurdos::mesh::getTracePath(snrs, hashes);
                            char res[128];
                            if (len == 0) {
                                snprintf(res, sizeof(res), "Trace timed out");
                            } else {
                                int pos = snprintf(res, sizeof(res),
                                    "Trace: %d hop%s", len, len == 1 ? "" : "s");
                                for (int h = 0; h < len && pos < (int)sizeof(res) - 10; h++)
                                    pos += snprintf(res + pos, sizeof(res) - pos,
                                        "  [%ddBm]", snrs[h]);
                            }
                            lv_label_set_text(trace_result_label, res);
                            sigurdos::mesh::clearTraceResult();
                            lv_timer_del(t);
                        }
                    }, 500, nullptr);
                }
            }, LV_EVENT_CLICKED, (void*)(intptr_t)contact_idx);
        }
    }

    show_screen(scr);
}

} // namespace sigurdos::ui
