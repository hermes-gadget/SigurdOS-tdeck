// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "screen_transports.h"
#include "../screens.h"
#include "../screens_common.h"
#include "../theme.h"
#include "../responsive.h"
#include "../lv_timer_owner.h"
#include "../../comms/transport_iface.h"
#include "../../hal/wifi_ota.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstdint>
#include <new>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

struct TransportRow {
    sigurdos::comms::TransportId id;
    lv_obj_t* status = nullptr;
    lv_obj_t* toggle = nullptr;
};

struct TransportScreenCtx {
    lv_obj_t* wifi_link = nullptr;
    TransportRow rows[2];
    LvTimerOwner timer;
};

static const char* transport_name(sigurdos::comms::TransportId id)
{
    return id == sigurdos::comms::TransportId::TCP ? "TCP" : "WebSocket";
}

static const char* transport_default_detail(sigurdos::comms::TransportId id)
{
    return id == sigurdos::comms::TransportId::TCP
        ? "port 5000" : "port 8765";
}

static void render_wifi_link(TransportScreenCtx& ctx)
{
    if (!ctx.wifi_link || !lv_obj_is_valid(ctx.wifi_link)) return;

    char text[128];
    uint32_t color = TEXT_SECONDARY;
    if (sigurdos::ota::isAccessPointActive()) {
        snprintf(text, sizeof(text),
                 "WiFi link: OTA AP active — transports parked");
        color = ACCENT_ORANGE;
    } else {
        const auto info = sigurdos::wifi_sta::getStatusInfo();
        if (info.status == sigurdos::wifi_sta::Status::Connected) {
            snprintf(text, sizeof(text), "WiFi STA: %s  %s",
                     info.ssid[0] ? info.ssid : "connected",
                     info.ip[0] ? info.ip : "no IP");
            color = ACCENT_GREEN;
        } else if (info.status == sigurdos::wifi_sta::Status::Connecting) {
            snprintf(text, sizeof(text), "WiFi STA: connecting to %s",
                     info.ssid[0] ? info.ssid : "network");
            color = ACCENT;
        } else {
            snprintf(text, sizeof(text), "WiFi STA: unavailable");
        }
    }
    lv_label_set_text(ctx.wifi_link, text);
    lv_obj_set_style_text_color(ctx.wifi_link, lv_color_hex(color), 0);
}

