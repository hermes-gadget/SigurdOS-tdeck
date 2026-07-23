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
#include "../lv_timer_owner.h"
#include "../wifi_credentials_policy.h"
#include "../../hal/wifi_ota.h"
#include "../../hal/wifi_coordinator.h"
#include "../../hal/prefs.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <new>

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

struct WifiScanCtx {
    lv_obj_t* list;
    LvTimerOwner timer;
};

struct WifiDialogCtx {
    lv_obj_t* dialog = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* save_btn = nullptr;
    LvTimerOwner poll_timer;
    LvTimerOwner dismiss_timer;
    WifiCredentialStage staged;
    uint32_t generation = 0;
    bool closing = false;
};

static WifiDialogCtx* g_wifi_dialog = nullptr;
static uint32_t g_wifi_dialog_generation = 0;

static bool wifi_dialog_current(const WifiDialogCtx* ctx)
{
    return ctx && ctx == g_wifi_dialog && !ctx->closing &&
           ctx->generation == g_wifi_dialog_generation;
}

static void wifi_dialog_dismiss(lv_timer_t* timer)
{
    auto* ctx = static_cast<WifiDialogCtx*>(lv_timer_get_user_data(timer));
    if (!wifi_dialog_current(ctx)) {
        if (ctx) ctx->dismiss_timer.complete(timer);
        else lv_timer_del(timer);
        return;
    }
    ctx->dismiss_timer.complete(timer);
    ctx->closing = true;
    lv_obj_del_async(ctx->dialog);
}

static void wifi_connection_poll(lv_timer_t* timer)
{
    auto* ctx = static_cast<WifiDialogCtx*>(lv_timer_get_user_data(timer));
    if (!wifi_dialog_current(ctx)) {
        if (ctx) ctx->poll_timer.complete(timer);
        else lv_timer_del(timer);
        return;
    }

    const auto status = sigurdos::wifi_sta::getStatus();
    if (status == sigurdos::wifi_sta::Status::Connected) {
        auto prefs = sigurdos::prefs_get();
        if (wifi_credentials_commit(ctx->staged, true,
                                    prefs.wifi_ssid, sizeof(prefs.wifi_ssid),
                                    prefs.wifi_password, sizeof(prefs.wifi_password))) {
            sigurdos::prefs_set(prefs);
        }
        lv_label_set_text(ctx->title, "Connected!");
        lv_obj_set_style_text_color(ctx->title, lv_color_hex(ACCENT_GREEN), 0);
        ctx->poll_timer.complete(timer);
        lv_timer_t* dismiss = lv_timer_create(wifi_dialog_dismiss, 1500, ctx);
        if (dismiss) {
            lv_timer_set_repeat_count(dismiss, 1);
            ctx->dismiss_timer.attach(dismiss);
        }
    } else if (status == sigurdos::wifi_sta::Status::Failed) {
        lv_label_set_text(ctx->title, "Connection failed");
        lv_obj_set_style_text_color(ctx->title, lv_color_hex(ACCENT_RED), 0);
        lv_obj_clear_state(ctx->save_btn, LV_STATE_DISABLED);
        ctx->poll_timer.complete(timer);
    }
}

