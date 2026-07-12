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
#include "../screen_lifetime.h"
#include "../theme.h"
#include "../responsive.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

static lv_obj_t* g_advert_status_label = nullptr;
static lv_obj_t* g_advert_button = nullptr;
static lv_timer_t* g_advert_status_timer = nullptr;
static ScreenLifetime g_advertise_lifetime;
static constexpr uint32_t ADVERT_COOLDOWN_SECONDS = 10;

static void advertise_status_update()
{
    if (!g_advert_status_label) return;

    uint32_t last = sigurdos::mesh::getLastAdvertTime();
    uint32_t now = sigurdos::mesh::getCurrentTime();
    bool success = sigurdos::mesh::getLastAdvertSuccess();
    bool used_gps = sigurdos::mesh::getLastAdvertUsedGps();

    char age[24];
    bool on_cooldown = false;
    uint32_t cooldown_left = 0;
    if (last == 0 || now < last) {
        snprintf(age, sizeof(age), "unknown");
        on_cooldown = false;
    } else {
        uint32_t delta = now - last;
        if (delta < 60)         snprintf(age, sizeof(age), "just now");
        else if (delta < 3600)  snprintf(age, sizeof(age), "%lumin ago", (unsigned long)(delta / 60));
        else if (delta < 86400) snprintf(age, sizeof(age), "%luh ago", (unsigned long)(delta / 3600));
        else                   snprintf(age, sizeof(age), "%lud ago", (unsigned long)(delta / 86400));

        if (delta < ADVERT_COOLDOWN_SECONDS) {
            on_cooldown = true;
            cooldown_left = ADVERT_COOLDOWN_SECONDS - delta;
        }
    }

    if (g_advert_button) {
        if (on_cooldown) {
            lv_obj_add_state(g_advert_button, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(g_advert_button, LV_STATE_DISABLED);
        }
    }

    char text[128];
    if (last == 0) {
        snprintf(text, sizeof(text), "Status: never sent yet");
    } else if (success) {
        if (on_cooldown) {
            snprintf(text, sizeof(text),
                "Status: last broadcast %s (%s), cool-down %lus",
                age, used_gps ? "with GPS" : "without GPS", (unsigned long)cooldown_left);
        } else {
            snprintf(text, sizeof(text),
                "Status: last broadcast %s (%s)",
                age, used_gps ? "with GPS" : "without GPS");
        }
    } else {
        snprintf(text, sizeof(text), "Status: last attempt failed (mesh unavailable)");
        if (on_cooldown) {
            snprintf(text, sizeof(text),
                "Status: last attempt failed, cool-down %lus",
                (unsigned long)cooldown_left);
        }
    }

    lv_label_set_text(g_advert_status_label, text);
}

static void advertise_status_timer_cb(lv_timer_t*)
{
    advertise_status_update();
}

// ════════════════════════════════════════════════════════
// Advertise — broadcast presence
// ════════════════════════════════════════════════════════
void advertise_screen_show()
{
    lv_obj_t* scr = make_screen_full("Advertise");

    lv_obj_t* info = lv_label_create(scr);
    lv_label_set_text(info,
        "Advertise Presence\n\n"
        "Broadcast your node to the mesh network.\n\n"
        "Other nodes will see you in their Heard list.\n\n"
        "Tap below to send advert.");
    lv_obj_set_width(info, CONTENT_W);
    lv_obj_set_style_pad_left(info, 8, 0);
    lv_obj_set_style_pad_right(info, 8, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(info, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);

    lv_obj_t* status = lv_label_create(scr);
    g_advert_status_label = status;
    lv_label_set_text(status, "Status: never sent yet");
    lv_obj_set_width(status, CONTENT_W);
    lv_obj_set_style_pad_left(status, 8, 0);
    lv_obj_set_style_pad_right(status, 8, 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(status, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 110);

    lv_obj_t* btn = lv_btn_create(scr);
    g_advert_button = btn;
    lv_obj_set_size(btn, 140, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -(BOT_BAR_H + DIVIDER_H + 8));
    lv_obj_set_style_bg_color(btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(TEXT_MUTED), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_t* bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_AUDIO "  Advertise Now");
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, [](lv_event_t*) {
        sigurdos::mesh::sendAdvert();
        advertise_status_update();
    }, LV_EVENT_CLICKED, nullptr);

    if (g_advert_status_timer) {
        lv_timer_del(g_advert_status_timer);
        g_advert_status_timer = nullptr;
    }
    g_advert_status_timer = lv_timer_create(advertise_status_timer_cb, 1000, nullptr);
    g_advertise_lifetime.bind(scr);
    g_advertise_lifetime.track(&g_advert_button);
    g_advertise_lifetime.track(&g_advert_status_label);
    g_advertise_lifetime.trackTimer(&g_advert_status_timer);
    advertise_status_update();

    show_screen(scr);
}

} // namespace sigurdos::ui
