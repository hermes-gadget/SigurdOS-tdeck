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

static lv_obj_t* g_date_row = nullptr;   // for live update after setting time
static lv_obj_t* g_time_row = nullptr;

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

struct DateTimeDialogCtx {
    lv_obj_t* input;
    lv_obj_t* feedback;
    bool       is_date;
};

static void datetime_set_dialog(lv_obj_t* parent, bool is_date)
{
    int y, mo, d, h, mi;
    sigurdos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);

    char cur[16];
    if (is_date) snprintf(cur, sizeof(cur), "%04d-%02d-%02d", y, mo, d);
    else         snprintf(cur, sizeof(cur), "%02d:%02d", h, mi);

    auto dlg_sz = dialog_size(260, 120);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, is_date ? "Set Date (YYYY-MM-DD)" : "Set Time (HH:MM 24h)");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* input = lv_textarea_create(dlg);
    lv_obj_set_size(input, dlg_sz.w - 16, 28);
    lv_obj_align(input, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(input, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(input, 0, 0);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_text(input, cur);
    apply_focus_style(input);

    // Focus immediately so the physical keyboard works without tapping the field
    lv_group_t* grp = lv_group_get_default();
    if (grp) {
        lv_group_add_obj(grp, input);
        lv_group_focus_obj(input);
    }

    lv_obj_t* fb = lv_label_create(dlg);
    lv_obj_set_style_text_color(fb, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(fb, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(fb, LV_ALIGN_BOTTOM_MID, 0, -28);

    lv_obj_t* cancel_btn = lv_btn_create(dlg);
    lv_obj_set_size(cancel_btn, 72, 24);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(cancel_btn, 0, 0);
    lv_obj_t* cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_font(cl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_set_style_text_font(sl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(sl);

    // ctx lives until the dialog is deleted (see LV_EVENT_DELETE below)
    auto* ctx = new DateTimeDialogCtx{ input, fb, is_date };
    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (DateTimeDialogCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* ctx = (DateTimeDialogCtx*)lv_event_get_user_data(e);
        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
        const char* s = lv_textarea_get_text(ctx->input);

        bool valid = false;
        uint32_t epoch = 0;

        if (ctx->is_date) {
            int ny, nm, nd;
            // Days in month lookup: jan=31, feb=28, mar=31, ...
            static const uint8_t DAYS_IN_MONTH[] = {31,28,31,30,31,30,31,31,30,31,30,31};
            if (sscanf(s, "%d-%d-%d", &ny, &nm, &nd) == 3 &&
                ny > 2020 && nm >= 1 && nm <= 12 && nd >= 1) {
                // Check days in month (with leap year for February)
                uint8_t max_days = DAYS_IN_MONTH[nm - 1];
                if (nm == 2 && (ny % 4 == 0 && (ny % 100 != 0 || ny % 400 == 0)))
                    max_days = 29;
                if (nd <= max_days) {
                    int cy, cmo, cd, ch, cmi;
                    sigurdos::mesh::getCurrentLocalDateTime(&cy, &cmo, &cd, &ch, &cmi);
                    epoch = sigurdos::mesh::makeEpoch(ny, nm, nd, ch, cmi);
                    valid = true;
                } else {
                    lv_label_set_text(ctx->feedback, "Invalid day for month");
                }
            } else {
                lv_label_set_text(ctx->feedback, "Invalid date (YYYY-MM-DD)");
            }
        } else {
            int nh, nm_v;
            if (sscanf(s, "%d:%d", &nh, &nm_v) == 2 &&
                nh >= 0 && nh <= 23 && nm_v >= 0 && nm_v <= 59) {
                int cy, cmo, cd, ch, cmi;
                sigurdos::mesh::getCurrentLocalDateTime(&cy, &cmo, &cd, &ch, &cmi);
                epoch = sigurdos::mesh::makeEpoch(cy, cmo, cd, nh, nm_v);
                valid = true;
            } else {
                lv_label_set_text(ctx->feedback, "Invalid time (HH:MM)");
            }
        }

        if (valid && sigurdos::mesh::setSystemTime(epoch)) {
            int yy, mmo, dd, hh, mmi;
            sigurdos::mesh::getCurrentLocalDateTime(&yy, &mmo, &dd, &hh, &mmi);
            char dbuf[32], tbuf[16];
            snprintf(dbuf, sizeof(dbuf), "  Date: %04d-%02d-%02d", yy, mmo, dd);
            snprintf(tbuf, sizeof(tbuf), "  Time: %02d:%02d", hh, mmi);
            update_row_label(g_date_row, dbuf);
            update_row_label(g_time_row, tbuf);
            home_screen_update_time(tbuf);
            lv_obj_del_async(dlg);
        }
    }, LV_EVENT_CLICKED, (void*)ctx);
}

static lv_obj_t* g_backlight_row   = nullptr;
static lv_obj_t* g_auto_off_row    = nullptr;
static lv_obj_t* g_chat_history_row = nullptr;

struct BacklightCtx {
    lv_obj_t* value_label;
    lv_obj_t* row_label;
    int       brightness;
};

struct DisplayBrightnessCtx {
    lv_obj_t* value_label;
    lv_obj_t* row_label;
    int       brightness;
};

struct ChatHistoryCapCtx {
    lv_obj_t* value_label;
    lv_obj_t* row_label;
    int       cap;
};

static void chat_message_cap_dialog(lv_obj_t* parent, lv_obj_t* row_label)
{
    auto dlg_sz = dialog_size(220, 120);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Chat Message Cap");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    int cap = (int)chat_screen_get_message_cap();
    lv_obj_t* cap_lbl = lv_label_create(dlg);
    char cap_buf[24];
    snprintf(cap_buf, sizeof(cap_buf), "%d msgs", cap);
    lv_label_set_text(cap_lbl, cap_buf);
    lv_obj_set_style_text_color(cap_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(cap_lbl, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(cap_lbl, LV_ALIGN_CENTER, 0, -2);

    auto* minus_btn = lv_btn_create(dlg);
    lv_obj_set_size(minus_btn, 40, 28);
    lv_obj_align(minus_btn, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_bg_color(minus_btn, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(minus_btn, 0, 0);
    lv_obj_t* ml = lv_label_create(minus_btn);
    lv_label_set_text(ml, "-");
    lv_obj_center(ml);

    auto* plus_btn = lv_btn_create(dlg);
    lv_obj_set_size(plus_btn, 40, 28);
    lv_obj_align(plus_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(plus_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(plus_btn, 0, 0);
    lv_obj_t* pl = lv_label_create(plus_btn);
    lv_label_set_text(pl, "+");
    lv_obj_center(pl);

    auto* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_center(sl);

    auto* ctx = new ChatHistoryCapCtx{ cap_lbl, row_label, cap };

    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (ChatHistoryCapCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, (void*)ctx);

    lv_obj_add_event_cb(minus_btn, [](lv_event_t* e) {
        auto* c = (ChatHistoryCapCtx*)lv_event_get_user_data(e);
        c->cap = c->cap > 16 ? c->cap - 16 : 8;
        chat_screen_set_message_cap((uint16_t)c->cap);
        c->cap = (int)chat_screen_get_message_cap();
        char b[24];
        snprintf(b, sizeof(b), "%d msgs", c->cap);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
        auto* c = (ChatHistoryCapCtx*)lv_event_get_user_data(e);
        c->cap += 16;
        chat_screen_set_message_cap((uint16_t)c->cap);
        c->cap = (int)chat_screen_get_message_cap();
        char b[24];
        snprintf(b, sizeof(b), "%d msgs", c->cap);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* c = (ChatHistoryCapCtx*)lv_event_get_user_data(e);
        chat_screen_set_message_cap((uint16_t)c->cap);

        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Chat history: %d messages", c->cap);
        update_row_label(c->row_label, row_buf);

        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, (void*)ctx);
}

static void backlight_dialog(lv_obj_t* parent, lv_obj_t* row_label)
{
    auto dlg_sz = dialog_size(220, 120);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Keyboard Backlight");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    int brightness = p.kbd_backlight;

    lv_obj_t* val_lbl = lv_label_create(dlg);
    char val_buf[24];
    snprintf(val_buf, sizeof(val_buf), "%d (%d%%)", brightness, brightness * 100 / 255);
    lv_label_set_text(val_lbl, val_buf);
    lv_obj_set_style_text_color(val_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(val_lbl, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(val_lbl, LV_ALIGN_CENTER, 0, -2);

    auto* minus_btn = lv_btn_create(dlg);
    lv_obj_set_size(minus_btn, 40, 28);
    lv_obj_align(minus_btn, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_bg_color(minus_btn, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(minus_btn, 0, 0);
    lv_obj_t* ml = lv_label_create(minus_btn);
    lv_label_set_text(ml, "-");
    lv_obj_center(ml);

    auto* plus_btn = lv_btn_create(dlg);
    lv_obj_set_size(plus_btn, 40, 28);
    lv_obj_align(plus_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(plus_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(plus_btn, 0, 0);
    lv_obj_t* pl = lv_label_create(plus_btn);
    lv_label_set_text(pl, "+");
    lv_obj_center(pl);

    auto* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_center(sl);

    auto* ctx = new BacklightCtx{ val_lbl, row_label, brightness };

    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (BacklightCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, (void*)ctx);

    lv_obj_add_event_cb(minus_btn, [](lv_event_t* e) {
        auto* c = (BacklightCtx*)lv_event_get_user_data(e);
        if (c->brightness >= 25) c->brightness -= 25;
        else c->brightness = 0;
        sigurdos_keyboard_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
        auto* c = (BacklightCtx*)lv_event_get_user_data(e);
        if (c->brightness <= 230) c->brightness += 25;
        else c->brightness = 255;
        sigurdos_keyboard_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* c = (BacklightCtx*)lv_event_get_user_data(e);
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.kbd_backlight = (uint8_t)c->brightness;
        sigurdos::prefs_set(np);
        sigurdos_keyboard_set_default_brightness(c->brightness);

        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Keyboard BL: %d (%d%%)",
                 c->brightness, c->brightness * 100 / 255);
        update_row_label(c->row_label, row_buf);

        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, (void*)ctx);
}

// ════════════════════════════════════════════════════════
// Auto-off timeout selector dialog
// ════════════════════════════════════════════════════════
static void auto_off_dialog(lv_obj_t* parent, lv_obj_t* row_label)
{
    auto dlg_sz = dialog_size(220, 160);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Auto-off Timeout");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    struct Opt { const char* label; uint16_t value; };
    static constexpr Opt OPTIONS[] = {
        {"Off",   0},
        {"15s",  15},
        {"30s",  30},
        {"1m",   60},
        {"2m",  120},
    };

    int btn_w = 68;
    int btn_h = 28;
    int gap_x = 10;
    int gap_y = 8;
    int start_y = 32;
    int total_w = 3 * btn_w + 2 * gap_x;
    int start_x = (dlg_sz.w - total_w) / 2;

    uint16_t current = sigurdos::prefs_get().auto_off_timeout;
    static lv_obj_t* selected = nullptr;

    for (int i = 0; i < 5; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = start_x + col * (btn_w + gap_x);
        int y = start_y + row * (btn_h + gap_y);

        lv_obj_t* btn = lv_btn_create(dlg);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_bg_color(btn,
            (OPTIONS[i].value == current) ? lv_color_hex(ACCENT) : lv_color_hex(BG_TERTIARY), 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, OPTIONS[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            lv_obj_t* clicked = (lv_obj_t*)lv_event_get_target(e);
            if (selected) {
                lv_obj_set_style_bg_color(selected, lv_color_hex(BG_TERTIARY), 0);
            }
            lv_obj_set_style_bg_color(clicked, lv_color_hex(ACCENT), 0);
            selected = clicked;
        }, LV_EVENT_CLICKED, nullptr);

        if (OPTIONS[i].value == current) {
            selected = btn;
        }
    }

    lv_obj_t* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_center(sl);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        lv_obj_t* row_lbl = (lv_obj_t*)lv_event_get_user_data(e);
        if (!selected) return;
        lv_obj_t* lbl = lv_obj_get_child(selected, 0);
        if (!lbl) return;
        const char* label = lv_label_get_text(lbl);

        uint16_t value = 30;
        for (auto& opt : OPTIONS) {
            if (strcmp(label, opt.label) == 0) {
                value = opt.value;
                break;
            }
        }

        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.auto_off_timeout = value;
        sigurdos::prefs_set(np);
        sigurdos_display_reset_auto_off();

        char row_buf[64];
        if (value == 0) {
            snprintf(row_buf, sizeof(row_buf), "  Auto-off: Off");
        } else {
            snprintf(row_buf, sizeof(row_buf), "  Auto-off: %ds", value);
        }
        update_row_label(row_lbl, row_buf);

        selected = nullptr;
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, (void*)row_label);

    lv_obj_add_event_cb(dlg, [](lv_event_t*) {
        selected = nullptr;
    }, LV_EVENT_DELETE, nullptr);
}

// ════════════════════════════════════════════════════════
// Display brightness dialog
// ════════════════════════════════════════════════════════
static void display_brightness_dialog(lv_obj_t* parent, lv_obj_t* row_label)
{
    auto dlg_sz = dialog_size(220, 120);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Display Brightness");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    int brightness = p.display_brightness;

    lv_obj_t* val_lbl = lv_label_create(dlg);
    char val_buf[24];
    snprintf(val_buf, sizeof(val_buf), "%d (%d%%)", brightness, brightness * 100 / 255);
    lv_label_set_text(val_lbl, val_buf);
    lv_obj_set_style_text_color(val_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(val_lbl, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(val_lbl, LV_ALIGN_CENTER, 0, -2);

    auto* minus_btn = lv_btn_create(dlg);
    lv_obj_set_size(minus_btn, 40, 28);
    lv_obj_align(minus_btn, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_bg_color(minus_btn, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(minus_btn, 0, 0);
    lv_obj_t* ml = lv_label_create(minus_btn);
    lv_label_set_text(ml, "-");
    lv_obj_center(ml);

    auto* plus_btn = lv_btn_create(dlg);
    lv_obj_set_size(plus_btn, 40, 28);
    lv_obj_align(plus_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(plus_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(plus_btn, 0, 0);
    lv_obj_t* pl = lv_label_create(plus_btn);
    lv_label_set_text(pl, "+");
    lv_obj_center(pl);

    auto* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl_lbl = lv_label_create(set_btn);
    lv_label_set_text(sl_lbl, "Set");
    lv_obj_center(sl_lbl);

    auto* ctx = new DisplayBrightnessCtx{ val_lbl, row_label, brightness };

    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (DisplayBrightnessCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, (void*)ctx);

    lv_obj_add_event_cb(minus_btn, [](lv_event_t* e) {
        auto* c = (DisplayBrightnessCtx*)lv_event_get_user_data(e);
        if (c->brightness > 20) c->brightness -= 25;
        if (c->brightness < 20) c->brightness = 20;
        sigurdos_display_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
        auto* c = (DisplayBrightnessCtx*)lv_event_get_user_data(e);
        if (c->brightness < 240) c->brightness += 25;
        if (c->brightness > 240) c->brightness = 240;
        sigurdos_display_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* c = (DisplayBrightnessCtx*)lv_event_get_user_data(e);
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.display_brightness = (uint8_t)c->brightness;
        sigurdos::prefs_set(np);
        sigurdos_display_set_brightness(c->brightness);

        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Display: %d (%d%%)",
                 c->brightness, c->brightness * 100 / 255);
        update_row_label(c->row_label, row_buf);

        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, (void*)ctx);
}

// Settings — category menu + sub-screens
// ════════════════════════════════════════════════════════

void settings_radio_show()
{
    lv_obj_t* scr = make_screen_full("Radio / Mesh");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ON);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    char buf[128];
    int row = 0;

    // Radio config
    if (p.configured) {
        snprintf(buf, sizeof(buf), "  Radio: %.3f MHz / %.1f kHz / SF%d / %d dBm",
                 p.freq, p.bw, p.sf, p.tx_power_dbm);
    } else {
        snprintf(buf, sizeof(buf), "  Radio: NOT CONFIGURED");
    }
    lv_obj_t* btn_rf = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
    lv_obj_set_style_bg_color(btn_rf, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_rf, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_rf, lv_color_hex(TEXT_PRIMARY), 0);
    if (!p.configured) {
        lv_obj_set_style_bg_color(btn_rf, lv_color_hex(0x4a2020), 0);
        lv_obj_set_style_bg_color(btn_rf, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    }
    lv_obj_add_event_cb(btn_rf, [](lv_event_t*) {
        radio_setup_screen_show();
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Flood max hops
    {
        static constexpr uint8_t FLOOD_HOPS_VALUES[] = {0, 3, 5, 10, 20, 50};
        static constexpr const char* FLOOD_HOPS_LABELS[] = {"No limit", "3", "5", "10", "20", "50"};
        static constexpr int NUM_FLOOD_HOPS = 6;
        int cur_idx = 0;
        for (int i = 0; i < NUM_FLOOD_HOPS; i++) {
            if (p.flood_max_hops == FLOOD_HOPS_VALUES[i]) { cur_idx = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Flood max hops: %s", FLOOD_HOPS_LABELS[cur_idx]);
        lv_obj_t* btn_flood = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_flood, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_flood, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_flood, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_flood, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            int idx = 0;
            for (int i = 0; i < 6; i++) {
                if (np.flood_max_hops == (uint8_t[]){0,3,5,10,20,50}[i]) { idx = i; break; }
            }
            idx = (idx + 1) % 6;
            np.flood_max_hops = (uint8_t[]){0,3,5,10,20,50}[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Flood max hops: %s",
                     (const char*[]){"No limit","3","5","10","20","50"}[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Auto-add contact types
    {
        static constexpr uint8_t AA_CONFIG_VALUES[] = {0x1E, 0x06, 0x0A, 0x02};
        static constexpr const char* AA_CONFIG_LABELS[] = {
            "All types", "Chat+Repeater", "Chat+Room", "Chat only"};
        static constexpr int NUM_AA_CONFIG = 4;
        int cur_aa = 0;
        for (int i = 0; i < NUM_AA_CONFIG; i++) {
            if (p.autoadd_config == AA_CONFIG_VALUES[i]) { cur_aa = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Auto-add: %s", AA_CONFIG_LABELS[cur_aa]);
        lv_obj_t* btn_aa = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_aa, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_aa, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_aa, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_aa, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr uint8_t VALS[] = {0x1E, 0x06, 0x0A, 0x02};
            static constexpr const char* LABELS[] = {
                "All types", "Chat+Repeater", "Chat+Room", "Chat only"};
            static constexpr int N = 4;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (np.autoadd_config == VALS[i]) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.autoadd_config = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Auto-add: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Auto-add max hops
    {
        static constexpr uint8_t AA_HOPS_VALUES[] = {0, 3, 5, 10, 20, 50};
        static constexpr const char* AA_HOPS_LABELS[] = {"No limit", "3", "5", "10", "20", "50"};
        static constexpr int NUM_AA_HOPS = 6;
        int cur_ah = 0;
        for (int i = 0; i < NUM_AA_HOPS; i++) {
            if (p.autoadd_max_hops == AA_HOPS_VALUES[i]) { cur_ah = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Add max hops: %s", AA_HOPS_LABELS[cur_ah]);
        lv_obj_t* btn_ah = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_ah, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_ah, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_ah, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_ah, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr uint8_t VALS[] = {0, 3, 5, 10, 20, 50};
            static constexpr const char* LABELS[] = {"No limit","3","5","10","20","50"};
            static constexpr int N = 6;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (np.autoadd_max_hops == VALS[i]) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.autoadd_max_hops = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Add max hops: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // RX delay base
    {
        static constexpr float RX_DELAY_VALUES[] = {0.0f, 5.0f, 10.0f, 15.0f, 20.0f};
        static constexpr const char* RX_DELAY_LABELS[] = {"0 (off)", "5", "10", "15", "20"};
        static constexpr int NUM_RX_DELAY = 5;
        int cur_rx = 0;
        for (int i = 0; i < NUM_RX_DELAY; i++) {
            if (fabsf(p.rx_delay_base - RX_DELAY_VALUES[i]) < 0.1f) { cur_rx = i; break; }
        }
        snprintf(buf, sizeof(buf), "  RX delay base: %s", RX_DELAY_LABELS[cur_rx]);
        lv_obj_t* btn_rx = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_rx, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_rx, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_rx, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_rx, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr float VALS[] = {0.0f, 5.0f, 10.0f, 15.0f, 20.0f};
            static constexpr const char* LABELS[] = {"0 (off)","5","10","15","20"};
            static constexpr int N = 5;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (fabsf(np.rx_delay_base - VALS[i]) < 0.1f) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.rx_delay_base = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  RX delay base: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // TX delay factor
    {
        static constexpr float TX_DELAY_VALUES[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
        static constexpr const char* TX_DELAY_LABELS[] = {"0 (off)", "0.5", "1.0", "1.5", "2.0"};
        static constexpr int NUM_TX_DELAY = 5;
        int cur_tx = 0;
        for (int i = 0; i < NUM_TX_DELAY; i++) {
            if (fabsf(p.tx_delay_factor - TX_DELAY_VALUES[i]) < 0.1f) { cur_tx = i; break; }
        }
        snprintf(buf, sizeof(buf), "  TX delay factor: %s", TX_DELAY_LABELS[cur_tx]);
        lv_obj_t* btn_tx = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_tx, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_tx, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_tx, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_tx, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr float VALS[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
            static constexpr const char* LABELS[] = {"0 (off)","0.5","1.0","1.5","2.0"};
            static constexpr int N = 5;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (fabsf(np.tx_delay_factor - VALS[i]) < 0.1f) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.tx_delay_factor = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  TX delay factor: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Direct TX delay factor
    {
        static constexpr float DIR_TX_DELAY_VALUES[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
        static constexpr const char* DIR_TX_DELAY_LABELS[] = {"0 (off)", "0.5", "1.0", "1.5", "2.0"};
        static constexpr int NUM_DIR_TX_DELAY = 5;
        int cur_dir = 0;
        for (int i = 0; i < NUM_DIR_TX_DELAY; i++) {
            if (fabsf(p.direct_tx_delay_factor - DIR_TX_DELAY_VALUES[i]) < 0.1f) { cur_dir = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Direct TX delay: %s", DIR_TX_DELAY_LABELS[cur_dir]);
        lv_obj_t* btn_dir = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_dir, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_dir, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_dir, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_dir, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr float VALS[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
            static constexpr const char* LABELS[] = {"0 (off)","0.5","1.0","1.5","2.0"};
            static constexpr int N = 5;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (fabsf(np.direct_tx_delay_factor - VALS[i]) < 0.1f) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.direct_tx_delay_factor = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Direct TX delay: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Auto-advert interval (hours between adverts)
    {
        static constexpr uint16_t ADV_INT_VALUES[] = {0, 24, 72, 168};
        static constexpr const char* ADV_INT_LABELS[] = {"Disabled", "24 hours", "72 hours", "168 hours"};
        static constexpr int NUM_ADV_INT = 4;
        int cur_adv = 0;
        for (int i = 0; i < NUM_ADV_INT; i++) {
            if (p.advert_interval_h == ADV_INT_VALUES[i]) { cur_adv = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Auto-advert: %s", ADV_INT_LABELS[cur_adv]);
        lv_obj_t* btn_adv = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_adv, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_adv, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_adv, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_adv, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr uint16_t VALS[] = {0, 24, 72, 168};
            static constexpr const char* LABELS[] = {"Disabled","24 hours","72 hours","168 hours"};
            static constexpr int N = 4;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (np.advert_interval_h == VALS[i]) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.advert_interval_h = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Auto-advert: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Node type (advert_type)
    {
        static constexpr uint8_t TYPE_VALUES[] = {1, 2, 3, 4};
        static constexpr const char* TYPE_LABELS[] = {"Chat", "Repeater", "Room Server", "Sensor"};
        static constexpr int NUM_TYPE = 4;
        int cur_type = 0;
        for (int i = 0; i < NUM_TYPE; i++) {
            if (p.advert_type == TYPE_VALUES[i]) { cur_type = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Node type: %s", TYPE_LABELS[cur_type]);
        lv_obj_t* btn_type = lv_list_add_btn(list, LV_SYMBOL_HOME, buf);
        lv_obj_set_style_bg_color(btn_type, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_type, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_type, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_type, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr uint8_t VALS[] = {1, 2, 3, 4};
            static constexpr const char* LABELS[] = {"Chat", "Repeater", "Room Server", "Sensor"};
            static constexpr int N = 4;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (np.advert_type == VALS[i]) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.advert_type = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Node type: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Duty cycle
    {
        static constexpr uint8_t DUTY_VALUES[] = {0, 1, 5, 10, 25, 50, 100};
        static constexpr const char* DUTY_LABELS[] = {"Disabled", "1%", "5%", "10%", "25%", "50%", "100%"};
        static constexpr int NUM_DUTY = 7;
        int cur_dc = 0;
        for (int i = 0; i < NUM_DUTY; i++) {
            if (p.duty_cycle == DUTY_VALUES[i]) { cur_dc = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Duty cycle: %s", DUTY_LABELS[cur_dc]);
        lv_obj_t* btn_dc = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_dc, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_dc, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_dc, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_dc, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr uint8_t VALS[] = {0, 1, 5, 10, 25, 50, 100};
            static constexpr const char* LABELS[] = {"Disabled","1%","5%","10%","25%","50%","100%"};
            static constexpr int N = 7;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (np.duty_cycle == VALS[i]) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.duty_cycle = VALS[idx];
            sigurdos::prefs_set(np);
            sigurdos::mesh::setDutyCycle(VALS[idx]);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Duty cycle: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Client repeat (opportunistic relay)
    {
        snprintf(buf, sizeof(buf), "  Client repeat: %s", p.client_repeat ? "ON" : "OFF");
        lv_obj_t* btn_cr = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_cr, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_cr, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_cr, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_cr, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            np.client_repeat = np.client_repeat ? 0 : 1;
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Client repeat: %s", np.client_repeat ? "ON" : "OFF");
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Regions (flood scope)
    {
        const char* active = sigurdos::mesh::getActiveRegion();
        if (!active || active[0] == '\0') {
            snprintf(buf, sizeof(buf), "  Regions: Public (unscoped)");
        } else {
            snprintf(buf, sizeof(buf), "  Regions: %s", active);
        }
        lv_obj_t* btn_reg = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_reg, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_reg, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_reg, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_reg, [](lv_event_t*) {
            navigate_to(Screen::Regions);
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    show_screen(scr);
}

void settings_display_show()
{
    lv_obj_t* scr = make_screen_full("Display / UI");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    char buf[128];
    int row = 0;

    // Keyboard backlight
    snprintf(buf, sizeof(buf), "  Keyboard BL: %d (%d%%)", p.kbd_backlight, p.kbd_backlight * 100 / 255);
    lv_obj_t* btn_bl = lv_list_add_btn(list, LV_SYMBOL_KEYBOARD, buf);
    lv_obj_set_style_bg_color(btn_bl, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_bl, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_bl, lv_color_hex(TEXT_PRIMARY), 0);
    g_backlight_row = btn_bl;
    lv_obj_add_event_cb(btn_bl, [](lv_event_t* e) {
        backlight_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                         (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Display brightness
    snprintf(buf, sizeof(buf), "  Display: %d (%d%%)", p.display_brightness, p.display_brightness * 100 / 255);
    lv_obj_t* btn_disp = lv_list_add_btn(list, LV_SYMBOL_IMAGE, buf);
    lv_obj_set_style_bg_color(btn_disp, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_disp, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_disp, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_disp, [](lv_event_t* e) {
        display_brightness_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                                 (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Auto-off timeout
    if (p.auto_off_timeout == 0) {
        snprintf(buf, sizeof(buf), "  Auto-off: Off");
    } else {
        snprintf(buf, sizeof(buf), "  Auto-off: %ds", p.auto_off_timeout);
    }
    lv_obj_t* btn_auto_off = lv_list_add_btn(list, LV_SYMBOL_IMAGE, buf);
    lv_obj_set_style_bg_color(btn_auto_off, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_auto_off, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_auto_off, lv_color_hex(TEXT_PRIMARY), 0);
    g_auto_off_row = btn_auto_off;
    lv_obj_add_event_cb(btn_auto_off, [](lv_event_t* e) {
        auto_off_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                       (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Chat history cap
    snprintf(buf, sizeof(buf), "  Chat history: %d messages", chat_screen_get_message_cap());
    lv_obj_t* btn_chat_cap = lv_list_add_btn(list, LV_SYMBOL_LIST, buf);
    lv_obj_set_style_bg_color(btn_chat_cap, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_chat_cap, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_chat_cap, lv_color_hex(TEXT_PRIMARY), 0);
    g_chat_history_row = btn_chat_cap;
    lv_obj_add_event_cb(btn_chat_cap, [](lv_event_t* e) {
        chat_message_cap_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                               (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Theme selector
    {
        uint8_t cur_theme = sigurdos::prefs_get().theme_id;
        if (cur_theme >= NUM_THEMES) cur_theme = 0;
        snprintf(buf, sizeof(buf), "  Theme: %s", THEMES[cur_theme].name);
        lv_obj_t* btn_theme = lv_list_add_btn(list, LV_SYMBOL_IMAGE, buf);
        lv_obj_set_style_bg_color(btn_theme, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_theme, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_theme, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_theme, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            np.theme_id = (np.theme_id + 1) % NUM_THEMES;
            theme_apply(np.theme_id);
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Theme: %s", THEMES[np.theme_id].name);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        g_backlight_row = nullptr;
        g_chat_history_row = nullptr;
        g_auto_off_row = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    show_screen(scr);
}

void settings_system_show()
{
    lv_obj_t* scr = make_screen_full("System");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    char buf[128];
    int row = 0;

    // Node name
    snprintf(buf, sizeof(buf), "  Name: %s", p.node_name);
    lv_obj_t* r0 = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
    lv_obj_set_style_bg_color(r0, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r0, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r0, lv_color_hex(TEXT_PRIMARY), 0);
    row++;

    // SD Card
    snprintf(buf, sizeof(buf), "  SD Card: %s", sigurdos_sdcard_mounted() ? "Mounted" : "Not mounted");
    lv_obj_t* r1 = lv_list_add_btn(list, LV_SYMBOL_SD_CARD, buf);
    lv_obj_set_style_bg_color(r1, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r1, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r1, lv_color_hex(TEXT_PRIMARY), 0);
    row++;

    // Date
    {
        int y, mo, d, h, mi;
        sigurdos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);
        snprintf(buf, sizeof(buf), "  Date: %04d-%02d-%02d", y, mo, d);
        lv_obj_t* btn_date = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_date, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_date, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_date, lv_color_hex(TEXT_PRIMARY), 0);
        g_date_row = btn_date;
        lv_obj_add_event_cb(btn_date, [](lv_event_t* e) {
            datetime_set_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)), true);
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Time
    {
        int y, mo, d, h, mi;
        sigurdos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);
        snprintf(buf, sizeof(buf), "  Time: %02d:%02d", h, mi);
        lv_obj_t* btn_time = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_time, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_time, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_time, lv_color_hex(TEXT_PRIMARY), 0);
        g_time_row = btn_time;
        lv_obj_add_event_cb(btn_time, [](lv_event_t* e) {
            datetime_set_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)), false);
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Run Setup Wizard
    lv_obj_t* btn_wizard = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "  Run Setup Wizard");
    lv_obj_set_style_bg_color(btn_wizard, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_wizard, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_wizard, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_wizard, [](lv_event_t*) {
        navigate_to(Screen::Onboarding);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Device PIN
    {
        bool has_pin = (p.device_pin != 0);
        snprintf(buf, sizeof(buf), "  Device PIN: %s", has_pin ? "Change" : "Set");
        lv_obj_t* btn_pin = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_pin, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_pin, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_pin, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_pin, [](lv_event_t* e) {
            lv_obj_t* scr_pin = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
            auto dlg_sz = dialog_size(260, 160);
            lv_obj_t* dlg = lv_obj_create(scr_pin);
            lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
            lv_obj_center(dlg);
            lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
            lv_obj_set_style_radius(dlg, 0, 0);
            lv_obj_set_style_border_width(dlg, 2, 0);
            lv_obj_set_style_border_color(dlg, lv_color_hex(DIVIDER), 0);
            lv_obj_set_style_pad_all(dlg, 8, 0);

            lv_obj_t* title = lv_label_create(dlg);
            lv_label_set_text(title, "Set/Change PIN");
            lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
            lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

            lv_obj_t* msg = lv_label_create(dlg);
            lv_label_set_text(msg, "Enter new 4-digit PIN:");
            lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(msg, LV_ALIGN_TOP_LEFT, 8, 24);

            lv_obj_t* pin_ta = lv_textarea_create(dlg);
            lv_obj_set_size(pin_ta, 120, 30);
            lv_obj_align(pin_ta, LV_ALIGN_TOP_MID, 0, 48);
            lv_textarea_set_password_mode(pin_ta, true);
            lv_textarea_set_one_line(pin_ta, true);
            lv_textarea_set_max_length(pin_ta, 4);
            lv_textarea_set_accepted_chars(pin_ta, "0123456789");
            lv_obj_set_style_text_align(pin_ta, LV_TEXT_ALIGN_CENTER, 0);
            apply_pixel_input(pin_ta);

            // Save button
            lv_obj_t* save_btn = lv_btn_create(dlg);
            lv_obj_set_size(save_btn, 80, 26);
            lv_obj_align(save_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
            apply_pixel_btn(save_btn);
            lv_obj_t* save_lbl = lv_label_create(save_btn);
            lv_label_set_text(save_lbl, "Save");
            lv_obj_center(save_lbl);
            lv_obj_add_event_cb(save_btn, [](lv_event_t* ev) {
                lv_obj_t* ta = (lv_obj_t*)lv_event_get_user_data(ev);
                lv_obj_t* dlg = lv_obj_get_parent(ta);
                const char* pin_str = lv_textarea_get_text(ta);
                if (pin_str && strlen(pin_str) >= 4) {
                    auto p = sigurdos::prefs_get();
                    p.device_pin = (uint32_t)atoi(pin_str);
                    sigurdos::prefs_set(p);
                }
                lv_obj_del_async(dlg);
            }, LV_EVENT_CLICKED, pin_ta);

            // Cancel button
            lv_obj_t* cancel_btn = lv_btn_create(dlg);
            lv_obj_set_size(cancel_btn, 80, 26);
            lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
            apply_pixel_btn_outline(cancel_btn);
            lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
            lv_label_set_text(cancel_lbl, "Cancel");
            lv_obj_center(cancel_lbl);
            lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ev) {
                lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
            }, LV_EVENT_CLICKED, nullptr);

            // If PIN is already set, add a "Clear PIN" button
            if (sigurdos::prefs_get().device_pin != 0) {
                lv_obj_t* clear_btn = lv_btn_create(dlg);
                lv_obj_set_size(clear_btn, 80, 26);
                lv_obj_align(clear_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
                lv_obj_set_style_bg_color(clear_btn, lv_color_hex(ACCENT_RED), 0);
                lv_obj_set_style_radius(clear_btn, 0, 0);
                lv_obj_t* clear_lbl = lv_label_create(clear_btn);
                lv_label_set_text(clear_lbl, "Clear");
                lv_obj_center(clear_lbl);
                lv_obj_add_event_cb(clear_btn, [](lv_event_t* ev) {
                    lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev));
                    auto p = sigurdos::prefs_get();
                    p.device_pin = 0;
                    sigurdos::prefs_set(p);
                    lv_obj_del_async(dlg);
                }, LV_EVENT_CLICKED, nullptr);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // WiFi SSID / Password (for GitHub OTA)
    {
        const char* ssid_label = p.wifi_ssid[0]
            ? p.wifi_ssid : "Not set";
        snprintf(buf, sizeof(buf), "  WiFi: %s", ssid_label);
        lv_obj_t* btn_wifi = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_wifi, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_wifi, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_wifi, [](lv_event_t*) {
            navigate_to(Screen::WiFiNetworks);
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // OTA firmware update (WiFi AP + web upload)
    lv_obj_t* btn_ota = lv_list_add_btn(list, LV_SYMBOL_WIFI, "  OTA Update");
    lv_obj_set_style_bg_color(btn_ota, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_ota, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_ota, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_ota, [](lv_event_t* e) {
        lv_obj_t* scr_ota = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));

        if (sigurdos_is_under_launcher()) {
            auto dlg_sz = dialog_size(260, 80);
            lv_obj_t* dlg = lv_obj_create(scr_ota);
            lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
            lv_obj_center(dlg);
            lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
            lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_border_width(dlg, 2, 0);
            lv_obj_set_style_radius(dlg, 0, 0);
            lv_obj_set_style_pad_all(dlg, 8, 0);

            lv_obj_t* msg = lv_label_create(dlg);
            lv_label_set_text(msg, "Update SigurdOS\nthrough Launcher instead");
            lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

            lv_obj_t* close_btn = lv_btn_create(dlg);
            lv_obj_set_size(close_btn, 80, 24);
            lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_obj_set_style_bg_color(close_btn, lv_color_hex(BG_INPUT), 0);
            lv_obj_set_style_radius(close_btn, 0, 0);
            lv_obj_t* cl = lv_label_create(close_btn);
            lv_label_set_text(cl, "OK");
            lv_obj_center(cl);
            lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
                lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
            }, LV_EVENT_CLICKED, nullptr);
            return;
        }

        auto dlg_sz = dialog_size(260, 120);
        lv_obj_t* dlg = lv_obj_create(scr_ota);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);

        sigurdos::ota::start("SigurdOS-OTA");
        const char* ip = sigurdos::ota::getIP();

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, "OTA Update Active");
        lv_obj_set_style_text_color(title, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        char info[80];
        snprintf(info, sizeof(info),
            "WiFi: SigurdOS-OTA\n"
            "IP: %s\n"
            "Open browser, upload firmware.bin", ip);
        lv_obj_t* msg = lv_label_create(dlg);
        lv_label_set_text(msg, info);
        lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t* close_btn = lv_btn_create(dlg);
        lv_obj_set_size(close_btn, 80, 24);
        lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_radius(close_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(close_btn);
        lv_label_set_text(cl, "Close");
        lv_obj_center(cl);
        lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
            lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // OTA release channel — cycling through main → dev → latest
    {
        const char* branches[] = {"main", "dev", "latest"};
        int n_branches = 3;
        int current = 0;
        const char* br = p.ota_branch;
        for (int i = 0; i < n_branches; i++) {
            if (strcmp(br, branches[i]) == 0) { current = i; break; }
        }
        snprintf(buf, sizeof(buf), "  OTA Branch: %s", branches[current]);
        lv_obj_t* btn_branch = lv_list_add_btn(list, LV_SYMBOL_REFRESH, buf);
        lv_obj_set_style_bg_color(btn_branch, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_branch, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_branch, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_branch, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            NodePrefs np = prefs_get();
            const char* branches[] = {"main", "dev", "latest"};
            int n_branches = 3;
            int current = 0;
            for (int i = 0; i < n_branches; i++) {
                if (strcmp(np.ota_branch, branches[i]) == 0) { current = (i + 1) % n_branches; break; }
            }
            strncpy(np.ota_branch, branches[current], sizeof(np.ota_branch) - 1);
            np.ota_branch[sizeof(np.ota_branch) - 1] = '\0';
            prefs_set(np);
            // Update button label without rebuilding the screen
            lv_obj_t* label = lv_obj_get_child(btn, 0);
            if (label) {
                char lbl[48];
                snprintf(lbl, sizeof(lbl), "  OTA Branch: %s", branches[current]);
                lv_label_set_text(label, lbl);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Pre-release toggle
    {
        snprintf(buf, sizeof(buf), "  Pre-releases: %s",
                 p.ota_allow_prerelease ? "ON" : "OFF");
        lv_obj_t* btn_pre = lv_list_add_btn(list, LV_SYMBOL_EDIT, buf);
        lv_obj_set_style_bg_color(btn_pre, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_pre, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_pre, lv_color_hex(
            p.ota_allow_prerelease ? ACCENT : TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_pre, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            NodePrefs np = prefs_get();
            np.ota_allow_prerelease = !np.ota_allow_prerelease;
            prefs_set(np);
            // Update button label without rebuilding the screen
            lv_obj_t* label = lv_obj_get_child(btn, 0);
            if (label) {
                lv_label_set_text(label, np.ota_allow_prerelease
                    ? "  Pre-releases: ON" : "  Pre-releases: OFF");
                lv_obj_set_style_text_color(label, lv_color_hex(
                    np.ota_allow_prerelease ? ACCENT : TEXT_PRIMARY), 0);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // OTA from GitHub (WiFi STA + download)
    lv_obj_t* btn_gh_ota = lv_list_add_btn(list, LV_SYMBOL_DOWNLOAD, "  OTA from GitHub");
    lv_obj_set_style_bg_color(btn_gh_ota, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_gh_ota, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_gh_ota, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_gh_ota, [](lv_event_t* e) {
        lv_obj_t* scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));

        if (sigurdos_is_under_launcher()) {
            auto dlg_sz = dialog_size(260, 80);
            lv_obj_t* dlg = lv_obj_create(scr);
            lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
            lv_obj_center(dlg);
            lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
            lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_border_width(dlg, 2, 0);
            lv_obj_set_style_radius(dlg, 0, 0);
            lv_obj_set_style_pad_all(dlg, 8, 0);

            lv_obj_t* msg = lv_label_create(dlg);
            lv_label_set_text(msg, "Update SigurdOS\nthrough Launcher instead");
            lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

            lv_obj_t* close_btn = lv_btn_create(dlg);
            lv_obj_set_size(close_btn, 80, 24);
            lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_obj_set_style_bg_color(close_btn, lv_color_hex(BG_INPUT), 0);
            lv_obj_set_style_radius(close_btn, 0, 0);
            lv_obj_t* cl = lv_label_create(close_btn);
            lv_label_set_text(cl, "OK");
            lv_obj_center(cl);
            lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
                lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
            }, LV_EVENT_CLICKED, nullptr);
            return;
        }

        auto dlg_sz = dialog_size(280, 160);
        lv_obj_t* dlg = lv_obj_create(scr);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, sigurdos::github_ota::getDownloadLabel());
        lv_obj_set_style_text_color(title, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* status_lbl = lv_label_create(dlg);
        lv_label_set_text(status_lbl, "Starting...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(status_lbl, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, -10);

        lv_obj_t* bar = lv_bar_create(dlg);
        lv_obj_set_size(bar, 240, 14);
        lv_obj_align(bar, LV_ALIGN_CENTER, 0, 20);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(ACCENT), LV_PART_INDICATOR);

        // Start the update
        if (!sigurdos::github_ota::startGitHubUpdate()) {
            lv_label_set_text(status_lbl, sigurdos::github_ota::getStatus().error_msg);
            lv_obj_set_style_text_color(status_lbl, lv_color_hex(ACCENT_RED), 0);
        }

        // Polling timer — finds label/bar from dialog children each tick
        (void)lv_timer_create([](lv_timer_t* t) {
            lv_obj_t* dlg = (lv_obj_t*)lv_timer_get_user_data(t);
            if (!dlg) { lv_timer_del(t); return; }

            const auto& st = sigurdos::github_ota::getStatus();
            // Find status label (second label) and bar
            lv_obj_t* lbl = nullptr;
            lv_obj_t* prog_bar = nullptr;
            int label_count = 0;
            uint32_t cnt = lv_obj_get_child_cnt(dlg);
            for (uint32_t i = 0; i < cnt; i++) {
                lv_obj_t* c = lv_obj_get_child(dlg, i);
                if (lv_obj_check_type(c, &lv_label_class)) {
                    label_count++;
                    if (label_count == 2) lbl = c;  // second label = status
                }
                if (lv_obj_check_type(c, &lv_bar_class)) {
                    prog_bar = c;
                }
            }
            if (lbl) {
                lv_label_set_text(lbl, st.status_msg);
                if (st.state == sigurdos::github_ota::GitHubOTAState::Failed) {
                    lv_obj_set_style_text_color(lbl, lv_color_hex(ACCENT_RED), 0);
                }
            }
            if (prog_bar) {
                lv_bar_set_value(prog_bar, st.progress_pct, LV_ANIM_ON);
            }
            if (st.state == sigurdos::github_ota::GitHubOTAState::Success ||
                st.state == sigurdos::github_ota::GitHubOTAState::Failed) {
                lv_timer_del(t);
            }
        }, 500, dlg);

        // Close button
        lv_obj_t* close_btn = lv_btn_create(dlg);
        lv_obj_set_size(close_btn, 80, 24);
        lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_radius(close_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(close_btn);
        lv_label_set_text(cl, "Close");
        lv_obj_center(cl);
        lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
            lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev));
            sigurdos::github_ota::cancel();
            lv_obj_del_async(dlg);
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Shut down
    lv_obj_t* btn_shutdown = lv_list_add_btn(list, LV_SYMBOL_POWER, "  Shut down");
    lv_obj_set_style_bg_color(btn_shutdown, lv_color_hex(0x4a2020), 0);
    lv_obj_set_style_bg_color(btn_shutdown, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_shutdown, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_shutdown, [](lv_event_t* e) {
        lv_obj_t* scr_sh = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        auto dlg_sz = dialog_size(240, 100);
        lv_obj_t* dlg = lv_obj_create(scr_sh);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_border_width(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, "Shut down?");
        lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* msg = lv_label_create(dlg);
        lv_label_set_text(msg, "Save state and power off?");
        lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, -4);

        lv_obj_t* cancel_btn = lv_btn_create(dlg);
        lv_obj_set_size(cancel_btn, 64, 24);
        lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -4);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_radius(cancel_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(cancel_btn);
        lv_label_set_text(cl, "Cancel");
        lv_obj_center(cl);
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ev) {
            lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* confirm_btn = lv_btn_create(dlg);
        lv_obj_set_size(confirm_btn, 64, 24);
        lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
        lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(confirm_btn, 0, 0);
        lv_obj_t* cfl = lv_label_create(confirm_btn);
        lv_label_set_text(cfl, "Shut down");
        lv_obj_center(cfl);
        lv_obj_add_event_cb(confirm_btn, [](lv_event_t*) {
            sigurdos::mesh::shutdown();
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    row++;

    // Reboot
    lv_obj_t* btn_reboot = lv_list_add_btn(list, LV_SYMBOL_POWER, "  Reboot");
    lv_obj_set_style_bg_color(btn_reboot, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(btn_reboot, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_reboot, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_reboot, [](lv_event_t*) {
        // Small delay for flash writes to complete, then restart
        sigurdos::mesh::saveState();
        sigurdos::mesh::saveChannels();
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }, LV_EVENT_CLICKED, nullptr);

    // Factory reset
    lv_obj_t* btn_reset = lv_list_add_btn(list, LV_SYMBOL_WARNING, "  Factory reset");
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x4a2020), 0);
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_reset, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_reset, [](lv_event_t* e) {
        lv_obj_t* scr_fr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        auto dlg_sz = dialog_size(250, 120);
        lv_obj_t* dlg = lv_obj_create(scr_fr);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(DIVIDER), 0);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);
        lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, "Factory reset?");
        lv_obj_set_style_text_color(title, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* msg = lv_label_create(dlg);
        lv_label_set_text(msg, "Erase all data and reboot?\nAll settings, contacts and\nidentity will be lost.");
        lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 22);

        // Cancel
        lv_obj_t* cancel_btn = lv_btn_create(dlg);
        lv_obj_set_size(cancel_btn, 100, 28);
        lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_radius(cancel_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(cancel_btn);
        lv_label_set_text(cl, "Cancel");
        lv_obj_center(cl);
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ev) {
            // Defer deletion: deleting the dialog (this button's parent) from
            // inside its own click handler is a use-after-free — LVGL may still
            // dereference the freed object after the callback returns. Matches
            // the async-delete pattern used by every other dialog close here.
            lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
        }, LV_EVENT_CLICKED, nullptr);

        // Confirm (red, dangerous)
        lv_obj_t* confirm_btn = lv_btn_create(dlg);
        lv_obj_set_size(confirm_btn, 100, 28);
        lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
        lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(confirm_btn, 0, 0);
        lv_obj_t* cfl = lv_label_create(confirm_btn);
        lv_label_set_text(cfl, "Reset");
        lv_obj_center(cfl);
        lv_obj_add_event_cb(confirm_btn, [](lv_event_t*) {
            sigurdos::mesh::factoryReset();
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;
    snprintf(buf, sizeof(buf), "  SigurdOS " SIGURDOS_VERSION);
    lv_obj_t* rv = lv_list_add_btn(list, LV_SYMBOL_HOME, buf);
    lv_obj_set_style_bg_color(rv, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(rv, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(rv, lv_color_hex(TEXT_PRIMARY), 0);
    row++;

    // Null row pointers on delete
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        g_date_row = nullptr;
        g_time_row = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    show_screen(scr);
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
