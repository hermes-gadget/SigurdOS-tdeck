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
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ════════════════════════════════════════════════════════
// Signal — radio statistics (two-column layout)
// ════════════════════════════════════════════════════════
void signal_screen_show()
{
    lv_obj_t* scr = make_screen_full("Signal");

    int rssi   = sigurdos::mesh::getLastRSSI();
    float snr  = sigurdos::mesh::getLastSNR();
    int noise  = sigurdos::mesh::getNoiseFloor();
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();

    if (!p.configured) {
        // ── Unconfigured: single centered message ──────────
        lv_obj_t* lbl = lv_label_create(scr);
        lv_obj_set_width(lbl, CONTENT_W);
        lv_obj_set_style_pad_left(lbl, 8, 0);
        lv_obj_set_style_pad_right(lbl, 8, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);

        char buf[256];
        snprintf(buf, sizeof(buf),
            "RSSI:    %d dBm\n"
            "SNR:     %.1f dB\n"
            "Noise:   %d dBm\n\n"
            "Radio:   NOT CONFIGURED\n"
            "Go to Settings > Radio\n"
            "to set frequency/power.",
            rssi, snr, noise);
        lv_label_set_text(lbl, buf);
        // ── RSSI sparkline (even unconfigured shows captured samples) ─
        int hist_count = sigurdos::mesh::getSignalHistoryCount();
        if (hist_count >= 2) {
            lv_obj_t* chart = lv_chart_create(scr);
            lv_obj_set_size(chart, CONTENT_W - 12, 60);
            lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 6, CONTENT_Y + 4 + 130);
            lv_obj_set_style_bg_color(chart, lv_color_hex(BG_TERTIARY), 0);
            lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(chart, 1, 0);
            lv_obj_set_style_border_color(chart, lv_color_hex(TEXT_MUTED), 0);
            lv_obj_set_style_pad_all(chart, 4, 0);
            lv_obj_set_style_radius(chart, 0, 0);
            lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
            lv_chart_set_div_line_count(chart, 4, 0);
            lv_chart_set_point_count(chart, hist_count);

            lv_chart_series_t* rssi_ser = lv_chart_add_series(chart, lv_color_hex(ACCENT), LV_CHART_AXIS_PRIMARY_Y);
            lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -120, 0);

            lv_chart_set_all_value(chart, rssi_ser, LV_CHART_POINT_NONE);
            for (int i = 0; i < hist_count && i < 64; i++) {
                int rssi_val = sigurdos::mesh::getSignalHistoryRSSI(i);
                lv_chart_set_value_by_id(chart, rssi_ser, i, rssi_val + 120);
            }

            lv_obj_t* ch_label = lv_label_create(scr);
            lv_label_set_text(ch_label, "RSSI History");
            lv_obj_set_style_text_color(ch_label, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(ch_label, emoji_wrapped_montserrat_12, 0);
            lv_obj_align_to(ch_label, chart, LV_ALIGN_OUT_TOP_LEFT, 0, -2);
        }
    } else {
        // ── Two-column flex row ─────────────────────────────
        lv_obj_t* row = lv_obj_create(scr);
        lv_obj_set_size(row, CONTENT_W, LV_SIZE_CONTENT);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        // Left column — signal metrics + airtime
        lv_obj_t* left = lv_label_create(row);
        lv_obj_set_width(left, LV_PCT(48));
        lv_obj_set_style_text_color(left, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(left, emoji_wrapped_montserrat_10, 0);

        char left_buf[256];
        snprintf(left_buf, sizeof(left_buf),
            "RSSI   %d dBm\n"
            "SNR    %.1f dB\n"
            "Noise  %d dBm\n"
            "Freq   %.3f\n"
            "BW     %.1f kHz\n"
            "RF     SF%d CR4/%d TX%d",
            rssi, snr, noise,
            p.freq, p.bw, p.sf, p.cr, p.tx_power_dbm);
        lv_label_set_text(left, left_buf);

        // Right column — packet counters + airtime + duty cycle
        lv_obj_t* right = lv_label_create(row);
        lv_obj_set_width(right, LV_PCT(48));
        lv_obj_set_style_text_color(right, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(right, emoji_wrapped_montserrat_10, 0);

        char right_buf[256];
        snprintf(right_buf, sizeof(right_buf),
            "TX Fld %u\n"
            "TX Dir %u\n"
            "RX Fld %u\n"
            "RX Dir %u\n"
            "Air %lu/%lu ms\n"
            "Duty %u%% %lums",
            sigurdos::mesh::getNumSentFlood(),
            sigurdos::mesh::getNumSentDirect(),
            sigurdos::mesh::getNumRecvFlood(),
            sigurdos::mesh::getNumRecvDirect(),
            sigurdos::mesh::getTotalTxAirtimeMs(),
            sigurdos::mesh::getTotalRxAirtimeMs(),
            (unsigned)p.duty_cycle,
            sigurdos::mesh::getRemainingTxBudget());
        lv_label_set_text(right, right_buf);

        // ── RSSI sparkline ─────────────────────────────────
        int hist_count = sigurdos::mesh::getSignalHistoryCount();
        if (hist_count >= 2) {
            lv_obj_t* chart = lv_chart_create(scr);
            lv_obj_set_size(chart, CONTENT_W - 12, 60);
            lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 6, CONTENT_Y + 4 + 112);
            lv_obj_set_style_bg_color(chart, lv_color_hex(BG_TERTIARY), 0);
            lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(chart, 1, 0);
            lv_obj_set_style_border_color(chart, lv_color_hex(TEXT_MUTED), 0);
            lv_obj_set_style_pad_all(chart, 4, 0);
            lv_obj_set_style_radius(chart, 0, 0);
            lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
            lv_chart_set_div_line_count(chart, 4, 0);
            lv_chart_set_point_count(chart, hist_count);

            // Create RSSI series
            lv_chart_series_t* rssi_ser = lv_chart_add_series(chart, lv_color_hex(ACCENT), LV_CHART_AXIS_PRIMARY_Y);
            lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -120, 0);

            // Fill series with signal history
            lv_chart_set_all_value(chart, rssi_ser, LV_CHART_POINT_NONE);
            for (int i = 0; i < hist_count && i < 64; i++) {
                int rssi_val = sigurdos::mesh::getSignalHistoryRSSI(i);
                lv_chart_set_value_by_id(chart, rssi_ser, i, rssi_val + 120);
            }

            // Add label
            lv_obj_t* ch_label = lv_label_create(scr);
            lv_label_set_text(ch_label, "RSSI History");
            lv_obj_set_style_text_color(ch_label, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(ch_label, emoji_wrapped_montserrat_12, 0);
            lv_obj_align_to(ch_label, chart, LV_ALIGN_OUT_TOP_LEFT, 0, -2);
        }
    }

    show_screen(scr);
}

} // namespace sigurdos::ui
