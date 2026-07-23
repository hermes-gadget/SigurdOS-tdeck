// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
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
#include "../theme.h"
#include "../responsive.h"
#include "../mesh_dashboard_metrics.h"
#include "../../hal/prefs.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

static constexpr uint32_t DASHBOARD_REFRESH_MS = 1000;
static constexpr int DASHBOARD_PACKET_SAMPLE_LIMIT = 50;

struct DashboardBar {
    lv_obj_t* label = nullptr;
    lv_obj_t* bar = nullptr;
};

struct DashboardWidgets {
    lv_obj_t* nodes = nullptr;
    lv_obj_t* throughput = nullptr;
    lv_obj_t* rssi = nullptr;
    lv_obj_t* snr = nullptr;
    DashboardBar duty;
    DashboardBar utilization;
    DashboardBar nearby_battery;
};

static DashboardWidgets g_widgets;
static lv_timer_t* g_refresh_timer = nullptr;
static ScreenLifetime g_dashboard_lifetime;

static uint32_t health_color(DashboardHealth h)
{
    switch (h) {
    case DashboardHealth::Good: return ACCENT_GREEN;
    case DashboardHealth::Fair: return ACCENT_ORANGE;
    case DashboardHealth::Poor: default: return ACCENT_RED;
    }
}

