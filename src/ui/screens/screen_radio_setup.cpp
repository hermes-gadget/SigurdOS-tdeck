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
#include "../chat_screen.h"
#include "../theme.h"
#include "../responsive.h"
#include "../../hal/prefs.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

void custom_rf_screen_show()
{
    using namespace sigurdos::theme;
    using responsive::CONTENT_Y;
    using responsive::CONTENT_W;
    using responsive::CONTENT_H;

    lv_obj_t* scr = make_screen_full("Custom RF");

    int y = CONTENT_Y + 4;
    int row_h = 28;
    int lbl_w = 50;
    int inp_w = CONTENT_W - lbl_w - 12;

    // Pre-fill from shared state
    char freq_buf[16], sf_buf[8], bw_buf[12], cr_buf[8], pwr_buf[8];
    snprintf(freq_buf, sizeof(freq_buf), "%.3f", s_rf_freq);
    snprintf(sf_buf,   sizeof(sf_buf),   "%d",   s_rf_sf);
    snprintf(bw_buf,   sizeof(bw_buf),   "%.1f",  s_rf_bw);
    snprintf(cr_buf,   sizeof(cr_buf),   "%d",   s_rf_cr);
    snprintf(pwr_buf,  sizeof(pwr_buf),  "%d",   s_rf_pwr);

    struct { const char* label; const char* val; lv_obj_t* ta; } fields[] = {
        {"Freq", freq_buf, nullptr},
        {"SF",   sf_buf,   nullptr},
        {"BW",   bw_buf,   nullptr},
        {"CR",   cr_buf,   nullptr},
        {"Pwr",  pwr_buf,  nullptr},
    };

    lv_group_t* grp = lv_group_get_default();
    for (int i = 0; i < 5; i++) {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, fields[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, y + 3);

        lv_obj_t* ta = lv_textarea_create(scr);
        lv_obj_set_size(ta, inp_w, 20);
        lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 8 + lbl_w, y);
        lv_obj_set_style_bg_color(ta, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_text_color(ta, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(ta, emoji_wrapped_montserrat_10, 0);
        lv_obj_set_style_border_width(ta, 0, 0);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_text(ta, fields[i].val);
        apply_focus_style(ta);
        fields[i].ta = ta;
        if (grp) lv_group_add_obj(grp, ta);
        y += row_h;
    }
    if (grp && fields[0].ta) lv_group_focus_obj(fields[0].ta);

    // Error label
    lv_obj_t* err_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(err_lbl, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(err_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_width(err_lbl, CONTENT_W);
    lv_obj_align(err_lbl, LV_ALIGN_TOP_LEFT, 8, y);
    lv_label_set_text(err_lbl, "");

    y += 22;

    // Apply button
    auto* btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 160, 28);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(btn, 0, 0);
    auto* bl = lv_label_create(btn);
    lv_label_set_text(bl, "Apply");
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, [](lv_event_t*) {
        // Find textareas by scanning children of root scr
        lv_obj_t* scr = lv_scr_act();
        // Walk children looking for textareas (5 of them)
        lv_obj_t* ta_freq = nullptr;
        lv_obj_t* ta_sf   = nullptr;
        lv_obj_t* ta_bw   = nullptr;
        lv_obj_t* ta_cr   = nullptr;
        lv_obj_t* ta_pwr  = nullptr;
        int found = 0;
        uint32_t cnt = lv_obj_get_child_cnt(scr);
        for (uint32_t i = 0; i < cnt && found < 5; i++) {
            lv_obj_t* child = lv_obj_get_child(scr, i);
            if (lv_obj_check_type(child, &lv_textarea_class)) {
                switch (found) {
                    case 0: ta_freq = child; break;
                    case 1: ta_sf   = child; break;
                    case 2: ta_bw   = child; break;
                    case 3: ta_cr   = child; break;
                    case 4: ta_pwr  = child; break;
                }
                found++;
            }
        }

        // Guard: all 5 textareas must have been found — layout change or
        // allocation failure leaves dangling pointers that would crash below.
        if (found != 5) {
            lv_obj_t* el = lv_obj_get_child(scr, lv_obj_get_child_cnt(scr) - 2);
            if (lv_obj_check_type(el, &lv_label_class)) {
                lv_label_set_text(el, "Error: textarea not found");
            }
            return;
        }

        float freq = atof(lv_textarea_get_text(ta_freq));
        int   sf   = atoi(lv_textarea_get_text(ta_sf));
        float bw   = atof(lv_textarea_get_text(ta_bw));
        int   cr   = atoi(lv_textarea_get_text(ta_cr));
        int   pwr  = atoi(lv_textarea_get_text(ta_pwr));

        const char* err = nullptr;
        if (freq < 400.0f || freq > 930.0f)              err = "Freq: 400.0 - 930.0 MHz";
        else if (sf < 7 || sf > 12)                      err = "SF: 7 - 12";
        else if (bw < 7.8f || bw > 500.0f)               err = "BW: 7.8 - 500 kHz";
        else if (cr < 5 || cr > 8)                       err = "CR: 4/5 - 4/8";
        else if (pwr < 2 || pwr > 22)                    err = "TX Pwr: 2 - 22 dBm";

        // Find err_lbl (last label before the button, positioned dynamically)
        // Simple approach: create a fresh label each time
        if (err) {
            lv_obj_t* el = lv_obj_get_child(scr, lv_obj_get_child_cnt(scr) - 2);
            if (lv_obj_check_type(el, &lv_label_class)) {
                lv_label_set_text(el, err);
            }
            return;
        }

        s_rf_freq = freq;
        s_rf_sf   = sf;
        s_rf_bw   = bw;
        s_rf_cr   = cr;
        s_rf_pwr  = pwr;

        // Persist to NVS immediately so settings survive reboot
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.freq         = freq;
        np.bw           = bw;
        np.sf           = (uint8_t)sf;
        np.cr           = (uint8_t)cr;
        np.tx_power_dbm = (int8_t)pwr;
        np.configured   = true;
        sigurdos::prefs_set(np);

        // Reload Radio Setup screen with updated values
        radio_setup_screen_show();
    }, LV_EVENT_CLICKED, nullptr);

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Radio Setup — configure frequency, SF, BW, power
// ════════════════════════════════════════════════════════
void radio_setup_screen_show()
{
    lv_obj_t* scr = make_screen_full("Radio Setup");

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();

    s_rf_freq = p.configured ? p.freq          : 869.618f;
    s_rf_sf   = p.configured ? p.sf            : 8;
    s_rf_bw   = p.configured ? p.bw            : 62.5f;
    s_rf_cr   = p.configured ? p.cr            : 5;
    s_rf_pwr  = p.configured ? p.tx_power_dbm  : 22;
    s_rx_gain    = p.rx_boosted_gain;
    s_duty_cycle = p.duty_cycle;
    s_multi_ack  = p.multi_acks;
    s_buzzer_quiet = p.buzzer_quiet;
    s_path_hash_mode = p.path_hash_mode > 2 ? 0 : p.path_hash_mode;

    // Warning
    auto* warn = lv_label_create(scr);
    lv_label_set_text(warn,
        "Check local regulations. Incorrect settings may be illegal.");
    lv_obj_set_width(warn, CONTENT_W);
    lv_obj_set_style_pad_left(warn, 8, 0);
    lv_obj_set_style_pad_right(warn, 8, 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(warn, lv_color_hex(0xccaa00), 0);
    lv_obj_set_style_text_font(warn, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(warn, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 2);

    // ── Left column: frequency presets ────────────────────
    int lx = 4;
    int lw = 148;
    static const struct { const char* label; float freq; } freqs[] = {
        {"868.000 (EU)",   868.000f},
        {"869.525 (UK)",   869.525f},
        {"869.618 (UK)",   869.618f},
        {"915.000 (US)",   915.000f},
        {"433.500 (EU)",   433.500f},
    };
    static constexpr int NUM_FREQS = 5;
    static lv_obj_t* freq_btns[NUM_FREQS] = {};

    int ly = CONTENT_Y + 26;
    int btn_h = 18;
    for (int i = 0; i < NUM_FREQS; i++) {
        auto* btn = lv_btn_create(scr);
        lv_obj_set_size(btn, lw, btn_h);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, lx, ly);
        lv_obj_set_style_bg_color(btn, lv_color_hex(
            fabsf(s_rf_freq - freqs[i].freq) < 0.001f ? 0x2a5a2a : BG_TERTIARY), 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        auto* tl = lv_label_create(btn);
        lv_label_set_text(tl, freqs[i].label);
        lv_obj_set_style_text_font(tl, emoji_wrapped_montserrat_10, 0);
        lv_obj_center(tl);
        freq_btns[i] = btn;

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            s_rf_freq = freqs[idx].freq;
            for (int j = 0; j < NUM_FREQS; j++) {
                lv_obj_set_style_bg_color(freq_btns[j],
                    lv_color_hex(j == idx ? 0x2a5a2a : BG_TERTIARY), 0);
            }
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        ly += 22;
    }

    // ── Right column: controls ────────────────────────────
    int rx = lx + lw + 6;
    int rw = DISPLAY_W - rx - 4;
    int ry = CONTENT_Y + 26;
    char buf[64];

    // Custom RF button
    auto* custom_btn = lv_btn_create(scr);
    lv_obj_set_size(custom_btn, rw, 22);
    lv_obj_align(custom_btn, LV_ALIGN_TOP_LEFT, rx, ry);
    lv_obj_set_style_bg_color(custom_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(custom_btn, 0, 0);
    lv_obj_set_style_border_width(custom_btn, 0, 0);
    auto* ctl = lv_label_create(custom_btn);
    lv_label_set_text(ctl, "Custom RF...");
    lv_obj_set_style_text_font(ctl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(ctl);
    lv_obj_add_event_cb(custom_btn, [](lv_event_t*) {
        custom_rf_screen_show();
    }, LV_EVENT_CLICKED, nullptr);
    ry += 28;

    // SF row
    snprintf(buf, sizeof(buf), "SF:%d", s_rf_sf);
    auto* sf_lbl = lv_label_create(scr);
    lv_label_set_text(sf_lbl, buf);
    lv_obj_set_style_text_color(sf_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(sf_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(sf_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* sf_minus = lv_btn_create(scr);
    lv_obj_set_size(sf_minus, 24, 20);
    lv_obj_align(sf_minus, LV_ALIGN_TOP_LEFT, rx + rw - 54, ry - 2);
    lv_obj_set_style_bg_color(sf_minus, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(sf_minus, 0, 0);
    auto* sml = lv_label_create(sf_minus); lv_label_set_text(sml, "-"); lv_obj_center(sml);
    lv_obj_add_event_cb(sf_minus, [](lv_event_t* e) {
        if (s_rf_sf > 6) { s_rf_sf--; char b[16]; snprintf(b, sizeof(b), "SF:%d", s_rf_sf); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b); }
    }, LV_EVENT_CLICKED, (void*)sf_lbl);

    auto* sf_plus = lv_btn_create(scr);
    lv_obj_set_size(sf_plus, 24, 20);
    lv_obj_align(sf_plus, LV_ALIGN_TOP_LEFT, rx + rw - 26, ry - 2);
    lv_obj_set_style_bg_color(sf_plus, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(sf_plus, 0, 0);
    auto* spl = lv_label_create(sf_plus); lv_label_set_text(spl, "+"); lv_obj_center(spl);
    lv_obj_add_event_cb(sf_plus, [](lv_event_t* e) {
        if (s_rf_sf < 12) { s_rf_sf++; char b[16]; snprintf(b, sizeof(b), "SF:%d", s_rf_sf); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b); }
    }, LV_EVENT_CLICKED, (void*)sf_lbl);
    ry += 24;

    // BW row
    snprintf(buf, sizeof(buf), "BW:%.1f", s_rf_bw);
    auto* bw_lbl = lv_label_create(scr);
    lv_label_set_text(bw_lbl, buf);
    lv_obj_set_style_text_color(bw_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(bw_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(bw_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* bw_minus = lv_btn_create(scr);
    lv_obj_set_size(bw_minus, 24, 20);
    lv_obj_align(bw_minus, LV_ALIGN_TOP_LEFT, rx + rw - 54, ry - 2);
    lv_obj_set_style_bg_color(bw_minus, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(bw_minus, 0, 0);
    auto* bml = lv_label_create(bw_minus); lv_label_set_text(bml, "-"); lv_obj_center(bml);
    lv_obj_add_event_cb(bw_minus, [](lv_event_t* e) {
        float v[] = {500.0f,250.0f,125.0f,62.5f,41.7f,31.25f,20.8f,15.6f,10.4f,7.8f};
        for (auto& x : v) if (s_rf_bw > x + 0.01f) { s_rf_bw = x; break; }
        char b[24]; snprintf(b, sizeof(b), "BW:%.1f", s_rf_bw); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b);
    }, LV_EVENT_CLICKED, (void*)bw_lbl);

    auto* bw_plus = lv_btn_create(scr);
    lv_obj_set_size(bw_plus, 24, 20);
    lv_obj_align(bw_plus, LV_ALIGN_TOP_LEFT, rx + rw - 26, ry - 2);
    lv_obj_set_style_bg_color(bw_plus, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(bw_plus, 0, 0);
    auto* bpl = lv_label_create(bw_plus); lv_label_set_text(bpl, "+"); lv_obj_center(bpl);
    lv_obj_add_event_cb(bw_plus, [](lv_event_t* e) {
        float v[] = {7.8f,10.4f,15.6f,20.8f,31.25f,41.7f,62.5f,125.0f,250.0f,500.0f};
        for (auto& x : v) if (s_rf_bw < x - 0.01f) { s_rf_bw = x; break; }
        char b[24]; snprintf(b, sizeof(b), "BW:%.1f", s_rf_bw); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b);
    }, LV_EVENT_CLICKED, (void*)bw_lbl);
    ry += 24;

    // TX power row
    snprintf(buf, sizeof(buf), "TX:%d", s_rf_pwr);
    auto* pwr_lbl = lv_label_create(scr);
    lv_label_set_text(pwr_lbl, buf);
    lv_obj_set_style_text_color(pwr_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(pwr_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(pwr_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* pwr_minus = lv_btn_create(scr);
    lv_obj_set_size(pwr_minus, 24, 20);
    lv_obj_align(pwr_minus, LV_ALIGN_TOP_LEFT, rx + rw - 54, ry - 2);
    lv_obj_set_style_bg_color(pwr_minus, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(pwr_minus, 0, 0);
    auto* pml = lv_label_create(pwr_minus); lv_label_set_text(pml, "-"); lv_obj_center(pml);
    lv_obj_add_event_cb(pwr_minus, [](lv_event_t* e) {
        if (s_rf_pwr > 2) { s_rf_pwr--; char b[24]; snprintf(b, sizeof(b), "TX:%d", s_rf_pwr); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b); }
    }, LV_EVENT_CLICKED, (void*)pwr_lbl);

    auto* pwr_plus = lv_btn_create(scr);
    lv_obj_set_size(pwr_plus, 24, 20);
    lv_obj_align(pwr_plus, LV_ALIGN_TOP_LEFT, rx + rw - 26, ry - 2);
    lv_obj_set_style_bg_color(pwr_plus, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(pwr_plus, 0, 0);
    auto* ppl = lv_label_create(pwr_plus); lv_label_set_text(ppl, "+"); lv_obj_center(ppl);
    lv_obj_add_event_cb(pwr_plus, [](lv_event_t* e) {
        if (s_rf_pwr < 22) { s_rf_pwr++; char b[24]; snprintf(b, sizeof(b), "TX:%d", s_rf_pwr); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b); }
    }, LV_EVENT_CLICKED, (void*)pwr_lbl);
    ry += 28;

    // ── RX boosted gain toggle ──────────────────────
    snprintf(buf, sizeof(buf), "RX Gain: %s", s_rx_gain ? "BOOST" : "NORMAL");
    auto* gain_lbl = lv_label_create(scr);
    lv_label_set_text(gain_lbl, buf);
    lv_obj_set_style_text_color(gain_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(gain_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(gain_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* gain_toggle = lv_btn_create(scr);
    lv_obj_set_size(gain_toggle, 48, 20);
    lv_obj_align(gain_toggle, LV_ALIGN_TOP_LEFT, rx + rw - 52, ry - 2);
    lv_obj_set_style_bg_color(gain_toggle, lv_color_hex(s_rx_gain ? ACCENT : ACCENT_RED), 0);
    lv_obj_set_style_radius(gain_toggle, 0, 0);
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
    ry += 24;

    // ── Multi-ACK toggle ────────────────────────────
    snprintf(buf, sizeof(buf), "Multi-ACK: %s", s_multi_ack ? "ON" : "OFF");
    auto* mack_lbl = lv_label_create(scr);
    lv_label_set_text(mack_lbl, buf);
    lv_obj_set_style_text_color(mack_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(mack_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(mack_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* mack_toggle = lv_btn_create(scr);
    lv_obj_set_size(mack_toggle, 48, 20);
    lv_obj_align(mack_toggle, LV_ALIGN_TOP_LEFT, rx + rw - 52, ry - 2);
    lv_obj_set_style_bg_color(mack_toggle, lv_color_hex(s_multi_ack ? ACCENT : ACCENT_RED), 0);
    lv_obj_set_style_radius(mack_toggle, 0, 0);
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
    ry += 24;

    // ── Buzzer toggle ──────────────────────────────
    snprintf(buf, sizeof(buf), "Buzzer: %s", s_buzzer_quiet ? "OFF" : "ON");
    auto* buzzer_lbl = lv_label_create(scr);
    lv_label_set_text(buzzer_lbl, buf);
    lv_obj_set_style_text_color(buzzer_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(buzzer_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(buzzer_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* buzzer_toggle = lv_btn_create(scr);
    lv_obj_set_size(buzzer_toggle, 48, 20);
    lv_obj_align(buzzer_toggle, LV_ALIGN_TOP_LEFT, rx + rw - 52, ry - 2);
    lv_obj_set_style_bg_color(buzzer_toggle, lv_color_hex(s_buzzer_quiet ? ACCENT_RED : ACCENT), 0);
    lv_obj_set_style_radius(buzzer_toggle, 0, 0);
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
    ry += 24;

    // ── Path hash size (multibyte) cycle ────────────
    // Mode 0/1/2 → 1/2/3-byte path hash for originated adverts & messages.
    // Keep at 1 byte unless your mesh's repeaters all run MeshCore 1.14+.
    snprintf(buf, sizeof(buf), "Path Hash: %d byte%s",
             s_path_hash_mode + 1, s_path_hash_mode == 0 ? "" : "s");
    auto* phash_lbl = lv_label_create(scr);
    lv_label_set_text(phash_lbl, buf);
    lv_obj_set_style_text_color(phash_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(phash_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(phash_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* phash_btn = lv_btn_create(scr);
    lv_obj_set_size(phash_btn, 48, 20);
    lv_obj_align(phash_btn, LV_ALIGN_TOP_LEFT, rx + rw - 52, ry - 2);
    lv_obj_set_style_bg_color(phash_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(phash_btn, 0, 0);
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
    ry += 24;

    // Save & Reboot
    auto* save_btn = lv_btn_create(scr);
    lv_obj_set_size(save_btn, rw, 28);
    lv_obj_align(save_btn, LV_ALIGN_TOP_LEFT, rx, ry);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(save_btn, 0, 0);
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
        sigurdos::prefs_set(np);
        sigurdos::mesh::saveChannels();
        chat_save_messages();
        ESP.restart();
    }, LV_EVENT_CLICKED, nullptr);

    show_screen(scr);
}

} // namespace sigurdos::ui
