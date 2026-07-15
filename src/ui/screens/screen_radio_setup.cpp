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
#include <SPIFFS.h>
#include "../chat_screen.h"
#include "../theme.h"
#include "../responsive.h"
#include "../../hal/prefs.h"
#include "../../hal/radio_profiles.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ════════════════════════════════════════════════════════
// Radio Setup state — shared between main screen and Custom RF screen
static float s_rf_freq = 869.618f;
static int   s_rf_sf   = 8;
static float s_rf_bw   = 62.5f;
static int   s_rf_cr   = 5;
static int   s_rf_pwr  = 22;
static bool  s_rx_gain    = false;  // RX boosted gain toggle state
static bool  s_multi_ack  = false;  // multi-ACK toggle state
static bool  s_buzzer_quiet = false; // buzzer quiet toggle state
static uint8_t s_duty_cycle = 0;    // duty cycle percentage (0 = disabled)
static uint8_t s_path_hash_mode = 0; // path hash size mode (0=1B, 1=2B, 2=3B)
static const sigurdos::RadioProfile* s_selected_profile = nullptr;

enum RfField : int {
    RF_FREQ = 0,
    RF_SF,
    RF_BW,
    RF_CR,
    RF_PWR,
    RF_FIELD_COUNT,
};

static lv_obj_t* s_custom_rf_labels[RF_FIELD_COUNT] = {};
static lv_obj_t* s_main_profile_btns[8] = {};
static lv_obj_t* s_main_sf_label = nullptr;
static lv_obj_t* s_main_bw_label = nullptr;
static lv_obj_t* s_main_cr_label = nullptr;
static lv_obj_t* s_main_pwr_label = nullptr;
static ScreenLifetime s_custom_rf_lifetime;
static ScreenLifetime s_radio_setup_lifetime;

static constexpr float BW_VALUES[] = {
    7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f,
};

static void add_to_group(lv_obj_t* obj)
{
    lv_group_t* g = lv_group_get_default();
    if (g && obj) lv_group_add_obj(g, obj);
}

static bool current_matches_profile(const sigurdos::RadioProfile* profile)
{
    return profile &&
           fabsf(s_rf_freq - profile->freq_mhz) < 0.001f &&
           fabsf(s_rf_bw - profile->bw_khz) < 0.01f &&
           s_rf_sf == profile->sf &&
           s_rf_cr == profile->cr &&
           s_rf_pwr == profile->tx_power_dbm;
}

static void refresh_custom_rf_labels()
{
    char buf[32];
    if (s_custom_rf_labels[RF_FREQ]) {
        snprintf(buf, sizeof(buf), "%.3f", s_rf_freq);
        lv_label_set_text(s_custom_rf_labels[RF_FREQ], buf);
    }
    if (s_custom_rf_labels[RF_SF]) {
        snprintf(buf, sizeof(buf), "%d", s_rf_sf);
        lv_label_set_text(s_custom_rf_labels[RF_SF], buf);
    }
    if (s_custom_rf_labels[RF_BW]) {
        snprintf(buf, sizeof(buf), "%.1f", s_rf_bw);
        lv_label_set_text(s_custom_rf_labels[RF_BW], buf);
    }
    if (s_custom_rf_labels[RF_CR]) {
        snprintf(buf, sizeof(buf), "%d", s_rf_cr);
        lv_label_set_text(s_custom_rf_labels[RF_CR], buf);
    }
    if (s_custom_rf_labels[RF_PWR]) {
        snprintf(buf, sizeof(buf), "%d", s_rf_pwr);
        lv_label_set_text(s_custom_rf_labels[RF_PWR], buf);
    }
}

