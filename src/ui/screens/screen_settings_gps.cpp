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
#include "../../hal/prefs.h"
#include "../../hal/gps.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

void settings_gps_show()
{
    lv_obj_t* scr = make_screen_full("GPS / Location");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    char buf[128];
    int row = 0;

    // GPS status
    snprintf(buf, sizeof(buf), "  GPS: %s", sigurdos_gps_has_fix() ? "Fix acquired" : "No fix");
    lv_obj_t* row0 = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
    lv_obj_set_style_bg_color(row0, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(row0, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(row0, lv_color_hex(TEXT_PRIMARY), 0);
    row++;

    // GPS enable toggle
    snprintf(buf, sizeof(buf), "  GPS: %s", p.gps_enabled ? "ON" : "OFF");
    lv_obj_t* btn_gps_en = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
    lv_obj_set_style_bg_color(btn_gps_en, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_gps_en, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_gps_en, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_gps_en, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.gps_enabled = !np.gps_enabled;
        sigurdos::prefs_set(np);
        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  GPS: %s", np.gps_enabled ? "ON" : "OFF");
        lv_obj_t* lbl = lv_obj_get_child(target, 1);
        if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
            lv_label_set_text(lbl, row_buf);
        }
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // GPS interval cycle
    {
        static constexpr uint16_t GPS_INT_VALUES[] = {0, 1, 5, 10, 30, 60};
        static constexpr const char* GPS_INT_LABELS[] = {"Every loop", "1s", "5s", "10s", "30s", "60s"};
        static constexpr int NUM_GPS_INT = 6;
        int cur_gps = 0;
        for (int i = 0; i < NUM_GPS_INT; i++) {
            if (p.gps_interval == GPS_INT_VALUES[i]) { cur_gps = i; break; }
        }
        snprintf(buf, sizeof(buf), "  GPS interval: %s", GPS_INT_LABELS[cur_gps]);
        lv_obj_t* btn_gps_int = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
        lv_obj_set_style_bg_color(btn_gps_int, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_bg_opa(btn_gps_int, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_gps_int, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_gps_int, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            int idx = 0;
            for (int i = 0; i < 6; i++) {
                if (np.gps_interval == (uint16_t[]){0,1,5,10,30,60}[i]) { idx = i; break; }
            }
            idx = (idx + 1) % 6;
            np.gps_interval = (uint16_t[]){0,1,5,10,30,60}[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  GPS interval: %s",
                     (const char*[]){"Every loop","1s","5s","10s","30s","60s"}[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Share location toggle
    snprintf(buf, sizeof(buf), "  Share location: %s", p.share_location ? "ON" : "OFF");
    lv_obj_t* btn_share = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
    lv_obj_set_style_bg_color(btn_share, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_share, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_share, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_share, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.share_location = !np.share_location;
        sigurdos::prefs_set(np);
        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Share location: %s", np.share_location ? "ON" : "OFF");
        lv_obj_t* lbl = lv_obj_get_child(target, 1);
        if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
            lv_label_set_text(lbl, row_buf);
        }
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    show_screen(scr);
}

} // namespace sigurdos::ui
