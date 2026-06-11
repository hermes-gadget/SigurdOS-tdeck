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

// ════════════════════════════════════════════════════════
// Settings — category menu
// ════════════════════════════════════════════════════════
void settings_screen_show()
{
    // PIN gate check
    if (sigurdos::prefs_get().device_pin != 0 && !pin_grace_active()) {
        pin_entry_show(Screen::Settings);
        return;
    }
    lv_obj_t* scr = make_screen_full("Settings");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    // Category helpers — compact row with accent icon
    struct Cat { const char* icon; const char* label; Screen target; };
    Cat cats[] = {
        {LV_SYMBOL_WIFI,    "WiFi",             Screen::WiFiNetworks},
        {LV_SYMBOL_WIFI,    "Bluetooth",        Screen::Bluetooth},
        {LV_SYMBOL_WIFI,    "Radio / Mesh",     Screen::SettingsRadio},
        {LV_SYMBOL_GPS,     "GPS / Location",   Screen::SettingsGPS},
        {LV_SYMBOL_IMAGE,   "Display / UI",     Screen::SettingsDisplay},
        {LV_SYMBOL_SETTINGS,"System",           Screen::SettingsSystem},
        {LV_SYMBOL_SETTINGS,"Node Stats",       Screen::NodeStats},
    };

    const int cat_count = sizeof(cats) / sizeof(cats[0]);
    for (int i = 0; i < cat_count; i++) {
        lv_obj_t* btn = lv_list_add_btn(list, cats[i].icon, cats[i].label);
        lv_obj_set_style_bg_color(btn, lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_t* arrow = lv_label_create(btn);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arrow, lv_color_hex(TEXT_MUTED), 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -4, 0);
        Screen target = cats[i].target;
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            Screen s = (Screen)(intptr_t)lv_event_get_user_data(e);
            navigate_to(s);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)target);
    }

    show_screen(scr);
}

} // namespace sigurdos::ui