static void refresh_main_profile_buttons()
{
    const sigurdos::RadioProfile* active = current_matches_profile(s_selected_profile)
        ? s_selected_profile
        : sigurdos::radio_profile_match(s_rf_freq, s_rf_bw, (uint8_t)s_rf_sf,
                                        (uint8_t)s_rf_cr, (int8_t)s_rf_pwr);

    const size_t count = sigurdos::radio_profile_count();
    const size_t max_buttons = sizeof(s_main_profile_btns) / sizeof(s_main_profile_btns[0]);
    for (size_t i = 0; i < count && i < max_buttons; ++i) {
        if (!s_main_profile_btns[i]) continue;
        const auto* profile = sigurdos::radio_profile_at(i);
        const bool selected = active && profile && strcmp(active->id, profile->id) == 0;
        lv_obj_set_style_bg_color(s_main_profile_btns[i],
                                  lv_color_hex(selected ? 0x2a5a2a : BG_TERTIARY), 0);
    }
}

static void refresh_main_rf_labels()
{
    char buf[32];
    if (s_main_sf_label) {
        snprintf(buf, sizeof(buf), "SF:%d", s_rf_sf);
        lv_label_set_text(s_main_sf_label, buf);
    }
    if (s_main_bw_label) {
        snprintf(buf, sizeof(buf), "BW:%.1f", s_rf_bw);
        lv_label_set_text(s_main_bw_label, buf);
    }
    if (s_main_cr_label) {
        snprintf(buf, sizeof(buf), "CR:4/%d", s_rf_cr);
        lv_label_set_text(s_main_cr_label, buf);
    }
    if (s_main_pwr_label) {
        snprintf(buf, sizeof(buf), "TX:%d", s_rf_pwr);
        lv_label_set_text(s_main_pwr_label, buf);
    }
    refresh_main_profile_buttons();
}

static int bw_index()
{
    int best = 0;
    float best_delta = fabsf(s_rf_bw - BW_VALUES[0]);
    for (int i = 1; i < (int)(sizeof(BW_VALUES) / sizeof(BW_VALUES[0])); ++i) {
        const float delta = fabsf(s_rf_bw - BW_VALUES[i]);
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static void adjust_rf_field(RfField field, int delta)
{
    switch (field) {
    case RF_FREQ:
        s_rf_freq += delta > 0 ? 0.125f : -0.125f;
        if (s_rf_freq < 400.0f) s_rf_freq = 400.0f;
        if (s_rf_freq > 930.0f) s_rf_freq = 930.0f;
        break;
    case RF_SF:
        s_rf_sf += delta;
        if (s_rf_sf < 6) s_rf_sf = 6;
        if (s_rf_sf > 12) s_rf_sf = 12;
        break;
    case RF_BW: {
        int idx = bw_index() + delta;
        if (idx < 0) idx = 0;
        const int max_idx = (int)(sizeof(BW_VALUES) / sizeof(BW_VALUES[0])) - 1;
        if (idx > max_idx) idx = max_idx;
        s_rf_bw = BW_VALUES[idx];
        break;
    }
    case RF_CR:
        s_rf_cr += delta;
        if (s_rf_cr < 5) s_rf_cr = 5;
        if (s_rf_cr > 8) s_rf_cr = 8;
        break;
    case RF_PWR:
        s_rf_pwr += delta;
        if (s_rf_pwr < 2) s_rf_pwr = 2;
        if (s_rf_pwr > 22) s_rf_pwr = 22;
        break;
    default:
        break;
    }
    s_selected_profile = nullptr;
    refresh_custom_rf_labels();
    refresh_main_rf_labels();
}

static void rf_adjust_cb(lv_event_t* e)
{
    intptr_t packed = (intptr_t)lv_event_get_user_data(e);
    RfField field = (RfField)((packed >> 8) & 0xFF);
    int delta = (packed & 0xFF) == 1 ? 1 : -1;
    adjust_rf_field(field, delta);
}

static lv_obj_t* make_rf_button(lv_obj_t* parent, int w, int h,
                                const char* text, uint32_t color)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    apply_focus_style(btn);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(lbl);
    add_to_group(btn);
    return btn;
}

static void add_custom_rf_row(lv_obj_t* parent, const char* name, RfField field, int y)
{
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, y + 5);

    lv_obj_t* minus = make_rf_button(parent, 34, 22, "-", ACCENT_RED);
    lv_obj_align(minus, LV_ALIGN_TOP_LEFT, 76, y);
    lv_obj_add_event_cb(minus, rf_adjust_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)(((int)field << 8) | 0));

    lv_obj_t* value = make_rf_button(parent, 98, 22, "", BG_INPUT);
    lv_obj_align(value, LV_ALIGN_TOP_LEFT, 116, y);
    s_custom_rf_labels[field] = lv_obj_get_child(value, 0);
    lv_obj_add_event_cb(value, rf_adjust_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)(((int)field << 8) | 1));

    lv_obj_t* plus = make_rf_button(parent, 34, 22, "+", ACCENT);
    lv_obj_align(plus, LV_ALIGN_TOP_LEFT, 222, y);
    lv_obj_add_event_cb(plus, rf_adjust_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)(((int)field << 8) | 1));
}