static void render_transport_row(TransportRow& row)
{
    if (!row.status || !lv_obj_is_valid(row.status) ||
        !row.toggle || !lv_obj_is_valid(row.toggle)) {
        return;
    }

    const sigurdos::comms::TransportStatus status =
        sigurdos::comms::transport_status(row.id);
    const bool parked = !sigurdos::ota::companionTransportsAllowed();
    const char* detail = status.detail && status.detail[0]
        ? status.detail : transport_default_detail(row.id);
    char text[192];

    if (parked && status.enabled) {
        snprintf(text, sizeof(text), "%s: ON / parked for OTA\nClients: %d  %s",
                 transport_name(row.id), status.clientCount, detail);
    } else {
        snprintf(text, sizeof(text), "%s: %s / %s\nClients: %d  %s",
                 transport_name(row.id), status.enabled ? "ON" : "OFF",
                 status.connected ? "connected" : "waiting",
                 status.clientCount, detail);
    }
    lv_label_set_text(row.status, text);

    const uint32_t color = parked && status.enabled
        ? ACCENT_ORANGE
        : (status.enabled
            ? (status.connected ? ACCENT_GREEN : ACCENT)
            : TEXT_PRIMARY);
    lv_obj_set_style_text_color(row.status, lv_color_hex(color), 0);

    if (status.enabled) {
        lv_obj_add_state(row.toggle, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(row.toggle, LV_STATE_CHECKED);
    }
}

static void transport_status_poll(lv_timer_t* timer)
{
    auto* ctx = static_cast<TransportScreenCtx*>(lv_timer_get_user_data(timer));
    if (!ctx || !ctx->wifi_link || !lv_obj_is_valid(ctx->wifi_link) ||
        !ctx->rows[0].status || !lv_obj_is_valid(ctx->rows[0].status)) {
        if (ctx) ctx->timer.complete(timer);
        else lv_timer_del(timer);
        return;
    }
    render_wifi_link(*ctx);
    render_transport_row(ctx->rows[0]);
    render_transport_row(ctx->rows[1]);
}

static void transport_toggle(lv_event_t* event)
{
    auto* row = static_cast<TransportRow*>(lv_event_get_user_data(event));
    if (!row || !row->toggle) return;

    const bool enable = !sigurdos::comms::transport_enabled(row->id);
    if (enable && !sigurdos::ota::companionTransportsAllowed()) {
        // The OTA AP is an exclusive radio owner. Leave the persisted toggle
        // unchanged; the transport layer will be unparked after OTA stops.
        render_transport_row(*row);
        return;
    }

    (void)sigurdos::comms::transport_set_enabled(row->id, enable);
    render_transport_row(*row);
}

void transports_screen_show()
{
    lv_obj_t* scr = make_screen_full("Network");
    apply_dark_bg(scr);

    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_size(cont, CONTENT_W, CONTENT_H);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    // cppcheck-suppress legacyUninitvar
    auto* ctx = new(std::nothrow) TransportScreenCtx{};
    if (!ctx) {
        lv_obj_t* error = lv_label_create(cont);
        lv_label_set_text(error, "Unable to create network controls");
        lv_obj_set_style_text_color(error, lv_color_hex(ACCENT_RED), 0);
        show_screen(scr);
        return;
    }

    lv_obj_add_event_cb(cont, [](lv_event_t* event) {
        auto* owned = static_cast<TransportScreenCtx*>(
            lv_event_get_user_data(event));
        delete owned;
    }, LV_EVENT_DELETE, ctx);

    const sigurdos::comms::TransportId ids[] = {
        sigurdos::comms::TransportId::TCP,
        sigurdos::comms::TransportId::WS,
    };

    ctx->wifi_link = lv_label_create(cont);
    lv_obj_set_width(ctx->wifi_link, CONTENT_W - 8);
    lv_obj_set_style_text_font(ctx->wifi_link, emoji_wrapped_montserrat_10, 0);
    render_wifi_link(*ctx);

    for (int i = 0; i < 2; ++i) {
        TransportRow& row = ctx->rows[i];
        row.id = ids[i];

        lv_obj_t* card = lv_obj_create(cont);
        lv_obj_set_size(card, CONTENT_W - 8, CONTENT_H / 4);
        apply_pixel_card(card);
        lv_obj_set_style_pad_all(card, 6, 0);

        row.status = lv_label_create(card);
        lv_obj_set_width(row.status, CONTENT_W - 90);
        lv_label_set_long_mode(row.status, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(row.status, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(row.status, LV_ALIGN_LEFT_MID, 2, 0);

        row.toggle = lv_switch_create(card);
        lv_obj_set_size(row.toggle, capped_width(52), 24);
        lv_obj_align(row.toggle, LV_ALIGN_RIGHT_MID, -2, 0);
        lv_obj_set_style_radius(row.toggle, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row.toggle, lv_color_hex(BG_INPUT), LV_PART_MAIN);
        lv_obj_set_style_border_width(row.toggle, PIXEL_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_color(row.toggle, lv_color_hex(DIVIDER), LV_PART_MAIN);
        lv_obj_set_style_radius(row.toggle, 0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(row.toggle, lv_color_hex(ACCENT),
                                   LV_PART_INDICATOR);
        lv_obj_set_style_radius(row.toggle, 0, LV_PART_KNOB);
        lv_obj_set_style_bg_color(row.toggle, lv_color_hex(ACCENT_FOREGROUND),
                                   LV_PART_KNOB);
        apply_focus_style(row.toggle);
        lv_obj_add_event_cb(row.toggle, transport_toggle, LV_EVENT_VALUE_CHANGED,
                            &row);
        lv_group_add_obj(lv_group_get_default(), row.toggle);
        render_transport_row(row);
    }

    lv_obj_t* notice = lv_label_create(cont);
    lv_label_set_text(notice,
        "TCP and WebSocket share the WiFi STA link.\n"
        "BLE companion is separate — configure it in Bluetooth.");
    lv_obj_set_width(notice, CONTENT_W - 8);
    lv_label_set_long_mode(notice, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(notice, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(notice, emoji_wrapped_montserrat_10, 0);

    lv_timer_t* timer = lv_timer_create(transport_status_poll, 500, ctx);
    ctx->timer.attach(timer);
    show_screen(scr);
}

}  // namespace sigurdos::ui