static void show_wifi_password_dialog(lv_obj_t* screen, const char* ssid)
{
    if (!screen || !ssid || !ssid[0]) return;
    if (g_wifi_dialog) return;
    auto* ctx = new(std::nothrow) WifiDialogCtx{};
    if (!ctx || !wifi_credentials_stage(ctx->staged, ssid, "")) {
        delete ctx;
        return;
    }

    auto dlg_sz = dialog_size(260, 140);
    lv_obj_t* dlg = lv_obj_create(screen);
    if (!dlg) { delete ctx; return; }
    ctx->dialog = dlg;
    ctx->generation = ++g_wifi_dialog_generation;
    if (ctx->generation == 0) ctx->generation = ++g_wifi_dialog_generation;
    g_wifi_dialog = ctx;
    lv_obj_set_user_data(dlg, ctx);
    lv_obj_add_event_cb(dlg, [](lv_event_t* event) {
        auto* owned = static_cast<WifiDialogCtx*>(lv_event_get_user_data(event));
        if (!owned) return;
        owned->closing = true;
        if (sigurdos::wifi_sta::getStatus() == sigurdos::wifi_sta::Status::Connecting) {
            sigurdos::wifi_sta::disconnect();
        }
        if (owned == g_wifi_dialog) g_wifi_dialog = nullptr;
        delete owned;
    }, LV_EVENT_DELETE, ctx);

    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_border_color(dlg, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    ctx->title = lv_label_create(dlg);
    lv_label_set_text(ctx->title, "Enter Password");
    lv_obj_set_style_text_color(ctx->title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(ctx->title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(ctx->title, LV_ALIGN_TOP_MID, 0, 4);

    char ssid_label[48];
    snprintf(ssid_label, sizeof(ssid_label), "Network: %s", ctx->staged.ssid);
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
    lv_group_add_obj(lv_group_get_default(), pw_ta);

    ctx->save_btn = lv_btn_create(dlg);
    lv_obj_set_size(ctx->save_btn, 80, 26);
    lv_obj_align(ctx->save_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    apply_pixel_btn(ctx->save_btn);
    lv_obj_t* save_lbl = lv_label_create(ctx->save_btn);
    lv_label_set_text(save_lbl, "Connect");
    lv_obj_center(save_lbl);
    lv_group_add_obj(lv_group_get_default(), ctx->save_btn);
    lv_obj_add_event_cb(ctx->save_btn, [](lv_event_t* event) {
        auto* owned = static_cast<WifiDialogCtx*>(lv_event_get_user_data(event));
        if (!wifi_dialog_current(owned)) return;
        lv_obj_t* ta = nullptr;
        const uint32_t count = lv_obj_get_child_cnt(owned->dialog);
        for (uint32_t i = 0; i < count; ++i) {
            lv_obj_t* child = lv_obj_get_child(owned->dialog, i);
            if (lv_obj_check_type(child, &lv_textarea_class)) { ta = child; break; }
        }
        if (!ta) return;
        char ssid[sizeof(owned->staged.ssid)];
        std::strncpy(ssid, owned->staged.ssid, sizeof(ssid));
        ssid[sizeof(ssid) - 1] = '\0';
        if (!wifi_credentials_stage(owned->staged, ssid, lv_textarea_get_text(ta))) return;
        lv_obj_add_state(owned->save_btn, LV_STATE_DISABLED);
        lv_label_set_text(owned->title, "Connecting...");
        lv_obj_set_style_text_color(owned->title, lv_color_hex(ACCENT), 0);
        if (!sigurdos::wifi_sta::beginConnect(owned->staged.ssid,
                                              owned->staged.password)) {
            char busy[80];
            snprintf(busy, sizeof(busy), "WiFi busy: %s",
                     sigurdos::wifi::ownerName(sigurdos::wifi::currentOwner()));
            lv_label_set_text(owned->title, busy);
            lv_obj_set_style_text_color(owned->title, lv_color_hex(ACCENT_RED), 0);
            lv_obj_clear_state(owned->save_btn, LV_STATE_DISABLED);
            return;
        }
        lv_timer_t* poll = lv_timer_create(wifi_connection_poll, 300, owned);
        if (poll) owned->poll_timer.attach(poll);
        else {
            sigurdos::wifi_sta::disconnect();
            lv_label_set_text(owned->title, "Unable to start connection");
            lv_obj_clear_state(owned->save_btn, LV_STATE_DISABLED);
        }
    }, LV_EVENT_CLICKED, ctx);

    lv_obj_t* cancel_btn = lv_btn_create(dlg);
    lv_obj_set_size(cancel_btn, 80, 26);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    apply_pixel_btn_outline(cancel_btn);
    lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);
    lv_group_add_obj(lv_group_get_default(), cancel_btn);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* event) {
        auto* owned = static_cast<WifiDialogCtx*>(lv_event_get_user_data(event));
        if (!wifi_dialog_current(owned)) return;
        owned->closing = true;
        sigurdos::wifi_sta::disconnect();
        lv_obj_del_async(owned->dialog);
    }, LV_EVENT_CLICKED, ctx);

    lv_group_focus_obj(pw_ta);
}

static void wifi_do_scan(lv_timer_t* timer) {
    auto* ctx = (WifiScanCtx*)lv_timer_get_user_data(timer);
    if (!ctx || !lv_obj_is_valid(ctx->list)) {
        if (ctx) ctx->timer.complete(timer);
        else lv_timer_del(timer);
        return;
    }
    lv_obj_t* list = ctx->list;
    
    g_wifi_ap_count = sigurdos::wifi_scan::scan(g_wifi_aps, 30);
    g_wifi_scan_done = true;
    
    lv_obj_clean(list);
    
    if (g_wifi_ap_count == sigurdos::wifi_scan::SIGURDOS_WIFI_SCAN_BUSY) {
        char busy[80];
        snprintf(busy, sizeof(busy), "WiFi busy: %s",
                 sigurdos::wifi::ownerName(sigurdos::wifi::currentOwner()));
        lv_obj_t* empty = lv_label_create(list);
        lv_label_set_text(empty, busy);
        lv_obj_set_style_text_color(empty, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_text_font(empty, emoji_wrapped_montserrat_12, 0);
        lv_obj_center(empty);
    } else if (g_wifi_ap_count <= 0) {
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
            
            lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
                int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(ev))) - 1;
                if (index < 0 || index >= g_wifi_ap_count || index >= 30) return;
                lv_obj_t* scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(ev));
                show_wifi_password_dialog(scr, g_wifi_aps[index].ssid);
            }, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i + 1)));
            
            if (first) {
                lv_group_focus_obj(btn);
                first = false;
            }
        }
    }
    ctx->timer.complete(timer);
}

void wifi_networks_screen_show()
{
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

    if (!sigurdos::wifi::canAcquire(sigurdos::wifi::Owner::Scan)) {
        char busy[80];
        snprintf(busy, sizeof(busy), "WiFi busy: %s\nTry again when it finishes.",
                 sigurdos::wifi::ownerName(sigurdos::wifi::currentOwner()));
        lv_label_set_text(scanning, busy);
        lv_obj_set_style_text_color(scanning, lv_color_hex(ACCENT_RED), 0);
        show_screen(scr);
        return;
    }
    
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
    
    // Defer scan to let the screen render first. The list owns this context,
    // so navigation cancels the pending timer before LVGL frees the list.
    auto* scan_ctx = new(std::nothrow) WifiScanCtx{list, {}};
    if (!scan_ctx) {
        lv_label_set_text(scanning, "Unable to start scan");
        show_screen(scr);
        return;
    }
    lv_obj_add_event_cb(list, [](lv_event_t* e) {
        delete (WifiScanCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, scan_ctx);
    lv_timer_t* scan_timer = lv_timer_create(wifi_do_scan, 400, scan_ctx);
    scan_ctx->timer.attach(scan_timer);
    if (!scan_timer) lv_label_set_text(scanning, "Unable to start scan");
    
    show_screen(scr);
}

} // namespace sigurdos::ui
