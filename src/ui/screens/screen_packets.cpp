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
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstring>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ── Packets screen live-update state ──────────────────────
static lv_obj_t*  g_packets_list       = nullptr;
static lv_timer_t* g_packets_timer     = nullptr;
static int        g_packets_last_count = -1;

// ════════════════════════════════════════════════════════
// Packets — raw packet log (last N transmissions)
// ════════════════════════════════════════════════════════

static constexpr int PKT_HEADER_H = 16;
static constexpr int PKT_ROW_H    = 22;
static constexpr int PKT_COL_MARGIN = 6;
static constexpr int PKT_COL_TIME   = PKT_COL_MARGIN;
static constexpr int PKT_COL_SOURCE = PKT_COL_TIME   + 52;
static constexpr int PKT_COL_RSSI   = PKT_COL_SOURCE + 106;
static constexpr int PKT_COL_SNR    = PKT_COL_RSSI   + 48;
static constexpr int PKT_COL_TYPE   = PKT_COL_SNR    + 44;

static void packets_rebuild_list()
{
    if (!g_packets_list) return;

    int n = sigurdos::mesh::getPacketLogCount();
    if (n == g_packets_last_count) return;
    g_packets_last_count = n;

    lv_obj_clean(g_packets_list);

    if (n == 0) {
        lv_obj_t* empty = lv_label_create(g_packets_list);
        lv_label_set_text(empty, "No packets yet.\nWaiting for mesh traffic...");
        lv_obj_set_style_text_color(empty, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(empty, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 8, 8);
        return;
    }

    for (int i = n - 1; i >= 0; i--) {
        sigurdos::mesh::PacketLogEntry e;
        if (!sigurdos::mesh::getPacketLogEntry(i, &e)) continue;

        lv_obj_t* row = lv_obj_create(g_packets_list);
        lv_obj_set_size(row, LV_PCT(100), PKT_ROW_H);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_PRIMARY), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

        // Time
        char tbuf[8];
        if (e.timestamp > 0) {
            uint32_t sec = e.timestamp % 86400;
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", (sec/3600)%24, (sec/60)%60);
        } else {
            snprintf(tbuf, sizeof(tbuf), "--:--");
        }
        lv_obj_t* time_l = lv_label_create(row);
        lv_label_set_text(time_l, tbuf);
        lv_obj_set_style_text_color(time_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(time_l, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(time_l, LV_ALIGN_LEFT_MID, PKT_COL_TIME, 0);

        // Source
        char src_buf[20];
        strncpy(src_buf, e.source, sizeof(src_buf) - 1);
        src_buf[sizeof(src_buf) - 1] = '\0';
        lv_obj_t* src_l = lv_label_create(row);
        lv_label_set_text(src_l, src_buf);
        lv_obj_set_style_text_color(src_l, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(src_l, emoji_wrapped_montserrat_10, 0);
        lv_obj_set_width(src_l, 100);
        lv_label_set_long_mode(src_l, LV_LABEL_LONG_DOT);
        lv_obj_align(src_l, LV_ALIGN_LEFT_MID, PKT_COL_SOURCE, 0);

        // RSSI
        char rssi_buf[10];
        snprintf(rssi_buf, sizeof(rssi_buf), "%d", e.rssi);
        lv_obj_t* rssi_l = lv_label_create(row);
        lv_label_set_text(rssi_l, rssi_buf);
        lv_obj_set_style_text_color(rssi_l,
            e.rssi > -85  ? lv_color_hex(ACCENT_GREEN)  :
            e.rssi > -100 ? lv_color_hex(ACCENT_ORANGE) :
                            lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_text_font(rssi_l, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(rssi_l, LV_ALIGN_LEFT_MID, PKT_COL_RSSI, 0);

        // SNR
        char snr_buf[10];
        snprintf(snr_buf, sizeof(snr_buf), "%.1f", e.snr);
        lv_obj_t* snr_l = lv_label_create(row);
        lv_label_set_text(snr_l, snr_buf);
        lv_obj_set_style_text_color(snr_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(snr_l, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(snr_l, LV_ALIGN_LEFT_MID, PKT_COL_SNR, 0);

        // Type
        uint32_t type_color;
        if      (strcmp(e.type, "ADVERT")    == 0) type_color = ACCENT;
        else if (strcmp(e.type, "ADVERT_RX") == 0) type_color = ACCENT;
        else if (strcmp(e.type, "DM_RX")     == 0) type_color = ACCENT_GREEN;
        else if (strcmp(e.type, "DM")        == 0) type_color = ACCENT_GREEN;
        else if (strcmp(e.type, "GRP_RX")    == 0) type_color = ACCENT_ORANGE;
        else if (strcmp(e.type, "CHANNEL")   == 0) type_color = ACCENT_ORANGE;
        else if (strcmp(e.type, "ANON_RX")   == 0) type_color = TEXT_SECONDARY;
        else if (strcmp(e.type, "ANON")      == 0) type_color = TEXT_SECONDARY;
        else                                        type_color = TEXT_SECONDARY;
        lv_obj_t* type_l = lv_label_create(row);
        lv_label_set_text(type_l, e.type);
        lv_obj_set_style_text_color(type_l, lv_color_hex(type_color), 0);
        lv_obj_set_style_text_font(type_l, emoji_wrapped_montserrat_10, 0);
        lv_label_set_long_mode(type_l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(type_l, DISPLAY_W - PKT_COL_TYPE - PKT_COL_MARGIN);
        lv_obj_align(type_l, LV_ALIGN_LEFT_MID, PKT_COL_TYPE, 0);
    }
}

static void packets_timer_cb(lv_timer_t*) { packets_rebuild_list(); }

static void packets_screen_delete_cb(lv_event_t*)
{
    g_packets_list = nullptr;
    g_packets_last_count = -1;
    if (g_packets_timer) {
        lv_timer_del(g_packets_timer);
        g_packets_timer = nullptr;
    }
}

void heard_screen_show()
{
    lv_obj_t* scr = make_screen_full("Packets");

    int list_y = CONTENT_Y + PKT_HEADER_H + DIVIDER_H;
    int list_h = DISPLAY_H - list_y - DIVIDER_H - BOT_BAR_H;

    // Column headers
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, LV_PCT(100), PKT_HEADER_H);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);

    const char* col_labels[] = {"TIME", "SOURCE", "RSSI", "SNR", "TYPE"};
    int col_x[] = {PKT_COL_TIME, PKT_COL_SOURCE, PKT_COL_RSSI, PKT_COL_SNR, PKT_COL_TYPE};
    for (int i = 0; i < 5; i++) {
        lv_obj_t* cl = lv_label_create(hdr);
        lv_label_set_text(cl, col_labels[i]);
        lv_obj_set_style_text_color(cl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(cl, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(cl, LV_ALIGN_LEFT_MID, col_x[i], 0);
    }

    // Divider below header
    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, CONTENT_Y + PKT_HEADER_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // Scrollable list area
    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), list_h);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, list_y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    g_packets_list = list;
    g_packets_last_count = -1;
    packets_rebuild_list();

    lv_obj_add_event_cb(scr, packets_screen_delete_cb, LV_EVENT_DELETE, nullptr);
    g_packets_timer = lv_timer_create(packets_timer_cb, 1000, nullptr);

    show_screen(scr);
}

} // namespace sigurdos::ui
