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


#include "ui.h"
#include "home_screen.h"
#include "chat_screen.h"
#include "navigation.h"
#include "theme.h"
#include "responsive.h"
using namespace slopos::responsive;
#include "../mesh/mesh_wrapper.h"
#include "../hal/battery.h"
#include <Arduino.h>
#include <lvgl.h>

namespace slopos {
namespace ui {

static lv_obj_t* splash_scr = nullptr;
static uint32_t splash_start = 0;
static bool home_shown = false;

void init()
{
    // ── Splash Screen ─────────────────────────────────
    splash_scr = lv_obj_create(nullptr);
    theme::apply_dark_bg(splash_scr);

    lv_obj_t* logo = lv_label_create(splash_scr);
    lv_label_set_text(logo, "SlopOS");
    lv_obj_set_style_text_color(logo, lv_color_hex(theme::ACCENT), 0);
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_24, 0);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -16);

    lv_obj_t* sub = lv_label_create(splash_scr);
    lv_label_set_text(sub, "T-Deck");
    lv_obj_set_style_text_color(sub, lv_color_hex(theme::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 16);

    // Loading bar
    lv_obj_t* bar = lv_obj_create(splash_scr);
    lv_obj_set_size(bar, DISPLAY_W * 2 / 3, 4);
    lv_obj_set_style_bg_color(bar, lv_color_hex(theme::BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -40);

    lv_obj_t* fill = lv_obj_create(bar);
    lv_obj_set_size(fill, DISPLAY_W / 5, 4);
    lv_obj_set_style_bg_color(fill, lv_color_hex(theme::ACCENT), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fill, 2, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);

    lv_scr_load(splash_scr);
    splash_start = millis();
    home_shown = false;
}

void loop()
{
    // Transition from splash to home after 2 seconds
    if (!home_shown && (millis() - splash_start > 2000)) {
        home_screen_create();   // creates new screen, animates, deletes old
        home_shown = true;
        splash_scr = nullptr;   // old screen already deleted by home_screen_create()
    }

    // Periodic status bar updates (every 30s)
    static uint32_t last_update = 0;
    if (home_shown && (millis() - last_update > 30000)) {
        last_update = millis();
        home_screen_update_battery(slopos_battery_pct());
        home_screen_update_signal(slopos::mesh::getLastRSSI());
        {
            uint32_t epoch = slopos::mesh::getCurrentTime();
            char tbuf[8];
            if (epoch == 0) {
                snprintf(tbuf, sizeof(tbuf), "--:--");
            } else {
                uint32_t sec = epoch % 86400;
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d", (sec/3600)%24, (sec/60)%60);
            }
            home_screen_update_time(tbuf);
        }
        // Persist mesh state (contacts, identity) every 5 min
        static uint8_t save_counter = 0;
        if (++save_counter >= 10) {
            save_counter = 0;
            slopos::mesh::saveState();
        }
    }

    // Poll for new mesh messages and feed to chat
    if (home_shown) {
        static uint32_t last_msg_poll = 0;
        if (millis() - last_msg_poll > 1000) {
            last_msg_poll = millis();
            slopos::mesh::MeshMessage msgs[4];
            int n = slopos::mesh::pollMessages(msgs, 4);
            for (int i = 0; i < n; i++) {
                chat_screen_add_msg(msgs[i].channel, msgs[i].sender, msgs[i].text, msgs[i].is_self);
            }
        }
    }
}

bool handle_trackball_event(SlopOSTrackballEvent event)
{
    if (home_shown && current_screen() == Screen::Home) {
        home_screen_handle_trackball(event);
        return true;
    }
    return false;
}

} // namespace ui
} // namespace slopos
