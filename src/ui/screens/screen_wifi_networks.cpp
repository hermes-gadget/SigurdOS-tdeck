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
#include "../prefs_ui.h"
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

static sigurdos::wifi_scan::APInfo g_wifi_aps[30];
static int g_wifi_ap_count = 0;

struct WifiScreenCtx {
    lv_obj_t* list;
    lv_obj_t* scan_status;
    lv_obj_t* connection_status;
    lv_obj_t* reconnect;
    LvTimerOwner scan_timer;
    LvTimerOwner status_timer;
    uint32_t token = 0;
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

static void wifi_screen_update_status(WifiScreenCtx* ctx)
{
    if (!ctx || !ctx->connection_status || !lv_obj_is_valid(ctx->connection_status)) {
        return;
    }

    const auto info = sigurdos::wifi_sta::getStatusInfo();
    char status[192];
    uint32_t color = TEXT_SECONDARY;
    bool reconnect_allowed = true;

    if (sigurdos::ota::isAccessPointActive()) {
        const char* ip = sigurdos::ota::getIP();
        snprintf(status, sizeof(status), "AP mode (OTA)\nIP: %s",
                 ip && ip[0] ? ip : "starting");
        color = ACCENT_ORANGE;
        reconnect_allowed = false;
    } else if (info.status == sigurdos::wifi_sta::Status::Connected &&
               info.connected) {
        snprintf(status, sizeof(status), "STA: %s\nIP: %s  %d dBm",
                 info.ssid[0] ? info.ssid : "connected",
                 info.ip[0] ? info.ip : "no IP", info.rssi);
        color = ACCENT_GREEN;
    } else if (info.status == sigurdos::wifi_sta::Status::Connecting) {
        snprintf(status, sizeof(status), "STA: connecting to %s...",
                 info.ssid[0] ? info.ssid : "network");
        color = ACCENT;
    } else if (info.status == sigurdos::wifi_sta::Status::Failed) {
        snprintf(status, sizeof(status), "STA: failed\n%s",
                 info.error[0] ? info.error : "Connection failed");
        color = ACCENT_RED;
    } else if (info.error[0]) {
        snprintf(status, sizeof(status), "STA: disconnected\n%s",
                 info.error);
        color = ACCENT_ORANGE;
    } else {
        snprintf(status, sizeof(status), "STA: disconnected\nReconnect to saved WiFi");
    }

    lv_label_set_text(ctx->connection_status, status);
    lv_obj_set_style_text_color(ctx->connection_status, lv_color_hex(color), 0);

    if (ctx->reconnect && lv_obj_is_valid(ctx->reconnect)) {
        if (reconnect_allowed) {
            lv_obj_clear_state(ctx->reconnect, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(ctx->reconnect, LV_STATE_DISABLED);
        }
    }
}

static void wifi_screen_status_poll(lv_timer_t* timer)
{
    auto* ctx = static_cast<WifiScreenCtx*>(lv_timer_get_user_data(timer));
    if (!ctx || !ctx->connection_status ||
        !lv_obj_is_valid(ctx->connection_status)) {
        if (ctx) ctx->status_timer.complete(timer);
        else lv_timer_del(timer);
        return;
    }
    wifi_screen_update_status(ctx);
}

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
        const bool staged = wifi_credentials_commit(
            ctx->staged, true,
            prefs.wifi_ssid, sizeof(prefs.wifi_ssid),
            prefs.wifi_password, sizeof(prefs.wifi_password));
        const bool persisted = staged && prefs_ui_commit(prefs);
        if (!wifi_credentials_save_succeeded(true, staged, persisted)) {
            lv_label_set_text(ctx->title, "Connected, settings not saved");
            lv_obj_set_style_text_color(ctx->title, lv_color_hex(ACCENT_RED), 0);
            // Keep the dialog open so the user can retry or close it.  A live
            // association must not be presented as a durable configuration.
            lv_obj_clear_state(ctx->save_btn, LV_STATE_DISABLED);
            ctx->poll_timer.complete(timer);
            return;
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
        const auto info = sigurdos::wifi_sta::getStatusInfo();
        lv_label_set_text(ctx->title,
                          info.error[0] ? info.error : "Connection failed");
        lv_obj_set_style_text_color(ctx->title, lv_color_hex(ACCENT_RED), 0);
        lv_obj_clear_state(ctx->save_btn, LV_STATE_DISABLED);
        ctx->poll_timer.complete(timer);
    }
}

static void show_wifi_password_dialog(lv_obj_t* screen, const char* ssid)
{
    if (!screen || !ssid || !ssid[0]) return;
    if (g_wifi_dialog) return;
    // cppcheck-suppress legacyUninitvar
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

static void wifi_render_results(lv_obj_t* list) {
    lv_obj_clean(list);

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

        lv_group_add_obj(g, btn);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, [](lv_event_t* ev) {
            int index = static_cast<int>(
                reinterpret_cast<intptr_t>(lv_event_get_user_data(ev))) - 1;
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

static void wifi_scan_poll(lv_timer_t* timer) {
    auto* ctx = static_cast<WifiScreenCtx*>(lv_timer_get_user_data(timer));
    if (!ctx || !lv_obj_is_valid(ctx->list) ||
        !lv_obj_is_valid(ctx->scan_status)) {
        if (ctx) ctx->scan_timer.complete(timer);
        else lv_timer_del(timer);
        return;
    }

    const auto result = sigurdos::wifi_scan::poll(
        ctx->token, g_wifi_aps, sigurdos::wifi_scan::SIGURDOS_WIFI_SCAN_MAX_APS);
    g_wifi_ap_count = result.count;

    if (result.status == sigurdos::wifi_scan::Status::Running) {
        char progress[48];
        if (result.count > 0) {
            snprintf(progress, sizeof(progress), "Reading networks... %d", result.count);
            lv_label_set_text(ctx->scan_status, progress);
        }
        return;
    }

    ctx->scan_timer.complete(timer);
    if (result.status == sigurdos::wifi_scan::Status::Complete) {
        if (result.count == 0) {
            lv_label_set_text(ctx->scan_status, "No networks found");
        } else {
            char found[40];
            snprintf(found, sizeof(found), "%d networks found", result.count);
            lv_label_set_text(ctx->scan_status, found);
            wifi_render_results(ctx->list);
        }
    } else if (result.status == sigurdos::wifi_scan::Status::Error) {
        lv_label_set_text(ctx->scan_status, "WiFi scan failed");
        lv_obj_set_style_text_color(ctx->scan_status, lv_color_hex(ACCENT_RED), 0);
    }
}

void wifi_networks_screen_show()
{
    lv_obj_t* scr = make_screen_full("WiFi");
    apply_dark_bg(scr);

    // Content container
    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_size(cont, CONTENT_W, CONTENT_H);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    // cppcheck-suppress legacyUninitvar
    auto* screen_ctx = new(std::nothrow) WifiScreenCtx{};
    if (!screen_ctx) {
        lv_obj_t* failure = lv_label_create(cont);
        lv_label_set_text(failure, "Unable to create WiFi status");
        lv_obj_set_style_text_color(failure, lv_color_hex(ACCENT_RED), 0);
        show_screen(scr);
        return;
    }

    lv_obj_add_event_cb(cont, [](lv_event_t* e) {
        auto* owned = static_cast<WifiScreenCtx*>(lv_event_get_user_data(e));
        if (!owned) return;
        sigurdos::wifi_scan::cancel(owned->token);
        delete owned;
    }, LV_EVENT_DELETE, screen_ctx);

    // Live connection card — this remains useful even when scanning is
    // blocked by an OTA session or another WiFi owner.
    const int status_h = CONTENT_H / 4 < 54 ? 54 : CONTENT_H / 4;
    lv_obj_t* status_card = lv_obj_create(cont);
    lv_obj_set_size(status_card, CONTENT_W - 8, status_h);
    apply_pixel_card(status_card);
    lv_obj_set_style_pad_all(status_card, 6, 0);

    screen_ctx->connection_status = lv_label_create(status_card);
    lv_label_set_text(screen_ctx->connection_status, "WiFi: checking...");
    lv_obj_set_width(screen_ctx->connection_status, CONTENT_W - 108);
    lv_label_set_long_mode(screen_ctx->connection_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(screen_ctx->connection_status,
                                lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(screen_ctx->connection_status,
                               emoji_wrapped_montserrat_10, 0);
    lv_obj_align(screen_ctx->connection_status, LV_ALIGN_TOP_LEFT, 2, 2);

    screen_ctx->reconnect = lv_btn_create(status_card);
    lv_obj_set_size(screen_ctx->reconnect, capped_width(86), 28);
    lv_obj_align(screen_ctx->reconnect, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
    apply_pixel_btn_outline(screen_ctx->reconnect);
    lv_obj_t* reconnect_label = lv_label_create(screen_ctx->reconnect);
    lv_label_set_text(reconnect_label, "Reconnect");
    lv_obj_center(reconnect_label);
    lv_group_add_obj(lv_group_get_default(), screen_ctx->reconnect);
    lv_obj_add_event_cb(screen_ctx->reconnect, [](lv_event_t* e) {
        auto* owned = static_cast<WifiScreenCtx*>(lv_event_get_user_data(e));
        if (!owned || sigurdos::ota::isAccessPointActive()) return;
        lv_obj_add_state(owned->reconnect, LV_STATE_DISABLED);
        sigurdos::wifi_sta::reconnect();
        wifi_screen_update_status(owned);
    }, LV_EVENT_CLICKED, screen_ctx);

    lv_timer_t* status_timer =
        lv_timer_create(wifi_screen_status_poll, 500, screen_ctx);
    screen_ctx->status_timer.attach(status_timer);
    wifi_screen_update_status(screen_ctx);

    // Scanning indicator
    lv_obj_t* scanning = lv_label_create(cont);
    screen_ctx->scan_status = scanning;
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
        wifi_screen_update_status(screen_ctx);
        show_screen(scr);
        return;
    }

    // Results list (scrollable, flex column)
    lv_obj_t* list = lv_obj_create(cont);
    int list_h = CONTENT_H - status_h - 34;
    if (list_h < 40) list_h = 40;
    lv_obj_set_size(list, CONTENT_W - 8, list_h);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    
    // Trackball support
    lv_group_t* g = lv_group_get_default();
    lv_indev_set_group(lv_indev_get_next(nullptr), g);

    g_wifi_ap_count = 0;
    screen_ctx->list = list;

    const auto started = sigurdos::wifi_scan::begin();
    screen_ctx->token = started.token;
    if (started.status == sigurdos::wifi_scan::Status::Complete) {
        lv_label_set_text(scanning, "No networks found");
        wifi_screen_update_status(screen_ctx);
        show_screen(scr);
        return;
    }
    if (started.status != sigurdos::wifi_scan::Status::Running) {
        char failure[80];
        if (started.status == sigurdos::wifi_scan::Status::Busy) {
            snprintf(failure, sizeof(failure), "WiFi busy: %s",
                     sigurdos::wifi::ownerName(sigurdos::wifi::currentOwner()));
        } else {
            snprintf(failure, sizeof(failure), "Unable to start scan");
        }
        lv_label_set_text(scanning, failure);
        lv_obj_set_style_text_color(scanning, lv_color_hex(ACCENT_RED), 0);
        wifi_screen_update_status(screen_ctx);
        show_screen(scr);
        return;
    }

    lv_timer_t* scan_timer = lv_timer_create(wifi_scan_poll, 50, screen_ctx);
    screen_ctx->scan_timer.attach(scan_timer);
    if (!scan_timer) {
        sigurdos::wifi_scan::cancel(screen_ctx->token);
        lv_label_set_text(scanning, "Unable to start scan");
    }

    show_screen(scr);
}

} // namespace sigurdos::ui
