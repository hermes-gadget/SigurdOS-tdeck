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

#include "screens_common.h"
#include "screens.h"
#include "navigation.h"
#include "theme.h"
#include "responsive.h"
#include "../hal/battery.h"
#include "../hal/wifi_ota.h"
#include "../hal/prefs.h"
#include "../mesh/mesh_wrapper.h"
#include "../fonts/emoji_font.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// Back button reference for back-swipe visual feedback
static lv_obj_t* s_back_btn = nullptr;

// WiFi status icon in bottom bar
static lv_obj_t* g_wifi_icon = nullptr;
static lv_obj_t* g_companion_icon = nullptr;

void create_companion_status_icon(lv_obj_t* top_bar, int right_offset)
{
    if (!top_bar) return;
    g_companion_icon = lv_label_create(top_bar);
    lv_label_set_text(g_companion_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(g_companion_icon, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(g_companion_icon, LV_ALIGN_RIGHT_MID, right_offset, 0);
    update_companion_status();
}

void update_companion_status()
{
    if (!g_companion_icon) return;
    if (!lv_obj_is_valid(g_companion_icon)) {
        g_companion_icon = nullptr;
        return;
    }
    const bool available = sigurdos::mesh::companionBleAvailable();
    const bool connected = sigurdos::mesh::companionBleConnected();
    if (!available) {
        lv_obj_add_flag(g_companion_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(g_companion_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(g_companion_icon,
        lv_color_hex(connected ? ACCENT_GREEN : TEXT_SECONDARY), 0);
}

// ════════════════════════════════════════════════════════
// make_screen_full — builds consistent top+bottom bars
// ════════════════════════════════════════════════════════
lv_obj_t* make_screen_full(const char* title)
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    apply_dark_bg(scr);

    // ── Top bar ──────────────────────────────────────────
    lv_obj_t* top = lv_obj_create(scr);
    lv_obj_set_size(top, LV_PCT(100), TOP_BAR_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);

    lv_obj_t* back = lv_btn_create(top);
    lv_obj_set_size(back, 24, TOP_BAR_H - 4);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
    apply_topbar_icon_btn(back);
    s_back_btn = back; // store for back-swipe highlight
    if (can_go_back()) {
        lv_obj_add_event_cb(back, [](lv_event_t*) { go_back(); }, LV_EVENT_CLICKED, nullptr);
    }

    lv_obj_t* back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon,
        lv_color_hex(can_go_back() ? ACCENT : TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(back_icon, emoji_wrapped_montserrat_12, 0);
    lv_obj_center(back_icon);

    // Screen title (centered, visible now that channels are gone)
    if (title && title[0]) {
        lv_obj_t* ttl = lv_label_create(top);
        lv_label_set_text(ttl, title);
        lv_obj_set_style_text_color(ttl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(ttl, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(ttl, LV_ALIGN_CENTER, 0, 0);
    }

    // Time (24h snapshot)
    {
        uint32_t epoch = sigurdos::mesh::getCurrentTime();
        char t[8];
        if (epoch == 0) {
            snprintf(t, sizeof(t), "--:--");
        } else {
            uint32_t sec = epoch % 86400;
            snprintf(t, sizeof(t), "%02d:%02d", (sec/3600)%24, (sec/60)%60);
        }
        lv_obj_t* tl = lv_label_create(top);
        lv_label_set_text(tl, t);
        lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(tl, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Signal dots (right of top bar, iOS-style)
    {
        int rssi = sigurdos::mesh::getLastRSSI();
        lv_obj_t* sig = create_signal_dots(top, rssi);
        lv_obj_align(sig, LV_ALIGN_RIGHT_MID, -54, 0);
    }
    create_companion_status_icon(top);

    // Top divider
    lv_obj_t* tdiv = lv_obj_create(scr);
    lv_obj_set_size(tdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(tdiv, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);
    lv_obj_set_style_bg_color(tdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(tdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tdiv, 0, 0);

    // ── Bottom bar ───────────────────────────────────────
    lv_obj_t* bot = lv_obj_create(scr);
    lv_obj_set_size(bot, LV_PCT(100), BOT_BAR_H);
    lv_obj_align(bot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(bot, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bot, 0, 0);
    lv_obj_set_style_border_width(bot, 0, 0);

    // Device name (left) — constrained to avoid overlapping WiFi/battery
    lv_obj_t* dev = lv_label_create(bot);
    lv_label_set_text(dev, sigurdos::mesh::getOwnName());
    lv_obj_set_style_text_color(dev, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dev, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_width(dev, 200);
    lv_label_set_long_mode(dev, LV_LABEL_LONG_DOT);
    lv_obj_align(dev, LV_ALIGN_LEFT_MID, 4, 0);

    // WiFi status icon (next to battery)
    {
        lv_obj_t* wifi = lv_label_create(bot);
        lv_label_set_text(wifi, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(wifi, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(wifi, LV_ALIGN_RIGHT_MID, -52, 0);
        g_wifi_icon = wifi;
        update_wifi_status();  // set initial state
    }

    // Battery % (right, snapshot)
    {
        char batt[8];
        int pct = sigurdos_battery_pct();
        snprintf(batt, sizeof(batt), "%d%%", pct);
        lv_obj_t* bl = lv_label_create(bot);
        lv_label_set_text(bl, batt);
        lv_obj_set_style_text_color(bl,
            lv_color_hex(pct > 20 ? ACCENT : ACCENT_RED), 0);
        lv_obj_set_style_text_font(bl, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(bl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Bottom divider
    lv_obj_t* bdiv = lv_obj_create(scr);
    lv_obj_set_size(bdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(bdiv, LV_ALIGN_TOP_MID, 0, DISPLAY_H - BOT_BAR_H - DIVIDER_H);
    lv_obj_set_style_bg_color(bdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(bdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bdiv, 0, 0);

    return scr;
}

// ── WiFi status icon in bottom bar ──────────────────────

void update_wifi_status() {
    if (!g_wifi_icon) return;
    if (!lv_obj_is_valid(g_wifi_icon)) {
        g_wifi_icon = nullptr;
        return;
    }
    bool connected = sigurdos::wifi_sta::isConnected();
    if (connected) {
        int rssi = sigurdos::wifi_sta::getRSSI();
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %d", LV_SYMBOL_WIFI, rssi);
        lv_label_set_text(g_wifi_icon, buf);
        lv_obj_set_style_text_color(g_wifi_icon,
            lv_color_hex(rssi > -60 ? ACCENT_GREEN : ACCENT), 0);
        lv_obj_clear_flag(g_wifi_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_wifi_icon, LV_OBJ_FLAG_HIDDEN);
    }
}

// ════════════════════════════════════════════════════════
// Back-button highlight for back-swipe visual feedback
// ════════════════════════════════════════════════════════
void highlight_back_button(bool show)
{
    if (!s_back_btn || !lv_obj_is_valid(s_back_btn)) return;

    if (show) {
        lv_obj_set_style_border_width(s_back_btn, 2, 0);
        lv_obj_set_style_border_color(s_back_btn, lv_color_hex(theme::ACCENT), 0);
    } else {
        lv_obj_set_style_border_width(s_back_btn, 1, 0);
        lv_obj_set_style_border_color(s_back_btn, lv_color_hex(theme::DIVIDER), 0);
    }
}

void screens_clear_back_btn()
{
    s_back_btn = nullptr;
}

void screens_clear_wifi_icon()
{
    g_wifi_icon = nullptr;
}

void screens_clear_companion_icon()
{
    g_companion_icon = nullptr;
}

// ════════════════════════════════════════════════════════
// Device PIN protection
// ════════════════════════════════════════════════════════
static uint32_t g_pin_last_unlock_ms = 0;
static constexpr uint32_t PIN_GRACE_MS = 300000; // 5 minutes in milliseconds
static bool g_pin_entry_active = false;  // set while PIN entry screen is displayed

bool is_pin_entry_active() { return g_pin_entry_active; }

bool pin_grace_active() {
    if (g_pin_last_unlock_ms == 0) return false;
    uint32_t now = millis();
    // Handle millis() wraparound (~49 days): if now < last_unlock,
    // the timer wrapped; grace is still valid if within the window.
    if (now < g_pin_last_unlock_ms) {
        return (UINT32_MAX - g_pin_last_unlock_ms + now) < PIN_GRACE_MS;
    }
    return (now - g_pin_last_unlock_ms) < PIN_GRACE_MS;
}

struct PinEntryCtx {
    int attempts;
    lv_obj_t* attempts_label;
    Screen target;
};

static void pin_entry_success(Screen target) {
    g_pin_entry_active = false;
    g_pin_last_unlock_ms = millis();
    if (g_pin_last_unlock_ms == 0) g_pin_last_unlock_ms = 1;
    // Direct load — bypass navigate_to's same-screen guard
    if (target == Screen::Settings) {
        settings_screen_show();
    } else if (target == Screen::Terminal) {
        terminal_screen_show();
    }
}

void pin_entry_show(Screen target_screen) {
    g_pin_entry_active = true;
    lv_obj_t* scr = lv_obj_create(nullptr);
    apply_dark_bg(scr);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Enter Device PIN");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    // Subtitle
    lv_obj_t* sub = lv_label_create(scr);
    lv_label_set_text(sub, "4-digit PIN to unlock");
    lv_obj_set_style_text_color(sub, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(sub, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 60);

    // PIN input
    lv_obj_t* ta = lv_textarea_create(scr);
    lv_obj_set_size(ta, 160, 44);
    lv_obj_center(ta);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 4);
    lv_textarea_set_accepted_chars(ta, "0123456789");
    lv_textarea_set_placeholder_text(ta, "----");
    lv_obj_set_style_text_font(ta, emoji_wrapped_montserrat_14, 0);
    apply_pixel_input(ta);

    // Attempts remaining label
    lv_obj_t* attempts_label = lv_label_create(scr);
    lv_label_set_text(attempts_label, "3 attempts remaining");
    lv_obj_set_style_text_color(attempts_label, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(attempts_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(attempts_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    // Back to Home button
    lv_obj_t* cancel_btn = lv_btn_create(scr);
    lv_obj_set_size(cancel_btn, 120, 28);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_MID, 0, -60);
    apply_pixel_btn_outline(cancel_btn);
    lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Back to Home");
    lv_obj_center(cancel_lbl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t*) {
        g_pin_entry_active = false;
        go_back();
    }, LV_EVENT_CLICKED, nullptr);

    // Allocate context
    PinEntryCtx* ctx = new PinEntryCtx{3, attempts_label, target_screen};

    // Value change callback — auto-submit on 4 digits
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
        PinEntryCtx* ctx = (PinEntryCtx*)lv_event_get_user_data(e);
        lv_obj_t* ta_obj = (lv_obj_t*)lv_event_get_target(e);

        const char* text = lv_textarea_get_text(ta_obj);
        if (strlen(text) == 4) {
            uint32_t entered = (uint32_t)atoi(text);
            if (entered == sigurdos::prefs_get().device_pin) {
                pin_entry_success(ctx->target);
                return;
            }
            // Wrong PIN
            ctx->attempts--;
            lv_textarea_set_text(ta_obj, "");
            if (ctx->attempts <= 0) {
                g_pin_entry_active = false;
                go_back();
            } else {
                char att_buf[32];
                snprintf(att_buf, sizeof(att_buf), "%d attempts remaining", ctx->attempts);
                lv_label_set_text(ctx->attempts_label, att_buf);
            }
        }
    }, LV_EVENT_VALUE_CHANGED, ctx);

    // Store ctx on screen for cleanup
    lv_obj_set_user_data(scr, ctx);
    lv_obj_add_event_cb(scr, [](lv_event_t* e) {
        PinEntryCtx* c = (PinEntryCtx*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
        delete c;
    }, LV_EVENT_DELETE, nullptr);

    // Focus the textarea — add to default group without wiping existing objects
    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, ta);
        lv_group_focus_obj(ta);
    }

    // auto_del=false via show_screen — mixing auto_del values deadlocks LVGL
    // after ~4-5 navigation cycles (see screens.cpp / PR #840).
    show_screen(scr);
}

} // namespace sigurdos::ui
