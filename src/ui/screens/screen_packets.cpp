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
#include "../screen_lifetime.h"
#include "../list_window.h"
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
static constexpr int PKT_VISIBLE_ROWS = PACKET_LIST_RENDER_CAPACITY;

struct PacketRow {
    lv_obj_t* row = nullptr;
    lv_obj_t* time = nullptr;
    lv_obj_t* source = nullptr;
    lv_obj_t* rssi = nullptr;
    lv_obj_t* snr = nullptr;
    lv_obj_t* type = nullptr;
};

// ── Packets screen live-update state ──────────────────────
static lv_obj_t* g_packets_list = nullptr;
static lv_obj_t* g_packets_empty = nullptr;
static lv_obj_t* g_packets_page_label = nullptr;
static lv_obj_t* g_packets_newer_btn = nullptr;
static lv_obj_t* g_packets_older_btn = nullptr;
static PacketRow g_packet_rows[PKT_VISIBLE_ROWS];
static lv_timer_t* g_packets_timer = nullptr;
static uint32_t g_packets_last_generation = UINT32_MAX;
static int g_packets_page = 0;
static ScreenLifetime g_packets_lifetime;

static uint32_t packet_type_color(const char* type)
{
    if (!type) return TEXT_SECONDARY;
    if (strcmp(type, "ADVERT") == 0 || strcmp(type, "ADVERT_RX") == 0) return ACCENT;
    if (strcmp(type, "DM_RX") == 0 || strcmp(type, "DM") == 0) return ACCENT_GREEN;
    if (strcmp(type, "GRP_RX") == 0 || strcmp(type, "CHANNEL") == 0) return ACCENT_ORANGE;
    return TEXT_SECONDARY;
}