// ── Small metric card: title on top, big value below ────────
static lv_obj_t* make_card(lv_obj_t* parent, int x, int y, int w, int h)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    apply_pixel_card(card);
    lv_obj_set_style_border_color(card, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_style_pad_all(card, 3, 0);
    lv_obj_clear_flag(
        card,
        static_cast<lv_obj_flag_t>(
            LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return card;
}

static lv_obj_t* card_labels(lv_obj_t* card, const char* title)
{
    lv_obj_t* t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(t, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* v = lv_label_create(card);
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_color(v, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(v, emoji_wrapped_montserrat_14, 0);
    lv_obj_align(v, LV_ALIGN_LEFT_MID, 0, 6);
    return v;
}

// ── Labelled progress bar ───────────────────────────────────
static DashboardBar make_bar(lv_obj_t* parent, int x, int y, int w)
{
    DashboardBar result;
    result.label = lv_label_create(parent);
    lv_label_set_text(result.label, "--");
    lv_obj_set_style_text_color(result.label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(result.label, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_pos(result.label, x, y);

    result.bar = lv_bar_create(parent);
    lv_obj_set_size(result.bar, w, 8);
    lv_obj_set_pos(result.bar, x, y + 13);
    lv_obj_set_style_radius(result.bar, 0, 0);
    lv_obj_set_style_bg_color(result.bar, lv_color_hex(BG_INPUT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(result.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(result.bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        result.bar, lv_color_hex(TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_radius(result.bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(result.bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(result.bar, 0, 100);
    return result;
}

static void update_bar(const DashboardBar& widgets, const char* title,
                       int pct, uint32_t color, const char* detail = nullptr)
{
    char buf[64];
    if (detail && detail[0]) {
        snprintf(buf, sizeof(buf), "%s  %s", title, detail);
    } else {
        snprintf(buf, sizeof(buf), "%s  %d%%", title, dashboard_clamp_pct(pct));
    }
    lv_label_set_text(widgets.label, buf);
    lv_obj_set_style_bg_color(
        widgets.bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_bar_set_value(
        widgets.bar, dashboard_clamp_pct(pct), LV_ANIM_OFF);
}

static int sample_packet_signals(float* avg_rssi, float* avg_snr)
{
    sigurdos::mesh::PacketLogEntry samples[DASHBOARD_PACKET_SAMPLE_LIMIT];
    int sample_count = 0;
    int count = sigurdos::mesh::getPacketLogCount();
    if (count > DASHBOARD_PACKET_SAMPLE_LIMIT) {
        count = DASHBOARD_PACKET_SAMPLE_LIMIT;
    }
    for (int i = 0; i < count; ++i) {
        sigurdos::mesh::PacketLogEntry entry{};
        if (!sigurdos::mesh::getPacketLogEntry(i, &entry) ||
            entry.rssi == 0) {
            continue;
        }
        samples[sample_count++] = entry;
    }

    int rssi_count = 0;
    *avg_rssi = dashboard_average_rssi(samples, sample_count, &rssi_count);
    *avg_snr = dashboard_average_snr(samples, sample_count);
    return rssi_count;
}

static int sample_nearby_batteries(float* average_volts)
{
    // Calling has* refreshes the wrapper's parsed cache if the matching
    // response is still queued. get* also exposes the most recently parsed
    // value after another screen has consumed the response queue.
    sigurdos::mesh::hasStatusResponse();
    sigurdos::mesh::hasTelemetryResponse();

    float sum = 0.0f;
    int count = 0;

    sigurdos::mesh::NodeStatus status{};
    if (sigurdos::mesh::getStatusResult(&status) &&
        status.batt_milli_volts > 0) {
        sum += static_cast<float>(status.batt_milli_volts) / 1000.0f;
        ++count;
    }

    sigurdos::mesh::TelemetryResult telemetry{};
    if (sigurdos::mesh::getTelemetryResult(&telemetry)) {
        for (int i = 0; i < telemetry.n_items; ++i) {
            if (telemetry.items[i].type == 116 &&
                telemetry.items[i].value_float > 0.0f) {
                sum += telemetry.items[i].value_float;
                ++count;
            }
        }
    }

    *average_volts = count > 0 ? sum / static_cast<float>(count) : 0.0f;
    return count;
}

static void refresh_dashboard()
{
    const int node_count = sigurdos::mesh::getContactCount();
    float avg_rssi = 0.0f;
    float avg_snr = 0.0f;
    const int heard_count = sample_packet_signals(&avg_rssi, &avg_snr);

    const uint32_t total_packets =
        sigurdos::mesh::getNumSentFlood() + sigurdos::mesh::getNumSentDirect() +
        sigurdos::mesh::getNumRecvFlood() + sigurdos::mesh::getNumRecvDirect();
    const uint32_t uptime_ms = millis();
    const float packets_per_second =
        dashboard_packet_throughput(total_packets, uptime_ms);

    const sigurdos::NodePrefs& prefs = sigurdos::prefs_get();
    const int duty_used = dashboard_duty_budget_used_pct(
        prefs.duty_cycle, sigurdos::mesh::getRemainingTxBudget());
    const int channel_utilization = dashboard_channel_utilization_pct(
        sigurdos::mesh::getTotalRxAirtimeMs(), uptime_ms);

    float nearby_battery_volts = 0.0f;
    const int battery_samples =
        sample_nearby_batteries(&nearby_battery_volts);

    char buf[48];
    snprintf(buf, sizeof(buf), "%d", node_count);
    lv_label_set_text(g_widgets.nodes, buf);

    snprintf(buf, sizeof(buf), "%.2f/s", packets_per_second);
    lv_label_set_text(g_widgets.throughput, buf);

    if (heard_count > 0) {
        snprintf(buf, sizeof(buf), "%.0f dBm", avg_rssi);
        lv_label_set_text(g_widgets.rssi, buf);
        lv_obj_set_style_text_color(
            g_widgets.rssi,
            lv_color_hex(health_color(dashboard_signal_health(avg_rssi))), 0);
        snprintf(buf, sizeof(buf), "%.1f dB", avg_snr);
        lv_label_set_text(g_widgets.snr, buf);
    } else {
        lv_label_set_text(g_widgets.rssi, "-- dBm");
        lv_label_set_text(g_widgets.snr, "-- dB");
        lv_obj_set_style_text_color(
            g_widgets.rssi, lv_color_hex(TEXT_SECONDARY), 0);
    }

    update_bar(g_widgets.duty, "Duty budget", duty_used,
               duty_used >= 90 ? ACCENT_RED : ACCENT);
    update_bar(g_widgets.utilization, "Channel util", channel_utilization,
               channel_utilization >= 80 ? ACCENT_ORANGE : ACCENT);

    if (battery_samples > 0) {
        const int battery_pct =
            dashboard_battery_voltage_to_pct(nearby_battery_volts);
        snprintf(buf, sizeof(buf), "%.2fV (%d)", nearby_battery_volts,
                 battery_samples);
        update_bar(g_widgets.nearby_battery, "Nearby battery", battery_pct,
                   battery_pct > 20 ? ACCENT_GREEN : ACCENT_RED, buf);
    } else {
        update_bar(g_widgets.nearby_battery, "Nearby battery", 0,
                   TEXT_MUTED, "no telemetry");
    }
}

// ════════════════════════════════════════════════════════
// Mesh Health Dashboard — network-wide metrics at a glance
// ════════════════════════════════════════════════════════
void mesh_dashboard_screen_show()
{
    lv_obj_t* scr = make_screen_full("Mesh Health");
    if (g_refresh_timer) {
        lv_timer_del(g_refresh_timer);
        g_refresh_timer = nullptr;
    }
    g_dashboard_lifetime.bind(scr);
    g_dashboard_lifetime.trackTimer(&g_refresh_timer);

    // ── Layout ──────────────────────────────────────────────
    const int y0   = CONTENT_Y + 4;
    const int colw = (CONTENT_W - 12) / 2;
    const int cardh = 34;

    // Row 1 — nodes + throughput cards
    lv_obj_t* c_nodes = make_card(scr, 4, y0, colw, cardh);
    g_widgets.nodes = card_labels(c_nodes, "Known nodes");

    lv_obj_t* c_thru = make_card(scr, 8 + colw, y0, colw, cardh);
    g_widgets.throughput = card_labels(c_thru, "Throughput");

    // Row 2 — avg RSSI + avg SNR cards (health-coloured RSSI)
    const int y1 = y0 + cardh + 4;
    lv_obj_t* c_rssi = make_card(scr, 4, y1, colw, cardh);
    g_widgets.rssi = card_labels(c_rssi, "Avg packet RSSI");

    lv_obj_t* c_snr = make_card(scr, 8 + colw, y1, colw, cardh);
    g_widgets.snr = card_labels(c_snr, "Avg packet SNR");

    // Row 3+ — bars for the percentage metrics
    int by = y1 + cardh + 8;
    const int barw = CONTENT_W - 16;

    g_widgets.duty = make_bar(scr, 8, by, barw);
    by += 28;
    g_widgets.utilization = make_bar(scr, 8, by, barw);
    by += 28;
    g_widgets.nearby_battery = make_bar(scr, 8, by, barw);

    g_dashboard_lifetime.track(&g_widgets.nodes);
    g_dashboard_lifetime.track(&g_widgets.throughput);
    g_dashboard_lifetime.track(&g_widgets.rssi);
    g_dashboard_lifetime.track(&g_widgets.snr);
    g_dashboard_lifetime.track(&g_widgets.duty.label);
    g_dashboard_lifetime.track(&g_widgets.duty.bar);
    g_dashboard_lifetime.track(&g_widgets.utilization.label);
    g_dashboard_lifetime.track(&g_widgets.utilization.bar);
    g_dashboard_lifetime.track(&g_widgets.nearby_battery.label);
    g_dashboard_lifetime.track(&g_widgets.nearby_battery.bar);

    refresh_dashboard();
    g_refresh_timer = lv_timer_create(
        [](lv_timer_t*) { refresh_dashboard(); },
        DASHBOARD_REFRESH_MS, nullptr);

    show_screen(scr);
}

} // namespace sigurdos::ui
