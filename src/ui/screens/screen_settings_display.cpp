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
#include "../chat_screen.h"
#include "../../hal/prefs.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../fonts/emoji_font.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

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
        int val = (int)c->brightness - 25;
        c->brightness = (uint8_t)(val < 20 ? 20 : val);
        sigurdos_display_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
        auto* c = (DisplayBrightnessCtx*)lv_event_get_user_data(e);
        int val = (int)c->brightness + 25;
        c->brightness = (uint8_t)(val > 240 ? 240 : val);
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
            // Defer screen refresh to next LVGL tick — the current event
            // handler runs on a widget that will be deleted by lv_scr_load().
            lv_async_call([](void*) { refresh_current_screen(); }, nullptr);
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

} // namespace sigurdos::ui