static void packets_bind_row(PacketRow& row, int logical_index,
                             const sigurdos::mesh::PacketLogEntry& entry)
{
    lv_obj_clear_flag(row.row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(row.row,
        lv_color_hex(logical_index % 2 == 0 ? BG_TERTIARY : BG_PRIMARY), 0);

    char text[20];
    if (entry.timestamp > 0) {
        const uint32_t sec = entry.timestamp % 86400;
        snprintf(text, sizeof(text), "%02d:%02d", (sec / 3600) % 24, (sec / 60) % 60);
    } else {
        snprintf(text, sizeof(text), "--:--");
    }
    lv_label_set_text(row.time, text);
    lv_label_set_text(row.source, entry.source);

    snprintf(text, sizeof(text), "%d", entry.rssi);
    lv_label_set_text(row.rssi, text);
    lv_obj_set_style_text_color(row.rssi,
        entry.rssi > -85  ? lv_color_hex(ACCENT_GREEN) :
        entry.rssi > -100 ? lv_color_hex(ACCENT_ORANGE) :
                            lv_color_hex(ACCENT_RED), 0);

    snprintf(text, sizeof(text), "%.1f", entry.snr);
    lv_label_set_text(row.snr, text);
    lv_label_set_text(row.type, entry.type);
    lv_obj_set_style_text_color(row.type,
        lv_color_hex(packet_type_color(entry.type)), 0);
}

static PacketRow packets_create_row(lv_obj_t* parent)
{
    PacketRow result;
    result.row = lv_obj_create(parent);
    lv_obj_set_size(result.row, LV_PCT(100), PKT_ROW_H);
    lv_obj_set_style_bg_opa(result.row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(result.row, 0, 0);
    lv_obj_set_style_pad_all(result.row, 0, 0);
    lv_obj_set_scrollbar_mode(result.row, LV_SCROLLBAR_MODE_OFF);

    result.time = lv_label_create(result.row);
    lv_obj_set_style_text_color(result.time, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(result.time, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(result.time, LV_ALIGN_LEFT_MID, PKT_COL_TIME, 0);

    result.source = lv_label_create(result.row);
    lv_obj_set_style_text_color(result.source, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(result.source, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_width(result.source, 100);
    lv_label_set_long_mode(result.source, LV_LABEL_LONG_DOT);
    lv_obj_align(result.source, LV_ALIGN_LEFT_MID, PKT_COL_SOURCE, 0);

    result.rssi = lv_label_create(result.row);
    lv_obj_set_style_text_font(result.rssi, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(result.rssi, LV_ALIGN_LEFT_MID, PKT_COL_RSSI, 0);

    result.snr = lv_label_create(result.row);
    lv_obj_set_style_text_color(result.snr, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(result.snr, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(result.snr, LV_ALIGN_LEFT_MID, PKT_COL_SNR, 0);

    result.type = lv_label_create(result.row);
    lv_obj_set_style_text_font(result.type, emoji_wrapped_montserrat_10, 0);
    lv_label_set_long_mode(result.type, LV_LABEL_LONG_DOT);
    lv_obj_set_width(result.type, DISPLAY_W - PKT_COL_TYPE - PKT_COL_MARGIN);
    lv_obj_align(result.type, LV_ALIGN_LEFT_MID, PKT_COL_TYPE, 0);
    return result;
}

static void packets_rebuild_list()
{
    if (!g_packets_list) return;

    const uint32_t generation = sigurdos::mesh::getPacketLogGeneration();
    if (generation == g_packets_last_generation) return;
    g_packets_last_generation = generation;
    const int n = sigurdos::mesh::getPacketLogCount();

    if (n == 0) {
        lv_obj_clear_flag(g_packets_empty, LV_OBJ_FLAG_HIDDEN);
        for (auto& row : g_packet_rows) lv_obj_add_flag(row.row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_packets_page_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(g_packets_newer_btn, LV_STATE_DISABLED);
        lv_obj_add_state(g_packets_older_btn, LV_STATE_DISABLED);
        return;
    }
    lv_obj_add_flag(g_packets_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_packets_page_label, LV_OBJ_FLAG_HIDDEN);

    g_packets_page = list_clamp_page(g_packets_page, n, PKT_VISIBLE_ROWS);
    const ListWindow window = list_window_from_newest(n, PKT_VISIBLE_ROWS, g_packets_page);
    int slot = 0;
    for (int i = window.end - 1; i >= window.start; --i) {
        sigurdos::mesh::PacketLogEntry e;
        if (!sigurdos::mesh::getPacketLogEntry(i, &e)) continue;
        packets_bind_row(g_packet_rows[slot++], i, e);
    }
    while (slot < PKT_VISIBLE_ROWS) {
        lv_obj_add_flag(g_packet_rows[slot++].row, LV_OBJ_FLAG_HIDDEN);
    }

    char page_text[32];
    snprintf(page_text, sizeof(page_text), "%d-%d/%d",
             n - window.end + 1, n - window.start, n);
    lv_label_set_text(g_packets_page_label, page_text);
    if (g_packets_page == 0) lv_obj_add_state(g_packets_newer_btn, LV_STATE_DISABLED);
    else lv_obj_clear_state(g_packets_newer_btn, LV_STATE_DISABLED);
    if (g_packets_page >= list_page_count(n, PKT_VISIBLE_ROWS) - 1)
        lv_obj_add_state(g_packets_older_btn, LV_STATE_DISABLED);
    else
        lv_obj_clear_state(g_packets_older_btn, LV_STATE_DISABLED);
}

static void packets_timer_cb(lv_timer_t*) { packets_rebuild_list(); }

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
    lv_obj_set_scroll_dir(list, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    g_packets_list = list;
    g_packets_empty = lv_label_create(list);
    lv_label_set_text(g_packets_empty, "No packets yet. Waiting for mesh traffic...");
    lv_obj_set_width(g_packets_empty, LV_PCT(100));
    lv_obj_set_style_text_align(g_packets_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_packets_empty, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(g_packets_empty, emoji_wrapped_montserrat_10, 0);

    for (auto& row : g_packet_rows) row = packets_create_row(list);

    lv_obj_t* pager = lv_obj_create(list);
    lv_obj_set_size(pager, LV_PCT(100), 24);
    lv_obj_set_style_bg_color(pager, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(pager, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pager, 0, 0);
    lv_obj_set_style_pad_all(pager, 0, 0);

    g_packets_newer_btn = lv_btn_create(pager);
    lv_obj_set_size(g_packets_newer_btn, 52, 20);
    lv_obj_align(g_packets_newer_btn, LV_ALIGN_LEFT_MID, 2, 0);
    apply_pixel_btn_outline(g_packets_newer_btn);
    lv_obj_t* newer_label = lv_label_create(g_packets_newer_btn);
    lv_label_set_text(newer_label, "Newer");
    lv_obj_center(newer_label);
    lv_obj_add_event_cb(g_packets_newer_btn, [](lv_event_t*) {
        if (g_packets_page > 0) g_packets_page--;
        g_packets_last_generation = UINT32_MAX;
        packets_rebuild_list();
    }, LV_EVENT_CLICKED, nullptr);

    g_packets_page_label = lv_label_create(pager);
    lv_obj_set_style_text_color(g_packets_page_label, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(g_packets_page_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(g_packets_page_label, LV_ALIGN_CENTER, 0, 0);

    g_packets_older_btn = lv_btn_create(pager);
    lv_obj_set_size(g_packets_older_btn, 52, 20);
    lv_obj_align(g_packets_older_btn, LV_ALIGN_RIGHT_MID, -2, 0);
    apply_pixel_btn_outline(g_packets_older_btn);
    lv_obj_t* older_label = lv_label_create(g_packets_older_btn);
    lv_label_set_text(older_label, "Older");
    lv_obj_center(older_label);
    lv_obj_add_event_cb(g_packets_older_btn, [](lv_event_t*) {
        g_packets_page++;
        g_packets_last_generation = UINT32_MAX;
        packets_rebuild_list();
    }, LV_EVENT_CLICKED, nullptr);

    g_packets_page = 0;
    g_packets_last_generation = UINT32_MAX;
    packets_rebuild_list();

    g_packets_lifetime.bind(scr);
    g_packets_lifetime.track(&g_packets_list);
    g_packets_lifetime.trackTimer(&g_packets_timer);
    g_packets_lifetime.onDelete([] {
        g_packets_last_generation = UINT32_MAX;
        g_packets_empty = nullptr;
        g_packets_page_label = nullptr;
        g_packets_newer_btn = nullptr;
        g_packets_older_btn = nullptr;
        for (auto& row : g_packet_rows) row = {};
    });
    g_packets_timer = lv_timer_create(packets_timer_cb, 1000, nullptr);

    show_screen(scr);
}

} // namespace sigurdos::ui
