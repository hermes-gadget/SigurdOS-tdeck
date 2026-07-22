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
#include "../finder_contact_policy.h"
#include "../theme.h"
#include "../responsive.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <new>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ════════════════════════════════════════════════════════
// Finder — nearby nodes with Ping Nearby
// ════════════════════════════════════════════════════════
void finder_screen_show()
{
    lv_obj_t* scr = make_screen_full("Finder");

    bool have_ping = sigurdos::mesh::getPingResultCount() > 0;

    // ── Ping status / button area ──────────────────
    lv_obj_t* ping_row = lv_obj_create(scr);
    lv_obj_set_size(ping_row, CONTENT_W, 24);
    lv_obj_align(ping_row, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 2);
    lv_obj_set_style_bg_opa(ping_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ping_row, 0, 0);
    lv_obj_set_flex_flow(ping_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ping_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (sigurdos::mesh::pingIsActive()) {
        // Ping in progress — show countdown
        uint32_t remain = sigurdos::mesh::activePingRemaining();
        uint32_t elapsed = 3000 - (remain > 0 ? remain : 0);
        char ping_buf[40];
        snprintf(ping_buf, sizeof(ping_buf), "%s Listening... (%lu/%lu)",
                 LV_SYMBOL_AUDIO, (unsigned long)(elapsed / 1000), 3UL);
        lv_obj_t* status = lv_label_create(ping_row);
        lv_label_set_text(status, ping_buf);
        lv_obj_set_style_text_color(status, lv_color_hex(ACCENT), 0);
    } else if (sigurdos::mesh::pingOnCooldown()) {
        // On cooldown — show remaining time
        uint32_t cd = (sigurdos::mesh::pingCooldownRemaining() + 999) / 1000;
        char ping_buf[32];
        snprintf(ping_buf, sizeof(ping_buf), "%s Ping ready in %lus", LV_SYMBOL_WIFI, (unsigned long)cd);
        lv_obj_t* status = lv_label_create(ping_row);
        lv_label_set_text(status, ping_buf);
        lv_obj_set_style_text_color(status, lv_color_hex(TEXT_SECONDARY), 0);
    } else {
        // Ping ready — show button
        lv_obj_t* btn = lv_btn_create(ping_row);
        lv_obj_set_size(btn, 100, 22);
        lv_obj_set_style_bg_color(btn, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "Ping Nearby");
        lv_obj_center(lbl);
        lv_obj_set_style_text_color(lbl, lv_color_hex(BG_PRIMARY), 0);

        // Button click handler
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            sigurdos::mesh::sendPingNearby();
            // Recreate screen to show listening state
            finder_screen_show();
        }, LV_EVENT_CLICKED, nullptr);
    }

    // ── Content list ───────────────────────────────
    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - 44);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 28);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    char buf[80];
    int row_n = 0;

    // ── Ping results ───────────────────────────────
    if (have_ping) {
        int n = sigurdos::mesh::getPingResultCount();
        for (int i = 0; i < n; i++) {
            auto* r = sigurdos::mesh::getPingResult(i);
            if (!r) continue;
            row_n++;
            snprintf(buf, sizeof(buf), "~ %s  %ddBm (unverified)", r->name, r->rssi);
            lv_obj_t* item = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
            lv_obj_set_style_bg_color(item,
                lv_color_hex(row_n % 2 == 1 ? BG_TERTIARY : BG_INPUT), 0);
        }
    }

    // ── Recently heard contacts (fallback) ─────────
    if (!have_ping) {
        sigurdos::mesh::ContactInfo* all_contacts =
            new(std::nothrow) sigurdos::mesh::ContactInfo[MAX_CONTACTS];
        if (all_contacts) {
            int total = sigurdos::mesh::exportContactsFull(all_contacts, MAX_CONTACTS);
            if (total > MAX_CONTACTS) total = MAX_CONTACTS;
            if (total < 0) total = 0;

            sigurdos::mesh::ContactInfo recent[FINDER_RECENT_CONTACT_LIMIT];
            const uint32_t now = sigurdos::mesh::getCurrentTime();
            const int recent_count = finder_select_recent_contacts(
                all_contacts, total, recent, FINDER_RECENT_CONTACT_LIMIT, now);
            delete[] all_contacts;

            for (int i = 0; i < recent_count; i++) {
                row_n++;
                const uint32_t age = finder_contact_age(now, recent[i].last_seen);
                snprintf(buf, sizeof(buf), "%s  %lus ago  %ddBm",
                         recent[i].name, (unsigned long)age, recent[i].rssi);
                lv_obj_t* item = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
                lv_obj_set_style_bg_color(item,
                    lv_color_hex(row_n % 2 == 1 ? BG_TERTIARY : BG_INPUT), 0);
            }
        }
    }

    // ── Empty state ────────────────────────────────
    bool show_empty = (row_n == 0);
    if (show_empty) {
        // Choose message based on state
        const char* msg;
        if (sigurdos::mesh::pingIsActive()) {
            msg = "Listening for nearby nodes...";
        } else if (have_ping) {
            msg = "Ping complete — no nodes responded.\n\n"
                  "Try again later or check the\nRepeaters screen for infrastructure\nrelay nodes.";
        } else {
            msg = "No nodes found nearby.\n\n"
                  "Press \"Ping Nearby\" to discover\n"
                  "repeaters and other nodes on\nyour local mesh.";
        }
        lv_obj_t* empty = lv_label_create(scr);
        lv_label_set_text(empty, msg);
        lv_obj_set_width(empty, CONTENT_W);
        lv_obj_set_style_pad_left(empty, 8, 0);
        lv_obj_set_style_pad_right(empty, 8, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(empty, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
    }

    // ── Footer ─────────────────────────────────────
    if (row_n > 0) {
        snprintf(buf, sizeof(buf), "%s %d node%s", LV_SYMBOL_OK,
                 row_n, row_n == 1 ? "" : "s");
        lv_obj_t* foot = lv_label_create(scr);
        lv_obj_set_width(foot, CONTENT_W);
        lv_obj_set_style_pad_left(foot, 8, 0);
        lv_obj_set_style_text_align(foot, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(foot, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(foot, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_label_set_text(foot, buf);
    }

    show_screen(scr);
}

} // namespace sigurdos::ui
