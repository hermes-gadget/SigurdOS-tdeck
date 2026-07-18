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
#include "../../hal/radio_profiles.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

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
        const auto* profile = sigurdos::radio_profile_match(p);
        if (profile) {
            snprintf(buf, sizeof(buf), "  Radio: %s / %.3f MHz / SF%d",
                     profile->short_label, p.freq, p.sf);
        } else {
            snprintf(buf, sizeof(buf), "  Radio: Custom / %.3f MHz / %.1f kHz / SF%d / %d dBm",
                     p.freq, p.bw, p.sf, p.tx_power_dbm);
        }
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
        navigate_to(Screen::RadioSetup);
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
            snprintf(buf, sizeof(buf), "  Flood scope [PUBLIC]");
        } else {
            snprintf(buf, sizeof(buf), "  Flood scope [%s]", active);
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

} // namespace sigurdos::ui
