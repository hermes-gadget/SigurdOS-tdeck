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
#include "../../hal/sdcard.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <SPIFFS.h>
#include <cstdio>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ════════════════════════════════════════════════════════
// Node Stats — diagnostics panel
// ════════════════════════════════════════════════════════
void node_stats_screen_show()
{
    lv_obj_t* scr = make_screen_full("Node Stats");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    char buf[128];
    int row = 0;

    // ── Sent ──
    snprintf(buf, sizeof(buf), "  Sent flood:   %u", sigurdos::mesh::getNumSentFlood());
    lv_obj_t* r0 = lv_list_add_btn(list, LV_SYMBOL_UPLOAD, buf);
    lv_obj_set_style_bg_color(r0, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r0, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r0, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r0, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_pad_top(r0, 4, 0);
    lv_obj_set_style_pad_bottom(r0, 4, 0);
    row++;

    snprintf(buf, sizeof(buf), "  Sent direct:  %u", sigurdos::mesh::getNumSentDirect());
    lv_obj_t* r1 = lv_list_add_btn(list, LV_SYMBOL_UPLOAD, buf);
    lv_obj_set_style_bg_color(r1, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r1, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r1, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r1, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_pad_top(r1, 4, 0);
    lv_obj_set_style_pad_bottom(r1, 4, 0);
    row++;

    // ── Received ──
    snprintf(buf, sizeof(buf), "  Recv flood:   %u", sigurdos::mesh::getNumRecvFlood());
    lv_obj_t* r2 = lv_list_add_btn(list, LV_SYMBOL_DOWNLOAD, buf);
    lv_obj_set_style_bg_color(r2, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r2, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r2, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r2, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_pad_top(r2, 4, 0);
    lv_obj_set_style_pad_bottom(r2, 4, 0);
    row++;

    snprintf(buf, sizeof(buf), "  Recv direct:  %u", sigurdos::mesh::getNumRecvDirect());
    lv_obj_t* r3 = lv_list_add_btn(list, LV_SYMBOL_DOWNLOAD, buf);
    lv_obj_set_style_bg_color(r3, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r3, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r3, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r3, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_pad_top(r3, 4, 0);
    lv_obj_set_style_pad_bottom(r3, 4, 0);
    row++;

    // ── Airtime ──
    unsigned long tx_air = sigurdos::mesh::getTotalTxAirtimeMs();
    unsigned long rx_air = sigurdos::mesh::getTotalRxAirtimeMs();
    snprintf(buf, sizeof(buf), "  TX airtime:   %lu ms", tx_air);
    lv_obj_t* r4 = lv_list_add_btn(list, LV_SYMBOL_CHARGE, buf);
    lv_obj_set_style_bg_color(r4, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r4, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r4, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r4, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_pad_top(r4, 4, 0);
    lv_obj_set_style_pad_bottom(r4, 4, 0);
    row++;

    snprintf(buf, sizeof(buf), "  RX airtime:   %lu ms", rx_air);
    lv_obj_t* r5 = lv_list_add_btn(list, LV_SYMBOL_CHARGE, buf);
    lv_obj_set_style_bg_color(r5, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r5, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r5, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r5, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_pad_top(r5, 4, 0);
    lv_obj_set_style_pad_bottom(r5, 4, 0);
    row++;

    // ── Budget & ACKs ──
    snprintf(buf, sizeof(buf), "  TX budget:    %lu ms", sigurdos::mesh::getRemainingTxBudget());
    lv_obj_t* r6 = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
    lv_obj_set_style_bg_color(r6, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r6, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r6, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r6, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_pad_top(r6, 4, 0);
    lv_obj_set_style_pad_bottom(r6, 4, 0);
    row++;

    snprintf(buf, sizeof(buf), "  ACKs received: %d", sigurdos::mesh::getAckCounter());
    lv_obj_t* r7 = lv_list_add_btn(list, LV_SYMBOL_OK, buf);
    lv_obj_set_style_bg_color(r7, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r7, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r7, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r7, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_pad_top(r7, 4, 0);
    lv_obj_set_style_pad_bottom(r7, 4, 0);
    row++;

    // ── Storage ──
    {
        // SPIFFS
        size_t spiffs_total = SPIFFS.totalBytes();
        size_t spiffs_used  = SPIFFS.usedBytes();
        snprintf(buf, sizeof(buf), "  SPIFFS:       %u/%u KB", (unsigned)(spiffs_used / 1024), (unsigned)(spiffs_total / 1024));
        lv_obj_t* rs0 = lv_list_add_btn(list, LV_SYMBOL_SD_CARD, buf);
        lv_obj_set_style_bg_color(rs0, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(rs0, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(rs0, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(rs0, emoji_wrapped_montserrat_12, 0);
        lv_obj_set_style_pad_top(rs0, 4, 0);
        lv_obj_set_style_pad_bottom(rs0, 4, 0);
        row++;

        // SD Card
        if (sigurdos_sdcard_mounted()) {
            uint64_t sd_total = sigurdos_sdcard_capacity_bytes();
            uint64_t sd_free  = sigurdos_sdcard_free_bytes();
            char total_str[16], free_str[16];
            sigurdos_sdcard_format_size(sd_total, total_str, sizeof(total_str));
            sigurdos_sdcard_format_size(sd_free, free_str, sizeof(free_str));
            snprintf(buf, sizeof(buf), "  SD Card:      %s free / %s", free_str, total_str);
            lv_obj_t* rs1 = lv_list_add_btn(list, LV_SYMBOL_SD_CARD, buf);
            lv_obj_set_style_bg_color(rs1, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(rs1, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(rs1, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(rs1, emoji_wrapped_montserrat_12, 0);
            lv_obj_set_style_pad_top(rs1, 4, 0);
            lv_obj_set_style_pad_bottom(rs1, 4, 0);
            row++;
        }
    }

    // ── Section spacer ──
    row++;

    // Reset stats button
    lv_obj_t* btn_reset = lv_list_add_btn(list, LV_SYMBOL_TRASH, "  Reset stats");
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x4a2020), 0);
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_reset, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_reset, [](lv_event_t* e) {
        lv_obj_t* ns_scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        auto dlg_sz = dialog_size(240, 100);
        lv_obj_t* dlg = lv_obj_create(ns_scr);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_border_width(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, "Reset packet stats?");
        lv_obj_set_style_text_color(title, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* msg = lv_label_create(dlg);
        lv_label_set_text(msg, "All counters will be zeroed.");
        lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, -4);

        // Cancel
        lv_obj_t* cancel_btn = lv_btn_create(dlg);
        lv_obj_set_size(cancel_btn, 64, 24);
        lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -4);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_radius(cancel_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(cancel_btn);
        lv_label_set_text(cl, "Cancel");
        lv_obj_center(cl);
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ev) {
            lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
        }, LV_EVENT_CLICKED, nullptr);

        // Confirm
        lv_obj_t* confirm_btn = lv_btn_create(dlg);
        lv_obj_set_size(confirm_btn, 64, 24);
        lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
        lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(confirm_btn, 0, 0);
        lv_obj_t* cfl = lv_label_create(confirm_btn);
        lv_label_set_text(cfl, "Reset");
        lv_obj_center(cfl);
        lv_obj_add_event_cb(confirm_btn, [](lv_event_t* e_confirm) {
            sigurdos::mesh::resetPacketStats();
            lv_obj_del_async(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e_confirm)));
            navigate_to(Screen::NodeStats); // refresh
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    show_screen(scr);
}

} // namespace sigurdos::ui
