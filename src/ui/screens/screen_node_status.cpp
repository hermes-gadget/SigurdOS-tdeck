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

static lv_timer_t* g_status_poll_timer = nullptr;
static lv_obj_t* g_status_waiting_label = nullptr;
static ScreenLifetime g_status_lifetime;
static ResponseWaitState g_status_wait;

void node_status_screen_begin_request(bool request_sent, uint32_t started_at_ms)
{
    g_status_wait.begin(started_at_ms, request_sent);
}

static const char* status_wait_message(ResponseWaitPhase phase)
{
    switch (phase) {
        case ResponseWaitPhase::SendFailed:
            return "Unable to send status request.";
        case ResponseWaitPhase::TimedOut:
            return "No status response.\nRequest timed out.";
        default:
            return "Requesting status...\nWaiting for response...";
    }
}

static void status_poll_timer_cb(lv_timer_t* timer)
{
    const ResponseWaitPhase phase = g_status_wait.phase(
        lv_tick_get(), sigurdos::mesh::hasStatusResponse());
    if (phase == ResponseWaitPhase::Loading) return;

    lv_timer_del(timer);
    if (g_status_poll_timer == timer) g_status_poll_timer = nullptr;

    if (phase == ResponseWaitPhase::Ready) {
        refresh_current_screen();
    } else if (g_status_waiting_label && lv_obj_is_valid(g_status_waiting_label)) {
        lv_label_set_text(g_status_waiting_label, status_wait_message(phase));
    }
}

// ════════════════════════════════════════════════════════
// Node Status screen (Phase 4.2)
// ════════════════════════════════════════════════════════
void node_status_screen_show()
{
    static constexpr int ROW_H = 18;
    if (g_status_poll_timer) {
        lv_timer_del(g_status_poll_timer);
        g_status_poll_timer = nullptr;
    }
    g_status_waiting_label = nullptr;

    lv_obj_t* scr = make_screen_full("Node Status");
    g_status_lifetime.bind(scr);
    g_status_lifetime.track(&g_status_waiting_label);
    g_status_lifetime.trackTimer(&g_status_poll_timer);
    g_status_lifetime.onDelete([] { g_status_wait.reset(); });

    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - 24);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    auto add_row = [&](const char* label, const char* value) {
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
        lv_obj_set_style_text_color(val, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(val, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -8, 0);
    };

    g_status_wait.ensureStarted(lv_tick_get());
    const ResponseWaitPhase wait_phase = g_status_wait.phase(
        lv_tick_get(), sigurdos::mesh::hasStatusResponse());

    if (wait_phase == ResponseWaitPhase::Ready) {
        sigurdos::mesh::NodeStatus st;
        sigurdos::mesh::getStatusResult(&st);

        char buf[64];

        snprintf(buf, sizeof(buf), "%d mV (%.2fV)", st.batt_milli_volts,
                 (float)st.batt_milli_volts / 1000.0f);
        add_row("Battery", buf);

        snprintf(buf, sizeof(buf), "%u s (%uh %um)",
                 st.total_up_time_secs,
                 st.total_up_time_secs / 3600,
                 (st.total_up_time_secs % 3600) / 60);
        add_row("Uptime", buf);

        snprintf(buf, sizeof(buf), "%u s TX / %u s RX",
                 st.total_air_time_secs, st.total_rx_air_time_secs);
        add_row("Airtime", buf);

        snprintf(buf, sizeof(buf), "%d dBm", st.last_rssi);
        add_row("Last RSSI", buf);

        snprintf(buf, sizeof(buf), "%.1f dB", (float)st.last_snr / 4.0f);
        add_row("Last SNR", buf);

        snprintf(buf, sizeof(buf), "%d dBm", st.noise_floor);
        add_row("Noise Floor", buf);

        snprintf(buf, sizeof(buf), "%u", st.curr_tx_queue_len);
        add_row("TX Queue", buf);

        snprintf(buf, sizeof(buf), "RX %u / TX %u / Err %u",
                 st.n_packets_recv, st.n_packets_sent, st.n_recv_errors);
        add_row("Packets", buf);

        snprintf(buf, sizeof(buf), "F %u/%u D %u/%u",
                 st.n_sent_flood, st.n_recv_flood,
                 st.n_sent_direct, st.n_recv_direct);
        add_row("Flood/Direct", buf);

        snprintf(buf, sizeof(buf), "Dups: D %u F %u / Err %u",
                 st.n_direct_dups, st.n_flood_dups, st.err_events);
        add_row("Dup/Err", buf);

        sigurdos::mesh::clearResponses();
        g_status_wait.reset();
    } else {
        g_status_waiting_label = lv_label_create(list);
        lv_label_set_text(g_status_waiting_label, status_wait_message(wait_phase));
        lv_obj_set_style_text_color(g_status_waiting_label, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(g_status_waiting_label, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(g_status_waiting_label, LV_ALIGN_CENTER, 0, 0);
    }

    show_screen(scr);
    if (wait_phase == ResponseWaitPhase::Loading) {
        g_status_poll_timer = lv_timer_create(status_poll_timer_cb, 250, nullptr);
    }
}

} // namespace sigurdos::ui
