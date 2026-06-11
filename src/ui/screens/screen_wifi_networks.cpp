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
#include "../../hal/wifi_ota.h"
#include "../../hal/prefs.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstring>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ════════════════════════════════════════════════════════
// WiFi Networks — full-screen network scanner with trackball
// ════════════════════════════════════════════════════════

// Global state for the WiFi networks screen
static bool g_wifi_scan_done = false;
static sigurdos::wifi_scan::APInfo g_wifi_aps[30];
static int g_wifi_ap_count = 0;

static void wifi_do_scan(lv_timer_t* timer) {
    lv_obj_t* list = (lv_obj_t*)lv_timer_get_user_data(timer);
    
    g_wifi_ap_count = sigurdos::wifi_scan::scan(g_wifi_aps, 30);
    g_wifi_scan_done = true;
    
    lv_obj_clean(list);
    
    if (g_wifi_ap_count <= 0) {
        lv_obj_t* empty = lv_label_create(list);
        lv_label_set_text(empty, "No networks found");
        lv_obj_set_style_text_color(empty, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(empty, emoji_wrapped_montserrat_12, 0);
        lv_obj_center(empty);
    } else {
        lv_group_t* g = lv_group_get_default();
        bool first = true;
        for (int i = 0; i < g_wifi_ap_count && i < 20; i++) {
            const auto& ap = g_wifi_aps[i];
            char row_buf[56];
            const char* lock = ap.encrypted ? "* " : "  ";
            snprintf(row_buf, sizeof(row_buf), "%s%s   %d dBm  %s",
                     lock, ap.ssid, ap.rssi, LV_SYMBOL_RIGHT);
            
            lv_obj_t* btn = lv_btn_create(list);
            lv_obj_set_size(btn, CONTENT_W - 8, 28);
            lv_obj_set_style_bg_color(btn, lv_color_hex(i % 2 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(btn, 0, 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_pad_left(btn, 6, 0);
            
            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, row_buf);
            lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_12, 0);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 2, 0);
            lv_obj_set_width(lbl, CONTENT_W - 30);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
            
            // Trackball support
            lv_group_add_obj(g, btn);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
            
            char* ssid_copy = strdup(ap.ssid);
            lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
                const char* sel = (const char*)lv_event_get_user_data(ev);
                // Show password dialog
                lv_obj_t* scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(ev));
                auto dlg_sz = dialog_size(260, 140);
                lv_obj_t* dlg = lv_obj_create(scr);
                lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
                lv_obj_center(dlg);
                lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
                lv_obj_set_style_radius(dlg, 0, 0);
                lv_obj_set_style_border_width(dlg, 2, 0);
                lv_obj_set_style_border_color(dlg, lv_color_hex(DIVIDER), 0);
                lv_obj_set_style_pad_all(dlg, 8, 0);
                
                lv_obj_t* title = lv_label_create(dlg);
                lv_label_set_text(title, "Enter Password");
                lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
                lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
                lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);
                
                char ssid_label[48];
                snprintf(ssid_label, sizeof(ssid_label), "Network: %s", sel);
                lv_obj_t* net_lbl = lv_label_create(dlg);
                lv_label_set_text(net_lbl, ssid_label);
                lv_obj_set_style_text_color(net_lbl, lv_color_hex(TEXT_SECONDARY), 0);
                lv_obj_set_style_text_font(net_lbl, emoji_wrapped_montserrat_10, 0);
                lv_obj_align(net_lbl, LV_ALIGN_TOP_LEFT, 8, 28);
                
                lv_obj_t* pw_ta = lv_textarea_create(dlg);
                lv_obj_set_size(pw_ta, 220, 30);
                lv_obj_align(pw_ta, LV_ALIGN_TOP_MID, 0, 52);
                lv_textarea_set_password_mode(pw_ta, true);
                lv_textarea_set_one_line(pw_ta, true);
                lv_textarea_set_max_length(pw_ta, 63);
                apply_pixel_input(pw_ta);
                lv_obj_set_style_text_color(pw_ta, lv_color_hex(TEXT_PRIMARY), 0);
                lv_group_add_obj(lv_group_get_default(), pw_ta);
                
                char* ssid_save = strdup(sel);
                lv_obj_add_event_cb(pw_ta, [](lv_event_t* ev) {
                    free(lv_event_get_user_data(ev));
                }, LV_EVENT_DELETE, ssid_save);
                
                // Save button
                lv_obj_t* save_btn = lv_btn_create(dlg);
                lv_obj_set_size(save_btn, 80, 26);
                lv_obj_align(save_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
                apply_pixel_btn(save_btn);
                lv_obj_t* save_lbl = lv_label_create(save_btn);
                lv_label_set_text(save_lbl, "Save");
                lv_obj_set_style_text_color(save_lbl, lv_color_hex(BG_PRIMARY), 0);
                lv_obj_center(save_lbl);
                lv_group_add_obj(lv_group_get_default(), save_btn);
                
                lv_obj_add_event_cb(save_btn, [](lv_event_t* ev) {
                    lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev));
                    // Find textarea and network label
                    lv_obj_t* ta = nullptr;
                    lv_obj_t* net_lbl = nullptr;
                    uint32_t cnt = lv_obj_get_child_cnt(dlg);
                    for (uint32_t i = 0; i < cnt; i++) {
                        lv_obj_t* c = lv_obj_get_child(dlg, i);
                        if (lv_obj_check_type(c, &lv_textarea_class)) { ta = c; }
                        else if (net_lbl == nullptr && lv_obj_check_type(c, &lv_label_class)) {
                            if (i > 0) net_lbl = c;
                        }
                    }
                    if (!ta || !net_lbl) {
                        lv_obj_del_async(dlg);
                        return;
                    }
                    const char* label_text = lv_label_get_text(net_lbl);
                    // Copy SSID out of LVGL's internal buffer immediately —
                    // the label pointer could become invalid if the dialog
                    // is modified before WiFi.begin() consumes it.
                    char ssid_buf[33];
                    const char* src = label_text + 9; // skip "Network: "
                    strncpy(ssid_buf, src, sizeof(ssid_buf) - 1);
                    ssid_buf[sizeof(ssid_buf) - 1] = '\0';
                    const char* pw = lv_textarea_get_text(ta);

                    // Save credentials to prefs
                    auto p = sigurdos::prefs_get();
                    strncpy(p.wifi_ssid, ssid_buf, sizeof(p.wifi_ssid) - 1);
                    p.wifi_ssid[sizeof(p.wifi_ssid) - 1] = '\0';
                    strncpy(p.wifi_password, pw, sizeof(p.wifi_password) - 1);
                    p.wifi_password[sizeof(p.wifi_password) - 1] = '\0';
                    sigurdos::prefs_set(p);

                    // Disable buttons during connection attempt
                    lv_obj_add_state((lv_obj_t*)lv_event_get_target(ev), LV_STATE_DISABLED);
                    // Also disable cancel button (last btn child)
                    for (int32_t i = (int32_t)cnt - 1; i >= 0; i--) {
                        lv_obj_t* c = lv_obj_get_child(dlg, i);
                        if (lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE)) {
                            lv_obj_add_state(c, LV_STATE_DISABLED);
                            break;
                        }
                    }

                    // Show connecting feedback
                    lv_obj_t* title = lv_obj_get_child(dlg, 0);
                    if (title) {
                        lv_label_set_text(title, "Connecting...");
                        lv_obj_set_style_text_color(title, lv_color_hex(ACCENT), 0);
                    }

                    // Start async WiFi connection
                    sigurdos::wifi_sta::beginConnect(ssid_buf, pw);

                    // Poll connection status every 300ms
                    (void)lv_timer_create([](lv_timer_t* timer) {
                        lv_obj_t* dlg = (lv_obj_t*)lv_timer_get_user_data(timer);
                        if (!lv_obj_is_valid(dlg)) {
                            lv_timer_del(timer);
                            return;
                        }
                        auto status = sigurdos::wifi_sta::getStatus();
                        lv_obj_t* title = lv_obj_get_child(dlg, 0);
                        if (status == sigurdos::wifi_sta::Status::Connected) {
                            if (title) {
                                lv_label_set_text(title, "Connected!");
                                lv_obj_set_style_text_color(title,
                                    lv_color_hex(ACCENT_GREEN), 0);
                            }
                            lv_timer_del(timer);
                            // Auto-dismiss after 1.5s
                            lv_timer_t* t = lv_timer_create([](lv_timer_t* t2) {
                                lv_obj_t* d = (lv_obj_t*)lv_timer_get_user_data(t2);
                                if (lv_obj_is_valid(d)) lv_obj_del_async(d);
                                lv_timer_del(t2);
                            }, 1500, dlg);
                            lv_timer_set_repeat_count(t, 1);
                        } else if (status == sigurdos::wifi_sta::Status::Failed) {
                            if (title) {
                                lv_label_set_text(title, "Connection failed");
                                lv_obj_set_style_text_color(title,
                                    lv_color_hex(ACCENT_RED), 0);
                            }
                            lv_timer_del(timer);
                            // Auto-dismiss after 2.5s
                            lv_timer_t* t = lv_timer_create([](lv_timer_t* t2) {
                                lv_obj_t* d = (lv_obj_t*)lv_timer_get_user_data(t2);
                                if (lv_obj_is_valid(d)) lv_obj_del_async(d);
                                lv_timer_del(t2);
                            }, 2500, dlg);
                            lv_timer_set_repeat_count(t, 1);
                        }
                        // else still Connecting — keep polling
                    }, 300, dlg);
                }, LV_EVENT_CLICKED, nullptr);
                
                // Cancel button
                lv_obj_t* cancel_btn = lv_btn_create(dlg);
                lv_obj_set_size(cancel_btn, 80, 26);
                lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
                apply_pixel_btn_outline(cancel_btn);
                lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
                lv_label_set_text(cancel_lbl, "Cancel");
                lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(TEXT_PRIMARY), 0);
                lv_obj_center(cancel_lbl);
                lv_group_add_obj(lv_group_get_default(), cancel_btn);
                lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ev) {
                    // Abort any in-progress connection
                    sigurdos::wifi_sta::disconnect();
                    lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
                }, LV_EVENT_CLICKED, nullptr);
                
                // Focus the password field
                if (pw_ta) lv_group_focus_obj(pw_ta);
                
            }, LV_EVENT_CLICKED, ssid_copy);
            
            lv_obj_add_event_cb(btn, [](lv_event_t* de) {
                free(lv_event_get_user_data(de));
            }, LV_EVENT_DELETE, ssid_copy);
            
            if (first) {
                lv_group_focus_obj(btn);
                first = false;
            }
        }
    }
    lv_timer_del(timer);
}

void wifi_networks_screen_show()
{
    // Guard against active OTA
    if (sigurdos::ota::isActive()) {
        sigurdos::ota::stop();
    }
    
    lv_obj_t* scr = make_screen_full("WiFi");
    
    // Content container
    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_size(cont, CONTENT_W, CONTENT_H);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    
    // Scanning indicator
    lv_obj_t* scanning = lv_label_create(cont);
    lv_label_set_text(scanning, "Scanning for networks...");
    lv_obj_set_style_text_color(scanning, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(scanning, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_width(scanning, CONTENT_W - 8);
    
    // Results list (scrollable, flex column)
    lv_obj_t* list = lv_obj_create(cont);
    lv_obj_set_size(list, CONTENT_W - 8, CONTENT_H - 40);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    
    // Trackball support
    lv_group_t* g = lv_group_get_default();
    lv_indev_set_group(lv_indev_get_next(nullptr), g);
    
    // Reset scan state
    g_wifi_scan_done = false;
    g_wifi_ap_count = 0;
    
    // Defer scan to let screen render first
    lv_timer_create(wifi_do_scan, 400, list);
    
    show_screen(scr);
}

} // namespace sigurdos::ui
