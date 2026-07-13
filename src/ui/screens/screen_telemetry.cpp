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
#include "../navigation.h"
#include "../response_wait_state.h"
#include "../screen_lifetime.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

static lv_timer_t* g_telemetry_poll_timer = nullptr;
static lv_obj_t* g_telemetry_waiting_label = nullptr;
static ScreenLifetime g_telemetry_lifetime;
static ResponseWaitState g_telemetry_wait;

void telemetry_screen_begin_request(bool request_sent, uint32_t started_at_ms)
{
    g_telemetry_wait.begin(started_at_ms, request_sent);
}

static const char* telemetry_wait_message(ResponseWaitPhase phase)
{
    switch (phase) {
        case ResponseWaitPhase::SendFailed:
            return "Unable to send telemetry request.";
        case ResponseWaitPhase::TimedOut:
            return "No telemetry response.\nRequest timed out.";
        default:
            return "Requesting telemetry...\nWaiting for response...";
    }
}

static void telemetry_poll_timer_cb(lv_timer_t* timer)
{
    const ResponseWaitPhase phase = g_telemetry_wait.phase(
        lv_tick_get(), sigurdos::mesh::hasTelemetryResponse());
    if (phase == ResponseWaitPhase::Loading) return;

    lv_timer_del(timer);
    if (g_telemetry_poll_timer == timer) g_telemetry_poll_timer = nullptr;

    if (phase == ResponseWaitPhase::Ready) {
        refresh_current_screen();
    } else if (g_telemetry_waiting_label && lv_obj_is_valid(g_telemetry_waiting_label)) {
        lv_label_set_text(g_telemetry_waiting_label, telemetry_wait_message(phase));
    }
}

// ════════════════════════════════════════════════════════
// Telemetry screen (Phase 4.3)
// ════════════════════════════════════════════════════════
void telemetry_screen_show()
{
    static constexpr int ROW_H = 20;
    if (g_telemetry_poll_timer) {
        lv_timer_del(g_telemetry_poll_timer);
        g_telemetry_poll_timer = nullptr;
    }
    g_telemetry_waiting_label = nullptr;

    lv_obj_t* scr = make_screen_full("Telemetry");
    g_telemetry_lifetime.bind(scr);
    g_telemetry_lifetime.track(&g_telemetry_waiting_label);
    g_telemetry_lifetime.trackTimer(&g_telemetry_poll_timer);
    g_telemetry_lifetime.onDelete([] { g_telemetry_wait.reset(); });

    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - 24);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    auto add_row = [&](const char* label, const char* value, uint32_t color) {
        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_left(row, 8, 0);
        lv_obj_set_style_pad_right(row, 8, 0);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_10, 0);

        lv_obj_t* val = lv_label_create(row);
        lv_label_set_text(val, value);
        lv_obj_set_style_text_color(val, lv_color_hex(color), 0);
        lv_obj_set_style_text_font(val, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -8, 0);
    };

    g_telemetry_wait.ensureStarted(lv_tick_get());
    const ResponseWaitPhase wait_phase = g_telemetry_wait.phase(
        lv_tick_get(), sigurdos::mesh::hasTelemetryResponse());

    if (wait_phase == ResponseWaitPhase::Ready) {
        sigurdos::mesh::TelemetryResult tr;
        sigurdos::mesh::getTelemetryResult(&tr);

        if (tr.n_items == 0) {
            lv_obj_t* empty = lv_label_create(list);
            lv_label_set_text(empty, "No telemetry data");
            lv_obj_set_style_text_color(empty, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
        } else {
            for (int i = 0; i < tr.n_items; i++) {
                auto& item = tr.items[i];
                uint32_t color = item.type == 116  // LPP_VOLTAGE
                    ? ACCENT_GREEN : item.type == 103 // LPP_TEMPERATURE
                    ? ACCENT : TEXT_PRIMARY;
                char label[32];
                snprintf(label, sizeof(label), "Ch.%d", item.channel);
                add_row(label, item.value_str, color);
            }
        }

        sigurdos::mesh::clearResponses();
        g_telemetry_wait.reset();
    } else {
        g_telemetry_waiting_label = lv_label_create(list);
        lv_label_set_text(g_telemetry_waiting_label, telemetry_wait_message(wait_phase));
        lv_obj_set_style_text_color(g_telemetry_waiting_label, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(g_telemetry_waiting_label, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(g_telemetry_waiting_label, LV_ALIGN_CENTER, 0, 0);
    }

    show_screen(scr);
    if (wait_phase == ResponseWaitPhase::Loading) {
        g_telemetry_poll_timer = lv_timer_create(telemetry_poll_timer_cb, 250, nullptr);
    }
}

} // namespace sigurdos::ui