void custom_rf_screen_show()
{
    using namespace sigurdos::theme;
    using responsive::CONTENT_Y;
    using responsive::CONTENT_W;
    using responsive::CONTENT_H;

    lv_obj_t* scr = make_screen_full("Custom RF");
    for (auto& label : s_custom_rf_labels) label = nullptr;
    s_custom_rf_lifetime.bind(scr);
    for (auto& label : s_custom_rf_labels) s_custom_rf_lifetime.track(&label);

    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_size(cont, CONTENT_W, CONTENT_H);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    int y = 4;
    add_custom_rf_row(cont, "Freq", RF_FREQ, y); y += 28;
    add_custom_rf_row(cont, "SF", RF_SF, y); y += 28;
    add_custom_rf_row(cont, "BW", RF_BW, y); y += 28;
    add_custom_rf_row(cont, "CR", RF_CR, y); y += 28;
    add_custom_rf_row(cont, "TX", RF_PWR, y); y += 32;
    refresh_custom_rf_labels();

    lv_obj_t* note = lv_label_create(cont);
    lv_label_set_text(note, "Custom values save as a manual RF profile.");
    lv_obj_set_width(note, CONTENT_W - 16);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(note, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(note, LV_ALIGN_TOP_LEFT, 8, y);
    y += 24;

    // Apply button
    auto* btn = lv_btn_create(cont);
    lv_obj_set_size(btn, 160, 28);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(btn, 0, 0);
    apply_focus_style(btn);
    auto* bl = lv_label_create(btn);
    lv_label_set_text(bl, "Apply");
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, [](lv_event_t*) {
        // Persist to NVS only if values actually changed
        const sigurdos::NodePrefs& cur = sigurdos::prefs_get();
        bool changed = (cur.freq != s_rf_freq || cur.bw != s_rf_bw ||
                        (uint8_t)cur.sf != (uint8_t)s_rf_sf ||
                        (uint8_t)cur.cr != (uint8_t)s_rf_cr ||
                        (int8_t)cur.tx_power_dbm != (int8_t)s_rf_pwr ||
                        strcmp(cur.radio_profile, "custom") != 0);
        if (changed) {
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            np.freq         = s_rf_freq;
            np.bw           = s_rf_bw;
            np.sf           = (uint8_t)s_rf_sf;
            np.cr           = (uint8_t)s_rf_cr;
            np.tx_power_dbm = (int8_t)s_rf_pwr;
            np.configured   = true;
            sigurdos::radio_profile_set_custom(np);
            sigurdos::prefs_set(np);
        }
        s_selected_profile = nullptr;

        sigurdos::mesh::applyRadioParams(
            s_rf_freq, s_rf_bw, s_rf_sf, s_rf_cr, s_rf_pwr,
            sigurdos::prefs_get().rx_boosted_gain);

        // Return to the routed Radio Setup parent with updated values.
        go_back();
    }, LV_EVENT_CLICKED, nullptr);
    add_to_group(btn);

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Radio Setup — configure frequency, SF, BW, power
// ════════════════════════════════════════════════════════
void radio_setup_screen_show()
{
    lv_obj_t* scr = make_screen_full("Radio Setup");
    for (auto& btn : s_main_profile_btns) btn = nullptr;
    s_main_sf_label = nullptr;
    s_main_bw_label = nullptr;
    s_main_cr_label = nullptr;
    s_main_pwr_label = nullptr;
    s_radio_setup_lifetime.bind(scr);
    for (auto& btn : s_main_profile_btns) s_radio_setup_lifetime.track(&btn);
    s_radio_setup_lifetime.track(&s_main_sf_label);
    s_radio_setup_lifetime.track(&s_main_bw_label);
    s_radio_setup_lifetime.track(&s_main_cr_label);
    s_radio_setup_lifetime.track(&s_main_pwr_label);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();

    s_selected_profile = p.configured ? sigurdos::radio_profile_match(p)
                                      : sigurdos::radio_profile_default();
    if (p.configured) {
        s_rf_freq = p.freq;
        s_rf_sf   = p.sf;
        s_rf_bw   = p.bw;
        s_rf_cr   = p.cr;
        s_rf_pwr  = p.tx_power_dbm;
    } else if (s_selected_profile) {
        s_rf_freq = s_selected_profile->freq_mhz;
        s_rf_sf   = s_selected_profile->sf;
        s_rf_bw   = s_selected_profile->bw_khz;
        s_rf_cr   = s_selected_profile->cr;
        s_rf_pwr  = s_selected_profile->tx_power_dbm;
    } else {
        s_rf_freq = 915.000f;
        s_rf_sf   = 8;
        s_rf_bw   = 62.5f;
        s_rf_cr   = 5;
        s_rf_pwr  = 22;
    }
    s_rx_gain    = p.rx_boosted_gain;
    s_duty_cycle = p.duty_cycle;
    s_multi_ack  = p.multi_acks;
    s_buzzer_quiet = p.buzzer_quiet;
    s_path_hash_mode = p.path_hash_mode > 2 ? 0 : p.path_hash_mode;

    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_size(cont, CONTENT_W, CONTENT_H);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    // Warning
    auto* warn = lv_label_create(cont);
    lv_label_set_text(warn,
        "Check local regulations. Incorrect settings may be illegal.");
    lv_obj_set_width(warn, CONTENT_W);
    lv_obj_set_style_pad_left(warn, 8, 0);
    lv_obj_set_style_pad_right(warn, 8, 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(warn, lv_color_hex(0xccaa00), 0);
    lv_obj_set_style_text_font(warn, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(warn, LV_ALIGN_TOP_LEFT, 0, 2);

    // ── Left column: country profiles ────────────────────
    int lx = 4;
    int lw = 148;
    const size_t profile_count = sigurdos::radio_profile_count();
    const size_t max_profile_btns = sizeof(s_main_profile_btns) / sizeof(s_main_profile_btns[0]);

    int ly = 26;
    int btn_h = 18;
    for (size_t i = 0; i < profile_count && i < max_profile_btns; i++) {
        const auto* profile = sigurdos::radio_profile_at(i);
        if (!profile) continue;
        auto* btn = lv_btn_create(cont);
        lv_obj_set_size(btn, lw, btn_h);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, lx, ly);
        lv_obj_set_style_bg_color(btn, lv_color_hex(
            current_matches_profile(profile) ? 0x2a5a2a : BG_TERTIARY), 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        apply_focus_style(btn);
        auto* tl = lv_label_create(btn);
        lv_label_set_text(tl, profile->short_label);
        lv_obj_set_style_text_font(tl, emoji_wrapped_montserrat_10, 0);
        lv_obj_center(tl);
        s_main_profile_btns[i] = btn;
        add_to_group(btn);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            size_t idx = (size_t)(intptr_t)lv_event_get_user_data(e);
            const auto* profile = sigurdos::radio_profile_at(idx);
            if (!profile) return;
            s_selected_profile = profile;
            s_rf_freq = profile->freq_mhz;
            s_rf_bw = profile->bw_khz;
            s_rf_sf = profile->sf;
            s_rf_cr = profile->cr;
            s_rf_pwr = profile->tx_power_dbm;
            refresh_main_rf_labels();
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        ly += 22;
    }

    // ── Right column: controls ────────────────────────────
    int rx = lx + lw + 6;
    int rw = DISPLAY_W - rx - 4;
    int ry = 26;
    char buf[64];

    // Custom RF button
    auto* custom_btn = lv_btn_create(cont);
    lv_obj_set_size(custom_btn, rw, 22);
    lv_obj_align(custom_btn, LV_ALIGN_TOP_LEFT, rx, ry);
    lv_obj_set_style_bg_color(custom_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(custom_btn, 0, 0);
    lv_obj_set_style_border_width(custom_btn, 0, 0);
    apply_focus_style(custom_btn);
    auto* ctl = lv_label_create(custom_btn);
    lv_label_set_text(ctl, "Custom RF...");
    lv_obj_set_style_text_font(ctl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(ctl);
    lv_obj_add_event_cb(custom_btn, [](lv_event_t*) {
        navigate_to_custom_radio_setup();
    }, LV_EVENT_CLICKED, nullptr);
    add_to_group(custom_btn);
    ry += 28;

    // SF row
    snprintf(buf, sizeof(buf), "SF:%d", s_rf_sf);
    s_main_sf_label = lv_label_create(cont);
    lv_label_set_text(s_main_sf_label, buf);
    lv_obj_set_style_text_color(s_main_sf_label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_main_sf_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(s_main_sf_label, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* sf_minus = make_rf_button(cont, 24, 20, "-", ACCENT_RED);
    lv_obj_align(sf_minus, LV_ALIGN_TOP_LEFT, rx + rw - 54, ry - 2);
    lv_obj_add_event_cb(sf_minus, [](lv_event_t*) { adjust_rf_field(RF_SF, -1); },
                        LV_EVENT_CLICKED, nullptr);

    auto* sf_plus = make_rf_button(cont, 24, 20, "+", ACCENT);
    lv_obj_align(sf_plus, LV_ALIGN_TOP_LEFT, rx + rw - 26, ry - 2);
    lv_obj_add_event_cb(sf_plus, [](lv_event_t*) { adjust_rf_field(RF_SF, 1); },
                        LV_EVENT_CLICKED, nullptr);
    ry += 24;

    // BW row
    snprintf(buf, sizeof(buf), "BW:%.1f", s_rf_bw);
    s_main_bw_label = lv_label_create(cont);
    lv_label_set_text(s_main_bw_label, buf);
    lv_obj_set_style_text_color(s_main_bw_label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_main_bw_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(s_main_bw_label, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* bw_minus = make_rf_button(cont, 24, 20, "-", ACCENT_RED);
    lv_obj_align(bw_minus, LV_ALIGN_TOP_LEFT, rx + rw - 54, ry - 2);
    lv_obj_add_event_cb(bw_minus, [](lv_event_t*) { adjust_rf_field(RF_BW, -1); },
                        LV_EVENT_CLICKED, nullptr);

    auto* bw_plus = make_rf_button(cont, 24, 20, "+", ACCENT);
    lv_obj_align(bw_plus, LV_ALIGN_TOP_LEFT, rx + rw - 26, ry - 2);
    lv_obj_add_event_cb(bw_plus, [](lv_event_t*) { adjust_rf_field(RF_BW, 1); },
                        LV_EVENT_CLICKED, nullptr);
    ry += 24;

    // CR row
    snprintf(buf, sizeof(buf), "CR:4/%d", s_rf_cr);
    s_main_cr_label = lv_label_create(cont);
    lv_label_set_text(s_main_cr_label, buf);
    lv_obj_set_style_text_color(s_main_cr_label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_main_cr_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(s_main_cr_label, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* cr_minus = make_rf_button(cont, 24, 20, "-", ACCENT_RED);
    lv_obj_align(cr_minus, LV_ALIGN_TOP_LEFT, rx + rw - 54, ry - 2);
    lv_obj_add_event_cb(cr_minus, [](lv_event_t*) { adjust_rf_field(RF_CR, -1); },
                        LV_EVENT_CLICKED, nullptr);

    auto* cr_plus = make_rf_button(cont, 24, 20, "+", ACCENT);
    lv_obj_align(cr_plus, LV_ALIGN_TOP_LEFT, rx + rw - 26, ry - 2);
    lv_obj_add_event_cb(cr_plus, [](lv_event_t*) { adjust_rf_field(RF_CR, 1); },
                        LV_EVENT_CLICKED, nullptr);
    ry += 24;

    // TX power row
    snprintf(buf, sizeof(buf), "TX:%d", s_rf_pwr);
    s_main_pwr_label = lv_label_create(cont);
    lv_label_set_text(s_main_pwr_label, buf);
    lv_obj_set_style_text_color(s_main_pwr_label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_main_pwr_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(s_main_pwr_label, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* pwr_minus = make_rf_button(cont, 24, 20, "-", ACCENT_RED);
    lv_obj_align(pwr_minus, LV_ALIGN_TOP_LEFT, rx + rw - 54, ry - 2);
    lv_obj_add_event_cb(pwr_minus, [](lv_event_t*) { adjust_rf_field(RF_PWR, -1); },
                        LV_EVENT_CLICKED, nullptr);

    auto* pwr_plus = make_rf_button(cont, 24, 20, "+", ACCENT);
    lv_obj_align(pwr_plus, LV_ALIGN_TOP_LEFT, rx + rw - 26, ry - 2);
    lv_obj_add_event_cb(pwr_plus, [](lv_event_t*) { adjust_rf_field(RF_PWR, 1); },
                        LV_EVENT_CLICKED, nullptr);
    ry += 28;

    // ── RX boosted gain toggle ──────────────────────
    snprintf(buf, sizeof(buf), "RX Gain: %s", s_rx_gain ? "BOOST" : "NORMAL");
    auto* gain_lbl = lv_label_create(cont);
    lv_label_set_text(gain_lbl, buf);
    lv_obj_set_style_text_color(gain_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(gain_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(gain_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* gain_toggle = lv_btn_create(cont);
    lv_obj_set_size(gain_toggle, 48, 20);
    lv_obj_align(gain_toggle, LV_ALIGN_TOP_LEFT, rx + rw - 52, ry - 2);
    lv_obj_set_style_bg_color(gain_toggle, lv_color_hex(s_rx_gain ? ACCENT : ACCENT_RED), 0);
    lv_obj_set_style_radius(gain_toggle, 0, 0);
    apply_focus_style(gain_toggle);
    auto* gtl = lv_label_create(gain_toggle);
    lv_label_set_text(gtl, s_rx_gain ? "ON" : "OFF");
    lv_obj_center(gtl);
    lv_obj_add_event_cb(gain_toggle, [](lv_event_t* e) {
        s_rx_gain = !s_rx_gain;
        lv_obj_t* lbl = (lv_obj_t*)lv_event_get_user_data(e);
        char b[32]; snprintf(b, sizeof(b), "RX Gain: %s", s_rx_gain ? "BOOST" : "NORMAL");
        lv_label_set_text(lbl, b);
        // Update toggle button appearance
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_set_style_bg_color(target, lv_color_hex(s_rx_gain ? ACCENT : ACCENT_RED), 0);
        lv_obj_t* bl = lv_obj_get_child(target, 0);
        if (bl && lv_obj_check_type(bl, &lv_label_class)) {
            lv_label_set_text(bl, s_rx_gain ? "ON" : "OFF");
        }
    }, LV_EVENT_CLICKED, (void*)gain_lbl);
    add_to_group(gain_toggle);
    ry += 24;

    // ── Multi-ACK toggle ────────────────────────────
    snprintf(buf, sizeof(buf), "Multi-ACK: %s", s_multi_ack ? "ON" : "OFF");
    auto* mack_lbl = lv_label_create(cont);
    lv_label_set_text(mack_lbl, buf);
    lv_obj_set_style_text_color(mack_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(mack_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(mack_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* mack_toggle = lv_btn_create(cont);
    lv_obj_set_size(mack_toggle, 48, 20);
    lv_obj_align(mack_toggle, LV_ALIGN_TOP_LEFT, rx + rw - 52, ry - 2);
    lv_obj_set_style_bg_color(mack_toggle, lv_color_hex(s_multi_ack ? ACCENT : ACCENT_RED), 0);
    lv_obj_set_style_radius(mack_toggle, 0, 0);
    apply_focus_style(mack_toggle);
    auto* mtl = lv_label_create(mack_toggle);
    lv_label_set_text(mtl, s_multi_ack ? "ON" : "OFF");
    lv_obj_center(mtl);
    lv_obj_add_event_cb(mack_toggle, [](lv_event_t* e) {
        s_multi_ack = !s_multi_ack;
        lv_obj_t* lbl = (lv_obj_t*)lv_event_get_user_data(e);
        char b[32]; snprintf(b, sizeof(b), "Multi-ACK: %s", s_multi_ack ? "ON" : "OFF");
        lv_label_set_text(lbl, b);
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_set_style_bg_color(target, lv_color_hex(s_multi_ack ? ACCENT : ACCENT_RED), 0);
        lv_obj_t* bl = lv_obj_get_child(target, 0);
        if (bl && lv_obj_check_type(bl, &lv_label_class)) {
            lv_label_set_text(bl, s_multi_ack ? "ON" : "OFF");
        }
    }, LV_EVENT_CLICKED, (void*)mack_lbl);
    add_to_group(mack_toggle);
    ry += 24;

    // ── Buzzer toggle ──────────────────────────────
    snprintf(buf, sizeof(buf), "Buzzer: %s", s_buzzer_quiet ? "OFF" : "ON");
    auto* buzzer_lbl = lv_label_create(cont);
    lv_label_set_text(buzzer_lbl, buf);
    lv_obj_set_style_text_color(buzzer_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(buzzer_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(buzzer_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* buzzer_toggle = lv_btn_create(cont);
    lv_obj_set_size(buzzer_toggle, 48, 20);
    lv_obj_align(buzzer_toggle, LV_ALIGN_TOP_LEFT, rx + rw - 52, ry - 2);
    lv_obj_set_style_bg_color(buzzer_toggle, lv_color_hex(s_buzzer_quiet ? ACCENT_RED : ACCENT), 0);
    lv_obj_set_style_radius(buzzer_toggle, 0, 0);
    apply_focus_style(buzzer_toggle);
    auto* btl = lv_label_create(buzzer_toggle);
    lv_label_set_text(btl, s_buzzer_quiet ? "OFF" : "ON");
    lv_obj_center(btl);
    lv_obj_add_event_cb(buzzer_toggle, [](lv_event_t* e) {
        s_buzzer_quiet = !s_buzzer_quiet;
        lv_obj_t* lbl = (lv_obj_t*)lv_event_get_user_data(e);
        char b[32]; snprintf(b, sizeof(b), "Buzzer: %s", s_buzzer_quiet ? "OFF" : "ON");
        lv_label_set_text(lbl, b);
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_set_style_bg_color(target, lv_color_hex(s_buzzer_quiet ? ACCENT_RED : ACCENT), 0);
        lv_obj_t* bl = lv_obj_get_child(target, 0);
        if (bl && lv_obj_check_type(bl, &lv_label_class)) {
            lv_label_set_text(bl, s_buzzer_quiet ? "OFF" : "ON");
        }
    }, LV_EVENT_CLICKED, (void*)buzzer_lbl);
    add_to_group(buzzer_toggle);
    ry += 24;

    // ── Path hash size (multibyte) cycle ────────────
    // Mode 0/1/2 → 1/2/3-byte path hash for originated adverts & messages.
    // Keep at 1 byte unless your mesh's repeaters all run MeshCore 1.14+.
    snprintf(buf, sizeof(buf), "Path Hash: %d byte%s",
             s_path_hash_mode + 1, s_path_hash_mode == 0 ? "" : "s");
    auto* phash_lbl = lv_label_create(cont);
    lv_label_set_text(phash_lbl, buf);
    lv_obj_set_style_text_color(phash_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(phash_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(phash_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* phash_btn = lv_btn_create(cont);
    lv_obj_set_size(phash_btn, 48, 20);
    lv_obj_align(phash_btn, LV_ALIGN_TOP_LEFT, rx + rw - 52, ry - 2);
    lv_obj_set_style_bg_color(phash_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(phash_btn, 0, 0);
    apply_focus_style(phash_btn);
    auto* phl = lv_label_create(phash_btn);
    snprintf(buf, sizeof(buf), "%dB", s_path_hash_mode + 1);
    lv_label_set_text(phl, buf);
    lv_obj_center(phl);
    lv_obj_add_event_cb(phash_btn, [](lv_event_t* e) {
        s_path_hash_mode = (s_path_hash_mode + 1) % 3;  // cycle 0→1→2→0
        lv_obj_t* lbl = (lv_obj_t*)lv_event_get_user_data(e);
        char b[32];
        snprintf(b, sizeof(b), "Path Hash: %d byte%s",
                 s_path_hash_mode + 1, s_path_hash_mode == 0 ? "" : "s");
        lv_label_set_text(lbl, b);
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* bl = lv_obj_get_child(target, 0);
        if (bl && lv_obj_check_type(bl, &lv_label_class)) {
            snprintf(b, sizeof(b), "%dB", s_path_hash_mode + 1);
            lv_label_set_text(bl, b);
        }
    }, LV_EVENT_CLICKED, (void*)phash_lbl);
    add_to_group(phash_btn);
    ry += 24;

    // Save & Reboot
    auto* save_btn = lv_btn_create(cont);
    lv_obj_set_size(save_btn, rw, 28);
    lv_obj_align(save_btn, LV_ALIGN_TOP_LEFT, rx, ry);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(save_btn, 0, 0);
    apply_focus_style(save_btn);
    auto* svl = lv_label_create(save_btn);
    lv_label_set_text(svl, LV_SYMBOL_SAVE "  Save & Reboot");
    lv_obj_center(svl);
    lv_obj_add_event_cb(save_btn, [](lv_event_t*) {
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.freq         = s_rf_freq;
        np.bw           = s_rf_bw;
        np.sf           = (uint8_t)s_rf_sf;
        np.cr           = (uint8_t)s_rf_cr;
        np.tx_power_dbm = (int8_t)s_rf_pwr;
        np.rx_boosted_gain = s_rx_gain;
        np.multi_acks     = s_multi_ack;
        np.buzzer_quiet   = s_buzzer_quiet;
        np.duty_cycle   = s_duty_cycle;
        np.path_hash_mode = s_path_hash_mode;
        np.configured   = true;
        if (current_matches_profile(s_selected_profile)) {
            sigurdos::radio_profile_apply(*s_selected_profile, np);
            np.rx_boosted_gain = s_rx_gain;
            np.multi_acks     = s_multi_ack;
            np.buzzer_quiet   = s_buzzer_quiet;
            np.duty_cycle   = s_duty_cycle;
            np.path_hash_mode = s_path_hash_mode;
        } else {
            const auto* matched = sigurdos::radio_profile_match(
                s_rf_freq, s_rf_bw, (uint8_t)s_rf_sf, (uint8_t)s_rf_cr, (int8_t)s_rf_pwr);
            if (matched) {
                sigurdos::radio_profile_apply(*matched, np);
                np.rx_boosted_gain = s_rx_gain;
                np.multi_acks     = s_multi_ack;
                np.buzzer_quiet   = s_buzzer_quiet;
                np.duty_cycle   = s_duty_cycle;
                np.path_hash_mode = s_path_hash_mode;
            } else {
                sigurdos::radio_profile_set_custom(np);
            }
        }
        sigurdos::prefs_set(np);
        sigurdos::mesh::saveChannels();
        // Flush and wait for flash writes to complete before restart
        SPIFFS.end();
        delay(200);
        ESP.restart();
    }, LV_EVENT_CLICKED, nullptr);
    add_to_group(save_btn);

    show_screen(scr);
}

} // namespace sigurdos::ui
