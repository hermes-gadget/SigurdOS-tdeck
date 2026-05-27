// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SlopOS-TDeck.
//
// SlopOS-TDeck is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SlopOS-TDeck is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SlopOS-TDeck.  If not, see <https://www.gnu.org/licenses/>.


#include "screens.h"
#include "navigation.h"
#include "theme.h"
#include "responsive.h"
#include "home_screen.h"
#include "chat_screen.h"
#include "../hal/tdeck_pins.h"
#include "../hal/battery.h"
#include "../hal/sdcard.h"
#include "../hal/gps.h"
#include "../hal/prefs.h"
#include "../hal/display.h"
#include "../hal/keyboard.h"
#include "../hal/display.h"
#include "../mesh/mesh_wrapper.h"
#include "../app/map_renderer.h"
#include "../fonts/emoji_font.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>
#include <cstring>

namespace slopos::ui {

using namespace theme;

// ── Layout constants (from responsive.h) ──────────────────
using namespace responsive;
// TOP_BAR_H, BOT_BAR_H, DIVIDER_H, CONTENT_Y, CONTENT_H — all from responsive.h

static lv_obj_t* g_date_row = nullptr;   // for live update after setting time
static lv_obj_t* g_time_row = nullptr;
static lv_obj_t* g_advert_status_label = nullptr;
static lv_obj_t* g_advert_button = nullptr;
static lv_timer_t* g_advert_status_timer = nullptr;
static constexpr uint32_t ADVERT_COOLDOWN_SECONDS = 10;

// Back button reference for back-swipe visual feedback
static lv_obj_t* s_back_btn = nullptr;

// ── Packets screen live-update state ──────────────────────
static lv_obj_t*  g_packets_list       = nullptr;
static lv_timer_t* g_packets_timer     = nullptr;
static int        g_packets_last_count = -1;

static void advertise_status_update()
{
    if (!g_advert_status_label) return;

    uint32_t last = slopos::mesh::getLastAdvertTime();
    uint32_t now = slopos::mesh::getCurrentTime();
    bool success = slopos::mesh::getLastAdvertSuccess();
    bool used_gps = slopos::mesh::getLastAdvertUsedGps();

    char age[24];
    bool on_cooldown = false;
    uint32_t cooldown_left = 0;
    if (last == 0 || now < last) {
        snprintf(age, sizeof(age), "unknown");
        on_cooldown = false;
    } else {
        uint32_t delta = now - last;
        if (delta < 60)         snprintf(age, sizeof(age), "just now");
        else if (delta < 3600)  snprintf(age, sizeof(age), "%lumin ago", (unsigned long)(delta / 60));
        else if (delta < 86400) snprintf(age, sizeof(age), "%luh ago", (unsigned long)(delta / 3600));
        else                   snprintf(age, sizeof(age), "%lud ago", (unsigned long)(delta / 86400));

        if (delta < ADVERT_COOLDOWN_SECONDS) {
            on_cooldown = true;
            cooldown_left = ADVERT_COOLDOWN_SECONDS - delta;
        }
    }

    if (g_advert_button) {
        if (on_cooldown) {
            lv_obj_add_state(g_advert_button, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(g_advert_button, LV_STATE_DISABLED);
        }
    }

    char text[128];
    if (last == 0) {
        snprintf(text, sizeof(text), "Status: never sent yet");
    } else if (success) {
        if (on_cooldown) {
            snprintf(text, sizeof(text),
                "Status: last broadcast %s (%s), cool-down %lus",
                age, used_gps ? "with GPS" : "without GPS", (unsigned long)cooldown_left);
        } else {
            snprintf(text, sizeof(text),
                "Status: last broadcast %s (%s)",
                age, used_gps ? "with GPS" : "without GPS");
        }
    } else {
        snprintf(text, sizeof(text), "Status: last attempt failed (mesh unavailable)");
        if (on_cooldown) {
            snprintf(text, sizeof(text),
                "Status: last attempt failed, cool-down %lus",
                (unsigned long)cooldown_left);
        }
    }

    lv_label_set_text(g_advert_status_label, text);
}

static void advertise_status_timer_cb(lv_timer_t*)
{
    advertise_status_update();
}

static void advertise_screen_delete_cb(lv_event_t*)
{
    g_advert_button = nullptr;
    if (g_advert_status_timer) {
        lv_timer_del(g_advert_status_timer);
        g_advert_status_timer = nullptr;
    }
    g_advert_status_label = nullptr;
}

// ════════════════════════════════════════════════════════
// make_screen_full — builds consistent top+bottom bars
// ════════════════════════════════════════════════════════
static lv_obj_t* make_screen_full(const char* title)
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    apply_dark_bg(scr);

    // ── Top bar ──────────────────────────────────────────
    lv_obj_t* top = lv_obj_create(scr);
    lv_obj_set_size(top, LV_PCT(100), TOP_BAR_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);

    lv_obj_t* back = lv_btn_create(top);
    lv_obj_set_size(back, 24, TOP_BAR_H - 4);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
    apply_topbar_icon_btn(back);
    s_back_btn = back; // store for back-swipe highlight
    if (can_go_back()) {
        lv_obj_add_event_cb(back, [](lv_event_t*) { go_back(); }, LV_EVENT_CLICKED, nullptr);
    }

    lv_obj_t* back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon,
        lv_color_hex(can_go_back() ? ACCENT : TEXT_MUTED), 0);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_12, 0);
    lv_obj_center(back_icon);

    // Dynamic channel hashtags (snapshot at screen creation)
    char ch_buf[100] = "";
    {
        char names[8][32];
        int n = slopos::mesh::exportChannels(names, 8);
        int pos = 0;
        for (int i = 0; i < n && pos < 90; i++) {
            const char* nm = names[i];
            pos += snprintf(ch_buf + pos, sizeof(ch_buf) - pos,
                            "%s%s*  ", nm[0] == '#' ? "" : "#", nm);
        }
        while (pos > 0 && ch_buf[pos-1] == ' ') ch_buf[--pos] = '\0';
        if (ch_buf[0] == '\0') snprintf(ch_buf, sizeof(ch_buf), "no channels");
    }
    lv_obj_t* ch_lbl = lv_label_create(top);
    lv_label_set_text(ch_lbl, ch_buf);
    lv_label_set_long_mode(ch_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ch_lbl, DISPLAY_W - 78);
    lv_obj_set_style_text_color(ch_lbl, lv_color_hex(CHANNEL_HASH), 0);
    lv_obj_set_style_text_font(ch_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(ch_lbl, LV_ALIGN_LEFT_MID, 32, 0);

    // Time (24h snapshot)
    {
        uint32_t epoch = slopos::mesh::getCurrentTime();
        char t[8];
        if (epoch == 0) {
            snprintf(t, sizeof(t), "--:--");
        } else {
            uint32_t sec = epoch % 86400;
            snprintf(t, sizeof(t), "%02d:%02d", (sec/3600)%24, (sec/60)%60);
        }
        lv_obj_t* tl = lv_label_create(top);
        lv_label_set_text(tl, t);
        lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_12, 0);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Screen title (small, centered — sits behind channel text but visible when no channels)
    if (title && title[0]) {
        lv_obj_t* ttl = lv_label_create(top);
        lv_label_set_text(ttl, title);
        lv_obj_set_style_text_color(ttl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(ttl, &lv_font_montserrat_10, 0);
        lv_obj_align(ttl, LV_ALIGN_CENTER, 0, 0);
    }

    // Top divider
    lv_obj_t* tdiv = lv_obj_create(scr);
    lv_obj_set_size(tdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(tdiv, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);
    lv_obj_set_style_bg_color(tdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(tdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tdiv, 0, 0);

    // ── Bottom bar ───────────────────────────────────────
    lv_obj_t* bot = lv_obj_create(scr);
    lv_obj_set_size(bot, LV_PCT(100), BOT_BAR_H);
    lv_obj_align(bot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(bot, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bot, 0, 0);
    lv_obj_set_style_border_width(bot, 0, 0);

    // Device name (left)
    lv_obj_t* dev = lv_label_create(bot);
    lv_label_set_text(dev, slopos::mesh::getOwnName());
    lv_obj_set_style_text_color(dev, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dev, &lv_font_montserrat_10, 0);
    lv_obj_align(dev, LV_ALIGN_LEFT_MID, 4, 0);

    // Signal bars (center, snapshot)
    {
        int rssi = slopos::mesh::getLastRSSI();
        const char* bars;
        if (rssi > -70)       bars = "▂▄▆█";
        else if (rssi > -85)  bars = "▂▄▆ ";
        else if (rssi > -100) bars = "▂▄  ";
        else if (rssi > -115) bars = "▂   ";
        else                  bars = "    ";
        lv_obj_t* sig = lv_label_create(bot);
        lv_label_set_text(sig, bars);
        lv_obj_set_style_text_color(sig, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(sig, &lv_font_montserrat_10, 0);
        lv_obj_align(sig, LV_ALIGN_CENTER, -20, 0);
    }

    // Battery % (right, snapshot)
    {
        char batt[8];
        int pct = slopos_battery_pct();
        snprintf(batt, sizeof(batt), "%d%%", pct);
        lv_obj_t* bl = lv_label_create(bot);
        lv_label_set_text(bl, batt);
        lv_obj_set_style_text_color(bl,
            lv_color_hex(pct > 20 ? ACCENT : ACCENT_RED), 0);
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_10, 0);
        lv_obj_align(bl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Bottom divider
    lv_obj_t* bdiv = lv_obj_create(scr);
    lv_obj_set_size(bdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(bdiv, LV_ALIGN_TOP_MID, 0, DISPLAY_H - BOT_BAR_H - DIVIDER_H);
    lv_obj_set_style_bg_color(bdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(bdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bdiv, 0, 0);

    return scr;
}

static void show_screen(lv_obj_t* scr)
{
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
}

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

    int n = slopos::mesh::getPacketLogCount();
    if (n == g_packets_last_count) return;
    g_packets_last_count = n;

    lv_obj_clean(g_packets_list);

    if (n == 0) {
        lv_obj_t* empty = lv_label_create(g_packets_list);
        lv_label_set_text(empty, "No packets yet.\nWaiting for mesh traffic...");
        lv_obj_set_style_text_color(empty, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 8, 8);
        return;
    }

    for (int i = n - 1; i >= 0; i--) {
        slopos::mesh::PacketLogEntry e;
        if (!slopos::mesh::getPacketLogEntry(i, &e)) continue;

        lv_obj_t* row = lv_obj_create(g_packets_list);
        lv_obj_set_size(row, LV_PCT(100), PKT_ROW_H);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_PRIMARY), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);

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
        lv_obj_set_style_text_font(time_l, &lv_font_montserrat_10, 0);
        lv_obj_align(time_l, LV_ALIGN_LEFT_MID, PKT_COL_TIME, 0);

        // Source
        char src_buf[20];
        strncpy(src_buf, e.source, sizeof(src_buf) - 1);
        src_buf[sizeof(src_buf) - 1] = '\0';
        lv_obj_t* src_l = lv_label_create(row);
        lv_label_set_text(src_l, src_buf);
        lv_obj_set_style_text_color(src_l, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(src_l, &lv_font_montserrat_10, 0);
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
        lv_obj_set_style_text_font(rssi_l, &lv_font_montserrat_10, 0);
        lv_obj_align(rssi_l, LV_ALIGN_LEFT_MID, PKT_COL_RSSI, 0);

        // SNR
        char snr_buf[10];
        snprintf(snr_buf, sizeof(snr_buf), "%.1f", e.snr);
        lv_obj_t* snr_l = lv_label_create(row);
        lv_label_set_text(snr_l, snr_buf);
        lv_obj_set_style_text_color(snr_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(snr_l, &lv_font_montserrat_10, 0);
        lv_obj_align(snr_l, LV_ALIGN_LEFT_MID, PKT_COL_SNR, 0);

        // Type
        uint32_t type_color;
        if      (strcmp(e.type, "ADVERT")    == 0) type_color = ACCENT;
        else if (strcmp(e.type, "ADVERT_RX") == 0) type_color = ACCENT;
        else if (strcmp(e.type, "DM_RX")     == 0) type_color = ACCENT_GREEN;
        else if (strcmp(e.type, "DM")        == 0) type_color = ACCENT_GREEN;
        else if (strcmp(e.type, "GRP_RX")    == 0) type_color = ACCENT_ORANGE;
        else if (strcmp(e.type, "CHANNEL")   == 0) type_color = ACCENT_ORANGE;
        else if (strcmp(e.type, "ANON_RX")   == 0) type_color = TEXT_MUTED;
        else if (strcmp(e.type, "ANON")      == 0) type_color = TEXT_MUTED;
        else                                        type_color = TEXT_SECONDARY;
        lv_obj_t* type_l = lv_label_create(row);
        lv_label_set_text(type_l, e.type);
        lv_obj_set_style_text_color(type_l, lv_color_hex(type_color), 0);
        lv_obj_set_style_text_font(type_l, &lv_font_montserrat_10, 0);
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
        lv_obj_set_style_text_color(cl, lv_color_hex(TEXT_MUTED), 0);
        lv_obj_set_style_text_font(cl, &lv_font_montserrat_10, 0);
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

// ════════════════════════════════════════════════════════
// Contacts — tap-to-message directory
// ════════════════════════════════════════════════════════
void contacts_screen_show()
{
    lv_obj_t* scr = make_screen_full("Contacts");

    slopos::mesh::ContactInfo all_contacts[32];
    int total = slopos::mesh::exportContactsFull(all_contacts, 32);

    // Filter to companions (CHAT) and room servers (ROOM)
    int n = 0;
    for (int i = 0; i < total; i++) {
        if (all_contacts[i].type == ADV_TYPE_CHAT ||
            all_contacts[i].type == ADV_TYPE_ROOM) {
            if (n < i) all_contacts[n] = all_contacts[i];
            n++;
        }
    }
    slopos::mesh::ContactInfo* contacts = all_contacts;

    // Sort alphabetically
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(contacts[j].name, contacts[i].name) < 0) {
                auto tmp = contacts[i]; contacts[i] = contacts[j]; contacts[j] = tmp;
            }

    if (n == 0) {
        lv_obj_t* info = lv_label_create(scr);
        lv_label_set_text(info,
            "No contacts yet.\n\n"
            "Companions (chat nodes) and room servers\n"
            "appear here once they broadcast an advert\n"
            "or send you a message.\n\n"
            "Tap a contact to send a direct message.");
        lv_obj_set_width(info, CONTENT_W);
        lv_obj_set_style_pad_left(info, 8, 0);
        lv_obj_set_style_pad_right(info, 8, 0);
        lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(info, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
        lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);
        show_screen(scr);
        return;
    }

    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    static constexpr int ROW_H = 32;

    for (int i = 0; i < n; i++) {
        auto& c = contacts[i];

        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(ACCENT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        // Icon
        lv_obj_t* icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_CALL);
        lv_obj_set_style_text_color(icon, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_12, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 6, 0);

        // Name
        lv_obj_t* name_l = lv_label_create(row);
        lv_label_set_text(name_l, c.name);
        lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(name_l, &lv_font_montserrat_12, 0);
        lv_obj_align(name_l, LV_ALIGN_LEFT_MID, 28, 0);

        // Store a heap copy of the contact name as user_data so the event
        // handler can find it without depending on widget child index order.
        // Freed on LV_EVENT_DELETE to avoid leaking when the screen is
        // discarded or the row is recycled.
        char* name_dup = strdup(c.name);
        lv_obj_set_user_data(row, name_dup);

        // RSSI
        char rssi_buf[12];
        snprintf(rssi_buf, sizeof(rssi_buf), "%ddBm", c.rssi);
        lv_obj_t* rssi_l = lv_label_create(row);
        lv_label_set_text(rssi_l, rssi_buf);
        lv_obj_set_style_text_color(rssi_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(rssi_l, &lv_font_montserrat_10, 0);
        lv_obj_align(rssi_l, LV_ALIGN_RIGHT_MID, -6, 0);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(target);
            if (name) {
                chat_screen_open_dm(name);
            }
        }, LV_EVENT_CLICKED, nullptr);

        // Free the heap-allocated name copy when the row is deleted
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Finder — nearby nodes with Ping Nearby
// ════════════════════════════════════════════════════════
void finder_screen_show()
{
    lv_obj_t* scr = make_screen_full("Finder");

    bool have_ping = slopos::mesh::getPingResultCount() > 0;

    // ── Ping status / button area ──────────────────
    lv_obj_t* ping_row = lv_obj_create(scr);
    lv_obj_set_size(ping_row, CONTENT_W, 24);
    lv_obj_align(ping_row, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 2);
    lv_obj_set_style_bg_opa(ping_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ping_row, 0, 0);
    lv_obj_set_flex_flow(ping_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ping_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (slopos::mesh::pingIsActive()) {
        // Ping in progress — show countdown
        uint32_t remain = slopos::mesh::pingCooldownRemaining();
        uint32_t elapsed = 3000 - (remain > 0 ? remain : 0);
        char ping_buf[40];
        snprintf(ping_buf, sizeof(ping_buf), "%s Listening... (%lu/%lu)",
                 LV_SYMBOL_AUDIO, (unsigned long)(elapsed / 1000), 3UL);
        lv_obj_t* status = lv_label_create(ping_row);
        lv_label_set_text(status, ping_buf);
        lv_obj_set_style_text_color(status, lv_color_hex(ACCENT), 0);
    } else if (slopos::mesh::pingOnCooldown()) {
        // On cooldown — show remaining time
        uint32_t cd = (slopos::mesh::pingCooldownRemaining() + 999) / 1000;
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
            slopos::mesh::sendPingNearby();
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

    char buf[80];
    int row_n = 0;

    // ── Ping results ───────────────────────────────
    if (have_ping) {
        int n = slopos::mesh::getPingResultCount();
        for (int i = 0; i < n; i++) {
            auto* r = slopos::mesh::getPingResult(i);
            if (!r) continue;
            row_n++;
            snprintf(buf, sizeof(buf), "%s  %ddBm", r->name, r->rssi);
            lv_obj_t* item = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
            lv_obj_set_style_bg_color(item,
                lv_color_hex(row_n % 2 == 1 ? BG_TERTIARY : BG_INPUT), 0);
        }
    }

    // ── Repeaters from contact list ────────────────
    slopos::mesh::ContactInfo contacts[32];
    int total = slopos::mesh::exportContactsFull(contacts, 32);
    if (total > 32) total = 32;
    if (total < 0) total = 0;

    // Build a set of ping responder names so we don't double-show
    bool is_ping_responder[32] = {};
    if (have_ping) {
        int np = slopos::mesh::getPingResultCount();
        for (int i = 0; i < np && i < 32; i++) {
            auto* r = slopos::mesh::getPingResult(i);
            if (!r) continue;
            for (int j = 0; j < total; j++) {
                if (strcmp(r->name, contacts[j].name) == 0) {
                    is_ping_responder[j] = true;
                    break;
                }
            }
        }
    }

    int n_repeaters = 0;
    for (int i = 0; i < total; i++) {
        if (contacts[i].type == ADV_TYPE_REPEATER && !is_ping_responder[i]) {
            if (n_repeaters < i) contacts[n_repeaters] = contacts[i];
            n_repeaters++;
        }
    }

    for (int i = 0; i < n_repeaters; i++) {
        row_n++;
        snprintf(buf, sizeof(buf), "%s  %ddBm", contacts[i].name, contacts[i].rssi);
        lv_obj_t* item = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(item,
            lv_color_hex(row_n % 2 == 1 ? BG_TERTIARY : BG_INPUT), 0);
    }

    // ── Empty state ────────────────────────────────
    bool show_empty = (row_n == 0);
    if (show_empty) {
        // Choose message based on state
        const char* msg;
        if (slopos::mesh::pingIsActive()) {
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
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
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
        lv_obj_set_style_text_font(foot, &lv_font_montserrat_10, 0);
        lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_label_set_text(foot, buf);
    }

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Repeaters — infrastructure relay nodes only
// ════════════════════════════════════════════════════════
void repeaters_screen_show()
{
    lv_obj_t* scr = make_screen_full("Repeaters");

    slopos::mesh::ContactInfo contacts[32];
    int total = slopos::mesh::exportContactsFull(contacts, 32);

    // Filter to repeaters only
    int n = 0;
    for (int i = 0; i < total; i++) {
        if (contacts[i].type == ADV_TYPE_REPEATER) {
            if (n < i) contacts[n] = contacts[i];
            n++;
        }
    }

    if (n == 0) {
        lv_obj_t* info = lv_label_create(scr);
        lv_label_set_text(info,
            "No repeaters found.\n\n"
            "Repeaters are infrastructure relay nodes\n"
            "that extend the range of the mesh network.");
        lv_obj_set_width(info, CONTENT_W);
        lv_obj_set_style_pad_left(info, 8, 0);
        lv_obj_set_style_pad_right(info, 8, 0);
        lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(info, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
        lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);
        show_screen(scr);
        return;
    }

    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    static constexpr int ROW_H = 32;

    for (int i = 0; i < n; i++) {
        auto& c = contacts[i];

        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);

        // Icon
        lv_obj_t* icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_12, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 6, 0);

        // Name
        lv_obj_t* name_l = lv_label_create(row);
        lv_label_set_text(name_l, c.name);
        lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(name_l, &lv_font_montserrat_12, 0);
        lv_obj_align(name_l, LV_ALIGN_LEFT_MID, 28, 0);

        // RSSI
        char rssi_buf[12];
        snprintf(rssi_buf, sizeof(rssi_buf), "%ddBm", c.rssi);
        lv_obj_t* rssi_l = lv_label_create(row);
        lv_label_set_text(rssi_l, rssi_buf);
        lv_obj_set_style_text_color(rssi_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(rssi_l, &lv_font_montserrat_10, 0);
        lv_obj_align(rssi_l, LV_ALIGN_RIGHT_MID, -6, 0);
    }

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Signal — radio statistics
// ════════════════════════════════════════════════════════
void signal_screen_show()
{
    lv_obj_t* scr = make_screen_full("Signal");

    int rssi   = slopos::mesh::getLastRSSI();
    float snr  = slopos::mesh::getLastSNR();
    int noise  = slopos::mesh::getNoiseFloor();
    const slopos::NodePrefs& p = slopos::prefs_get();

    lv_obj_t* lbl = lv_label_create(scr);
    lv_obj_set_width(lbl, CONTENT_W);
    lv_obj_set_style_pad_left(lbl, 8, 0);
    lv_obj_set_style_pad_right(lbl, 8, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);

    char buf[512];
    if (p.configured) {
        snprintf(buf, sizeof(buf),
            "RSSI:    %d dBm\n"
            "SNR:     %.1f dB\n"
            "Noise:   %d dBm\n\n"
            "Freq:    %.3f MHz\n"
            "BW:      %.1f kHz\n"
            "SF:      %d\n"
            "CR:      4/%d\n"
            "TX Pwr:  %d dBm",
            rssi, snr, noise,
            p.freq, p.bw, p.sf, p.cr, p.tx_power_dbm);
    } else {
        snprintf(buf, sizeof(buf),
            "RSSI:    %d dBm\n"
            "SNR:     %.1f dB\n"
            "Noise:   %d dBm\n\n"
            "Radio:   NOT CONFIGURED\n"
            "Go to Settings > Radio\n"
            "to set frequency/power.",
            rssi, snr, noise);
    }
    lv_label_set_text(lbl, buf);

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Map — offline tile maps
// ════════════════════════════════════════════════════════
void map_screen_show()
{
    lv_obj_t* scr = make_screen_full("Map");

    slopos_map_init();
    slopos_map_reparent(scr);
    slopos_map_render();

    lv_obj_t* map = lv_obj_create(scr);
    lv_obj_set_size(map, DISPLAY_W, CONTENT_H);
    lv_obj_align(map, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(map, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(map, 0, 0);
    lv_obj_add_flag(map, LV_OBJ_FLAG_CLICKABLE);

    static int drag_start_x = 0, drag_start_y = 0;
    static uint32_t map_last_render_ms = 0;

    // Reset drag state on every entry so stale values from a previous visit
    // don't cause a map jump or skip the first re-render.
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        drag_start_x = 0;
        drag_start_y = 0;
        map_last_render_ms = 0;
    }, LV_EVENT_SCREEN_LOADED, nullptr);

    lv_obj_add_event_cb(map, [](lv_event_t* e) {
        int code = lv_event_get_code(e);
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
        lv_indev_t* indev = lv_indev_get_act();
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        if (code == LV_EVENT_PRESSED) {
            drag_start_x = pt.x; drag_start_y = pt.y;
        } else if (code == LV_EVENT_PRESSING) {
            int dx = drag_start_x - pt.x;
            int dy = drag_start_y - pt.y;
            drag_start_x = pt.x; drag_start_y = pt.y;
            if (dx != 0 || dy != 0) slopos_map_pan(dx, dy);
            uint32_t now = millis();
            if (now - map_last_render_ms >= 200) {
                slopos_map_render();
                map_last_render_ms = now;
            }
        } else if (code == LV_EVENT_RELEASED) {
            slopos_map_render();
            map_last_render_ms = millis();
        }
    }, LV_EVENT_ALL, nullptr);

    // Zoom buttons (above bottom bar)
    int zoom_y_base = DISPLAY_H - BOT_BAR_H - DIVIDER_H - 8;

    lv_obj_t* zoom_in = lv_btn_create(scr);
    lv_obj_set_size(zoom_in, 32, 32);
    lv_obj_align(zoom_in, LV_ALIGN_BOTTOM_RIGHT, -8, -(BOT_BAR_H + DIVIDER_H + 8));
    lv_obj_set_style_bg_color(zoom_in, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(zoom_in, 0, 0);
    lv_obj_t* zi = lv_label_create(zoom_in);
    lv_label_set_text(zi, "+"); lv_obj_center(zi);
    lv_obj_add_event_cb(zoom_in, [](lv_event_t*) { slopos_map_zoom_in(); slopos_map_render(); },
                        LV_EVENT_CLICKED, nullptr);

    lv_obj_t* zoom_out = lv_btn_create(scr);
    lv_obj_set_size(zoom_out, 32, 32);
    lv_obj_align(zoom_out, LV_ALIGN_BOTTOM_RIGHT, -8, -(BOT_BAR_H + DIVIDER_H + 48));
    lv_obj_set_style_bg_color(zoom_out, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(zoom_out, 0, 0);
    lv_obj_t* zo = lv_label_create(zoom_out);
    lv_label_set_text(zo, "-"); lv_obj_center(zo);
    lv_obj_add_event_cb(zoom_out, [](lv_event_t*) { slopos_map_zoom_out(); slopos_map_render(); },
                        LV_EVENT_CLICKED, nullptr);

    (void)zoom_y_base;
    show_screen(scr);
    lv_timer_create([](lv_timer_t* t) {
        slopos_map_render();
        lv_timer_del(t);
    }, 250, nullptr);
}

// Update the text inside a settings row button (used after live time set)
static void update_row_label(lv_obj_t* row, const char* new_text)
{
    if (!row) return;
    uint32_t n = lv_obj_get_child_cnt(row);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* ch = lv_obj_get_child(row, i);
        if (lv_obj_check_type(ch, &lv_label_class)) {
            lv_label_set_text(ch, new_text);
            return;
        }
    }
}

struct DateTimeDialogCtx {
    lv_obj_t* input;
    lv_obj_t* feedback;
    bool       is_date;
};

static void datetime_set_dialog(lv_obj_t* parent, bool is_date)
{
    int y, mo, d, h, mi;
    slopos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);

    char cur[16];
    if (is_date) snprintf(cur, sizeof(cur), "%04d-%02d-%02d", y, mo, d);
    else         snprintf(cur, sizeof(cur), "%02d:%02d", h, mi);

    auto dlg_sz = dialog_size(260, 120);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, is_date ? "Set Date (YYYY-MM-DD)" : "Set Time (HH:MM 24h)");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* input = lv_textarea_create(dlg);
    lv_obj_set_size(input, dlg_sz.w - 16, 28);
    lv_obj_align(input, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(input, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_width(input, 0, 0);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_text(input, cur);

    // Focus immediately so the physical keyboard works without tapping the field
    lv_group_t* grp = lv_group_get_default();
    if (grp) {
        lv_group_add_obj(grp, input);
        lv_group_focus_obj(input);
    }

    lv_obj_t* fb = lv_label_create(dlg);
    lv_obj_set_style_text_color(fb, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(fb, &lv_font_montserrat_10, 0);
    lv_obj_align(fb, LV_ALIGN_BOTTOM_MID, 0, -28);

    lv_obj_t* cancel_btn = lv_btn_create(dlg);
    lv_obj_set_size(cancel_btn, 72, 24);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(cancel_btn, 0, 0);
    lv_obj_t* cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_10, 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_10, 0);
    lv_obj_center(sl);

    // ctx lives until the dialog is deleted (see LV_EVENT_DELETE below)
    auto* ctx = new DateTimeDialogCtx{ input, fb, is_date };
    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (DateTimeDialogCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* ctx = (DateTimeDialogCtx*)lv_event_get_user_data(e);
        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
        const char* s = lv_textarea_get_text(ctx->input);

        bool valid = false;
        uint32_t epoch = 0;

        if (ctx->is_date) {
            int ny, nm, nd;
            if (sscanf(s, "%d-%d-%d", &ny, &nm, &nd) == 3 &&
                ny > 2020 && nm >= 1 && nm <= 12 && nd >= 1 && nd <= 31) {
                int cy, cmo, cd, ch, cmi;
                slopos::mesh::getCurrentLocalDateTime(&cy, &cmo, &cd, &ch, &cmi);
                epoch = slopos::mesh::makeEpoch(ny, nm, nd, ch, cmi);
                valid = true;
            } else {
                lv_label_set_text(ctx->feedback, "Invalid date (YYYY-MM-DD)");
            }
        } else {
            int nh, nm_v;
            if (sscanf(s, "%d:%d", &nh, &nm_v) == 2 &&
                nh >= 0 && nh <= 23 && nm_v >= 0 && nm_v <= 59) {
                int cy, cmo, cd, ch, cmi;
                slopos::mesh::getCurrentLocalDateTime(&cy, &cmo, &cd, &ch, &cmi);
                epoch = slopos::mesh::makeEpoch(cy, cmo, cd, nh, nm_v);
                valid = true;
            } else {
                lv_label_set_text(ctx->feedback, "Invalid time (HH:MM)");
            }
        }

        if (valid && slopos::mesh::setSystemTime(epoch)) {
            int yy, mmo, dd, hh, mmi;
            slopos::mesh::getCurrentLocalDateTime(&yy, &mmo, &dd, &hh, &mmi);
            char dbuf[32], tbuf[16];
            snprintf(dbuf, sizeof(dbuf), "  Date: %04d-%02d-%02d", yy, mmo, dd);
            snprintf(tbuf, sizeof(tbuf), "  Time: %02d:%02d", hh, mmi);
            update_row_label(g_date_row, dbuf);
            update_row_label(g_time_row, tbuf);
            home_screen_update_time(tbuf);
            lv_obj_del_async(dlg);
        }
    }, LV_EVENT_CLICKED, (void*)ctx);
}

static lv_obj_t* g_backlight_row   = nullptr;
static lv_obj_t* g_auto_off_row    = nullptr;
static lv_obj_t* g_chat_history_row = nullptr;

struct BacklightCtx {
    lv_obj_t* value_label;
    lv_obj_t* row_label;
    int       brightness;
};

struct DisplayBrightnessCtx {
    lv_obj_t* value_label;
    lv_obj_t* row_label;
    int       brightness;
};

struct ChatHistoryCapCtx {
    lv_obj_t* value_label;
    lv_obj_t* row_label;
    int       cap;
};

static void chat_message_cap_dialog(lv_obj_t* parent, lv_obj_t* row_label)
{
    auto dlg_sz = dialog_size(220, 120);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Chat Message Cap");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    int cap = (int)chat_screen_get_message_cap();
    lv_obj_t* cap_lbl = lv_label_create(dlg);
    char cap_buf[24];
    snprintf(cap_buf, sizeof(cap_buf), "%d msgs", cap);
    lv_label_set_text(cap_lbl, cap_buf);
    lv_obj_set_style_text_color(cap_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(cap_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(cap_lbl, LV_ALIGN_CENTER, 0, -2);

    auto* minus_btn = lv_btn_create(dlg);
    lv_obj_set_size(minus_btn, 40, 28);
    lv_obj_align(minus_btn, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_bg_color(minus_btn, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(minus_btn, 0, 0);
    lv_obj_t* ml = lv_label_create(minus_btn);
    lv_label_set_text(ml, "-");
    lv_obj_center(ml);

    auto* plus_btn = lv_btn_create(dlg);
    lv_obj_set_size(plus_btn, 40, 28);
    lv_obj_align(plus_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(plus_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(plus_btn, 0, 0);
    lv_obj_t* pl = lv_label_create(plus_btn);
    lv_label_set_text(pl, "+");
    lv_obj_center(pl);

    auto* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_center(sl);

    auto* ctx = new ChatHistoryCapCtx{ cap_lbl, row_label, cap };

    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (ChatHistoryCapCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, (void*)ctx);

    lv_obj_add_event_cb(minus_btn, [](lv_event_t* e) {
        auto* c = (ChatHistoryCapCtx*)lv_event_get_user_data(e);
        c->cap = c->cap > 16 ? c->cap - 16 : 8;
        chat_screen_set_message_cap((uint16_t)c->cap);
        c->cap = (int)chat_screen_get_message_cap();
        char b[24];
        snprintf(b, sizeof(b), "%d msgs", c->cap);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
        auto* c = (ChatHistoryCapCtx*)lv_event_get_user_data(e);
        c->cap += 16;
        chat_screen_set_message_cap((uint16_t)c->cap);
        c->cap = (int)chat_screen_get_message_cap();
        char b[24];
        snprintf(b, sizeof(b), "%d msgs", c->cap);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* c = (ChatHistoryCapCtx*)lv_event_get_user_data(e);
        chat_screen_set_message_cap((uint16_t)c->cap);

        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Chat history: %d messages", c->cap);
        update_row_label(c->row_label, row_buf);

        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, (void*)ctx);
}

static void backlight_dialog(lv_obj_t* parent, lv_obj_t* row_label)
{
    auto dlg_sz = dialog_size(220, 120);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Keyboard Backlight");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    const slopos::NodePrefs& p = slopos::prefs_get();
    int brightness = p.kbd_backlight;

    lv_obj_t* val_lbl = lv_label_create(dlg);
    char val_buf[24];
    snprintf(val_buf, sizeof(val_buf), "%d (%d%%)", brightness, brightness * 100 / 255);
    lv_label_set_text(val_lbl, val_buf);
    lv_obj_set_style_text_color(val_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(val_lbl, LV_ALIGN_CENTER, 0, -2);

    auto* minus_btn = lv_btn_create(dlg);
    lv_obj_set_size(minus_btn, 40, 28);
    lv_obj_align(minus_btn, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_bg_color(minus_btn, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(minus_btn, 0, 0);
    lv_obj_t* ml = lv_label_create(minus_btn);
    lv_label_set_text(ml, "-");
    lv_obj_center(ml);

    auto* plus_btn = lv_btn_create(dlg);
    lv_obj_set_size(plus_btn, 40, 28);
    lv_obj_align(plus_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(plus_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(plus_btn, 0, 0);
    lv_obj_t* pl = lv_label_create(plus_btn);
    lv_label_set_text(pl, "+");
    lv_obj_center(pl);

    auto* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_center(sl);

    auto* ctx = new BacklightCtx{ val_lbl, row_label, brightness };

    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (BacklightCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, (void*)ctx);

    lv_obj_add_event_cb(minus_btn, [](lv_event_t* e) {
        auto* c = (BacklightCtx*)lv_event_get_user_data(e);
        if (c->brightness >= 25) c->brightness -= 25;
        else c->brightness = 0;
        slopos_keyboard_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
        auto* c = (BacklightCtx*)lv_event_get_user_data(e);
        if (c->brightness <= 230) c->brightness += 25;
        else c->brightness = 255;
        slopos_keyboard_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* c = (BacklightCtx*)lv_event_get_user_data(e);
        slopos::NodePrefs np = slopos::prefs_get();
        np.kbd_backlight = (uint8_t)c->brightness;
        slopos::prefs_set(np);
        slopos_keyboard_set_default_brightness(c->brightness);

        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Keyboard BL: %d (%d%%)",
                 c->brightness, c->brightness * 100 / 255);
        update_row_label(c->row_label, row_buf);

        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, (void*)ctx);
}

// ════════════════════════════════════════════════════════
// Auto-off timeout selector dialog
// ════════════════════════════════════════════════════════
static void auto_off_dialog(lv_obj_t* parent, lv_obj_t* row_label)
{
    auto dlg_sz = dialog_size(220, 160);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Auto-off Timeout");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    struct Opt { const char* label; uint16_t value; };
    static constexpr Opt OPTIONS[] = {
        {"Off",   0},
        {"15s",  15},
        {"30s",  30},
        {"1m",   60},
        {"2m",  120},
    };

    int btn_w = 68;
    int btn_h = 28;
    int gap_x = 10;
    int gap_y = 8;
    int start_y = 32;
    int total_w = 3 * btn_w + 2 * gap_x;
    int start_x = (dlg_sz.w - total_w) / 2;

    uint16_t current = slopos::prefs_get().auto_off_timeout;
    static lv_obj_t* selected = nullptr;

    for (int i = 0; i < 5; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = start_x + col * (btn_w + gap_x);
        int y = start_y + row * (btn_h + gap_y);

        lv_obj_t* btn = lv_btn_create(dlg);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_bg_color(btn,
            (OPTIONS[i].value == current) ? lv_color_hex(ACCENT) : lv_color_hex(BG_TERTIARY), 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, OPTIONS[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            lv_obj_t* clicked = (lv_obj_t*)lv_event_get_target(e);
            if (selected) {
                lv_obj_set_style_bg_color(selected, lv_color_hex(BG_TERTIARY), 0);
            }
            lv_obj_set_style_bg_color(clicked, lv_color_hex(ACCENT), 0);
            selected = clicked;
        }, LV_EVENT_CLICKED, nullptr);

        if (OPTIONS[i].value == current) {
            selected = btn;
        }
    }

    lv_obj_t* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_center(sl);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        lv_obj_t* row_lbl = (lv_obj_t*)lv_event_get_user_data(e);
        if (!selected) return;
        lv_obj_t* lbl = lv_obj_get_child(selected, 0);
        if (!lbl) return;
        const char* label = lv_label_get_text(lbl);

        uint16_t value = 30;
        for (auto& opt : OPTIONS) {
            if (strcmp(label, opt.label) == 0) {
                value = opt.value;
                break;
            }
        }

        slopos::NodePrefs np = slopos::prefs_get();
        np.auto_off_timeout = value;
        slopos::prefs_set(np);
        slopos_display_reset_auto_off();

        char row_buf[64];
        if (value == 0) {
            snprintf(row_buf, sizeof(row_buf), "  Auto-off: Off");
        } else {
            snprintf(row_buf, sizeof(row_buf), "  Auto-off: %ds", value);
        }
        update_row_label(row_lbl, row_buf);

        selected = nullptr;
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, (void*)row_label);

    lv_obj_add_event_cb(dlg, [](lv_event_t*) {
        selected = nullptr;
    }, LV_EVENT_DELETE, nullptr);
}

// ════════════════════════════════════════════════════════
// Display brightness dialog
// ════════════════════════════════════════════════════════
static void display_brightness_dialog(lv_obj_t* parent, lv_obj_t* row_label)
{
    auto dlg_sz = dialog_size(220, 120);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Display Brightness");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    const slopos::NodePrefs& p = slopos::prefs_get();
    int brightness = p.display_brightness;

    lv_obj_t* val_lbl = lv_label_create(dlg);
    char val_buf[24];
    snprintf(val_buf, sizeof(val_buf), "%d (%d%%)", brightness, brightness * 100 / 255);
    lv_label_set_text(val_lbl, val_buf);
    lv_obj_set_style_text_color(val_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(val_lbl, LV_ALIGN_CENTER, 0, -2);

    auto* minus_btn = lv_btn_create(dlg);
    lv_obj_set_size(minus_btn, 40, 28);
    lv_obj_align(minus_btn, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_bg_color(minus_btn, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(minus_btn, 0, 0);
    lv_obj_t* ml = lv_label_create(minus_btn);
    lv_label_set_text(ml, "-");
    lv_obj_center(ml);

    auto* plus_btn = lv_btn_create(dlg);
    lv_obj_set_size(plus_btn, 40, 28);
    lv_obj_align(plus_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(plus_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(plus_btn, 0, 0);
    lv_obj_t* pl = lv_label_create(plus_btn);
    lv_label_set_text(pl, "+");
    lv_obj_center(pl);

    auto* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl_lbl = lv_label_create(set_btn);
    lv_label_set_text(sl_lbl, "Set");
    lv_obj_center(sl_lbl);

    auto* ctx = new DisplayBrightnessCtx{ val_lbl, row_label, brightness };

    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (DisplayBrightnessCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, (void*)ctx);

    lv_obj_add_event_cb(minus_btn, [](lv_event_t* e) {
        auto* c = (DisplayBrightnessCtx*)lv_event_get_user_data(e);
        if (c->brightness >= 25) c->brightness -= 25;
        else c->brightness = 0;
        slopos_display_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
        auto* c = (DisplayBrightnessCtx*)lv_event_get_user_data(e);
        if (c->brightness <= 230) c->brightness += 25;
        else c->brightness = 255;
        slopos_display_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* c = (DisplayBrightnessCtx*)lv_event_get_user_data(e);
        slopos::NodePrefs np = slopos::prefs_get();
        np.display_brightness = (uint8_t)c->brightness;
        slopos::prefs_set(np);
        slopos_display_set_brightness(c->brightness);

        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Display: %d (%d%%)",
                 c->brightness, c->brightness * 100 / 255);
        update_row_label(c->row_label, row_buf);

        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, (void*)ctx);
}

// ════════════════════════════════════════════════════════
// Settings — status rows with alternating backgrounds
// ════════════════════════════════════════════════════════
void settings_screen_show()
{
    lv_obj_t* scr = make_screen_full("Settings");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);

    const slopos::NodePrefs& p = slopos::prefs_get();
    char buf[128];
    int row = 0;

    auto add_row = [&](const char* icon, const char* text) -> lv_obj_t* {
        lv_obj_t* btn = lv_list_add_btn(list, icon, text);
        lv_obj_set_style_bg_color(btn,
            lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(TEXT_PRIMARY), 0);
        row++;
        return btn;
    };

    // Node name
    snprintf(buf, sizeof(buf), "  Name: %s", p.node_name);
    add_row(LV_SYMBOL_SETTINGS, buf);

    // Radio config (tappable — opens radio setup)
    if (p.configured) {
        snprintf(buf, sizeof(buf), "  Radio: %.3f MHz / %.1f kHz / SF%d / %d dBm",
                 p.freq, p.bw, p.sf, p.tx_power_dbm);
    } else {
        snprintf(buf, sizeof(buf), "  Radio: NOT CONFIGURED — tap to configure");
    }
    lv_obj_t* btn_rf = add_row(LV_SYMBOL_WIFI, buf);
    if (!p.configured) {
        lv_obj_set_style_bg_color(btn_rf, lv_color_hex(0x4a2020), 0);
        lv_obj_set_style_bg_color(btn_rf, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    }
    lv_obj_add_event_cb(btn_rf, [](lv_event_t*) {
        radio_setup_screen_show();
    }, LV_EVENT_CLICKED, nullptr);

    // SD Card
    snprintf(buf, sizeof(buf), "  SD Card: %s",
             slopos_sdcard_mounted() ? "Mounted" : "Not mounted");
    add_row(LV_SYMBOL_SD_CARD, buf);

    // GPS
    snprintf(buf, sizeof(buf), "  GPS: %s",
             slopos_gps_has_fix() ? "Fix acquired" : "No fix");
    add_row(LV_SYMBOL_GPS, buf);

    // Share location (tappable toggle)
    snprintf(buf, sizeof(buf), "  Share location: %s",
             p.share_location ? "ON" : "OFF");
    lv_obj_t* btn_share = add_row(LV_SYMBOL_GPS, buf);
    lv_obj_add_event_cb(btn_share, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        slopos::NodePrefs np = slopos::prefs_get();
        np.share_location = !np.share_location;
        slopos::prefs_set(np);
        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Share location: %s",
                 np.share_location ? "ON" : "OFF");
        lv_obj_t* lbl = lv_obj_get_child(target, 1);
        if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
            lv_label_set_text(lbl, row_buf);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Keyboard backlight (tappable — opens backlight dialog)
    snprintf(buf, sizeof(buf), "  Keyboard BL: %d (%d%%)",
             p.kbd_backlight, p.kbd_backlight * 100 / 255);
    lv_obj_t* btn_bl = add_row(LV_SYMBOL_KEYBOARD, buf);
    g_backlight_row = btn_bl;
    lv_obj_add_event_cb(btn_bl, [](lv_event_t* e) {
        backlight_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                         (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);

    // Display brightness (tappable — opens brightness dialog)
    snprintf(buf, sizeof(buf), "  Display: %d (%d%%)",
             p.display_brightness, p.display_brightness * 100 / 255);
    lv_obj_t* btn_disp = add_row(LV_SYMBOL_IMAGE, buf);
    lv_obj_add_event_cb(btn_disp, [](lv_event_t* e) {
        display_brightness_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                                 (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);

    // Auto-off timeout (tappable — opens timeout selector)
    if (p.auto_off_timeout == 0) {
        snprintf(buf, sizeof(buf), "  Auto-off: Off");
    } else {
        snprintf(buf, sizeof(buf), "  Auto-off: %ds", p.auto_off_timeout);
    }
    lv_obj_t* btn_auto_off = add_row(LV_SYMBOL_IMAGE, buf);
    g_auto_off_row = btn_auto_off;
    lv_obj_add_event_cb(btn_auto_off, [](lv_event_t* e) {
        auto_off_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                       (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);

    snprintf(buf, sizeof(buf), "  Chat history: %d messages",
             chat_screen_get_message_cap());
    lv_obj_t* btn_chat_cap = add_row(LV_SYMBOL_LIST, buf);
    g_chat_history_row = btn_chat_cap;
    lv_obj_add_event_cb(btn_chat_cap, [](lv_event_t* e) {
        chat_message_cap_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                               (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);

    // Flood max hops (tappable — cycles through values)
    static constexpr uint8_t FLOOD_HOPS_VALUES[] = {0, 3, 5, 10, 20, 50};
    static constexpr const char* FLOOD_HOPS_LABELS[] = {"No limit", "3", "5", "10", "20", "50"};
    static constexpr int NUM_FLOOD_HOPS = 6;
    int cur_idx = 0;
    for (int i = 0; i < NUM_FLOOD_HOPS; i++) {
        if (p.flood_max_hops == FLOOD_HOPS_VALUES[i]) { cur_idx = i; break; }
    }
    snprintf(buf, sizeof(buf), "  Flood max hops: %s", FLOOD_HOPS_LABELS[cur_idx]);
    lv_obj_t* btn_flood = add_row(LV_SYMBOL_WIFI, buf);
    lv_obj_add_event_cb(btn_flood, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        slopos::NodePrefs np = slopos::prefs_get();
        int idx = 0;
        for (int i = 0; i < NUM_FLOOD_HOPS; i++) {
            if (np.flood_max_hops == FLOOD_HOPS_VALUES[i]) { idx = i; break; }
        }
        idx = (idx + 1) % NUM_FLOOD_HOPS;
        np.flood_max_hops = FLOOD_HOPS_VALUES[idx];
        slopos::prefs_set(np);
        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Flood max hops: %s", FLOOD_HOPS_LABELS[idx]);
        lv_obj_t* lbl = lv_obj_get_child(target, 1);
        if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
            lv_label_set_text(lbl, row_buf);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Date
    int y, mo, d, h, mi;
    slopos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);
    snprintf(buf, sizeof(buf), "  Date: %04d-%02d-%02d", y, mo, d);
    lv_obj_t* btn_date = add_row(LV_SYMBOL_SETTINGS, buf);
    g_date_row = btn_date;
    lv_obj_add_event_cb(btn_date, [](lv_event_t* e) {
        datetime_set_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)), true);
    }, LV_EVENT_CLICKED, nullptr);

    // Time
    snprintf(buf, sizeof(buf), "  Time: %02d:%02d", h, mi);
    lv_obj_t* btn_time = add_row(LV_SYMBOL_SETTINGS, buf);
    g_time_row = btn_time;
    lv_obj_add_event_cb(btn_time, [](lv_event_t* e) {
        datetime_set_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)), false);
    }, LV_EVENT_CLICKED, nullptr);

    // Run Setup Wizard
    lv_obj_t* btn_wizard = add_row(LV_SYMBOL_SETTINGS, "  Run Setup Wizard");
    lv_obj_add_event_cb(btn_wizard, [](lv_event_t*) {
        navigate_to(Screen::Onboarding);
    }, LV_EVENT_CLICKED, nullptr);

    // Shut down — graceful power-off
    lv_obj_t* btn_shutdown = add_row(LV_SYMBOL_POWER, "  Shut down");
    lv_obj_set_style_bg_color(btn_shutdown, lv_color_hex(0x4a2020), 0);
    lv_obj_set_style_bg_color(btn_shutdown, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_shutdown, [](lv_event_t* e) {
        lv_obj_t* scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        auto dlg_sz = dialog_size(240, 100);
        lv_obj_t* dlg = lv_obj_create(scr);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_border_width(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, "Shut down?");
        lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* msg = lv_label_create(dlg);
        lv_label_set_text(msg, "Save state and power off?");
        lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_10, 0);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, -4);

        // Cancel button
        lv_obj_t* cancel_btn = lv_btn_create(dlg);
        lv_obj_set_size(cancel_btn, 64, 24);
        lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -4);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_radius(cancel_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(cancel_btn);
        lv_label_set_text(cl, "Cancel");
        lv_obj_center(cl);
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
            lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_CLICKED, nullptr);

        // Confirm button
        lv_obj_t* confirm_btn = lv_btn_create(dlg);
        lv_obj_set_size(confirm_btn, 64, 24);
        lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
        lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(confirm_btn, 0, 0);
        lv_obj_t* cfl = lv_label_create(confirm_btn);
        lv_label_set_text(cfl, "Shut down");
        lv_obj_center(cfl);
        lv_obj_add_event_cb(confirm_btn, [](lv_event_t*) {
            slopos::mesh::shutdown();
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);

    // Version
    snprintf(buf, sizeof(buf), "  SlopOS " SLOPOS_VERSION);
    add_row(LV_SYMBOL_HOME, buf);

    // Null the row pointers when this screen is deleted so stale pointers can't be used
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        g_date_row = nullptr;
        g_time_row = nullptr;
        g_backlight_row = nullptr;
        g_chat_history_row = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Terminal — colored log output helpers
// ════════════════════════════════════════════════════════
static uint32_t term_classify_line(const char* text)
{
    if (strstr(text, "ERROR") || strstr(text, "FAIL") || strstr(text, "failed"))
        return ACCENT_RED;
    if (strstr(text, "WARN") || strstr(text, "WARNING"))
        return ACCENT_ORANGE;
    if (strstr(text, "OK") || strstr(text, "ok") ||
        strstr(text, "sent") || strstr(text, "Pong"))
        return ACCENT_GREEN;
    if (strstr(text, "RSSI") || strstr(text, "SNR") ||
        strstr(text, "Noise") || strstr(text, "dBm") || strstr(text, "MHz"))
        return ACCENT;
    if (text[0] == '>')
        return TEXT_PRIMARY;
    return 0x00ff00u;
}

// ── Terminal line cap — prevent unbounded label accumulation ──
static constexpr unsigned MAX_TERM_LINES = 64;

static void term_add_line(lv_obj_t* log, const char* text)
{
    // Prune oldest line if at cap
    while (lv_obj_get_child_cnt(log) >= MAX_TERM_LINES) {
        lv_obj_t* first = lv_obj_get_child(log, 0);
        if (first) lv_obj_del(first);
        else break;
    }

    lv_obj_t* lbl = lv_label_create(log);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(term_classify_line(text)), 0);
    lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_scroll_to_view(lbl, LV_ANIM_OFF);
}

// ════════════════════════════════════════════════════════
// Terminal — colored log output + command input
// ════════════════════════════════════════════════════════
void terminal_screen_show()
{
    lv_obj_t* scr = make_screen_full("Terminal");

    static constexpr int TERM_INPUT_H = 28;
    static constexpr int TERM_LOG_H   = CONTENT_H - TERM_INPUT_H - DIVIDER_H;  // 167

    // Log output container (pure black, flex-column, scrollable)
    lv_obj_t* log = lv_obj_create(scr);
    lv_obj_set_size(log, LV_PCT(100), TERM_LOG_H);
    lv_obj_align(log, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(log, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(log, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(log, 0, 0);
    lv_obj_set_style_pad_all(log, 4, 0);
    lv_obj_set_flex_flow(log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(log, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(log, LV_SCROLLBAR_MODE_OFF);

    // Boot header lines
    const slopos::NodePrefs& p = slopos::prefs_get();
    term_add_line(log, "SlopOS T-Deck Terminal");
    term_add_line(log, "MeshCore protocol active");
    if (p.configured) {
        char radio_buf[64];
        snprintf(radio_buf, sizeof(radio_buf), "Radio: SX1262 %.3f MHz configured", p.freq);
        term_add_line(log, radio_buf);
    } else {
        term_add_line(log, "Radio: ERROR - not configured");
    }

    // Divider between log and input
    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, CONTENT_Y + TERM_LOG_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // Command input
    lv_obj_t* input = lv_textarea_create(scr);
    lv_obj_set_size(input, LV_PCT(100), TERM_INPUT_H);
    lv_obj_align(input, LV_ALIGN_TOP_MID, 0, CONTENT_Y + TERM_LOG_H + DIVIDER_H);
    lv_obj_set_style_bg_color(input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(input, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_width(input, 0, 0);
    lv_obj_set_style_pad_all(input, 4, 0);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_placeholder_text(input, "> enter command...");

    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, input);
        lv_group_focus_obj(input);
    }

    lv_obj_add_event_cb(input, [](lv_event_t* e) {
        lv_obj_t* ta        = (lv_obj_t*)lv_event_get_target(e);
        const char* cmd     = lv_textarea_get_text(ta);
        if (!cmd || !cmd[0]) return;

        lv_obj_t* log_cont = (lv_obj_t*)lv_event_get_user_data(e);

        // Echo the command
        char echo[280];
        snprintf(echo, sizeof(echo), "> %s", cmd);
        term_add_line(log_cont, echo);

        char result[256] = "";
        if (strcmp(cmd, "help") == 0) {
            snprintf(result, sizeof(result), "Commands: help status advert ping emoji-list");
        } else if (strcmp(cmd, "status") == 0) {
            int rssi  = slopos::mesh::getLastRSSI();
            float snr = slopos::mesh::getLastSNR();
            int noise = slopos::mesh::getNoiseFloor();
            snprintf(result, sizeof(result),
                "RSSI:%ddBm SNR:%.1fdB Noise:%ddBm  Contacts:%d Channels:%d",
                rssi, snr, noise,
                slopos::mesh::getContactCount(),
                slopos::mesh::getChannelCount());
        } else if (strcmp(cmd, "advert") == 0) {
            bool ok = slopos::mesh::sendAdvert();
            snprintf(result, sizeof(result), ok ? "Advert sent" : "Send failed");
        } else if (strcmp(cmd, "ping") == 0) {
            snprintf(result, sizeof(result), "Pong! Uptime: %lums", millis());
        } else if (strcmp(cmd, "emoji-list") == 0) {
            term_add_line(log_cont, "--- Emoji list (362 available) ---");
            char line_buf[128];
            for (int i = 0; i < emoji_font_get_count(); i += 8) {
                int pos = 0;
                for (int j = 0; j < 8 && i + j < emoji_font_get_count(); j++) {
                    if (const char* e = emoji_font_get_by_index(i + j)) {
                        pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%s ", e);
                    }
                }
                if (pos > 0) term_add_line(log_cont, line_buf);
            }
            term_add_line(log_cont, "--- End emoji list ---");
            lv_textarea_set_text(ta, "");
            return;
        } else {
            snprintf(result, sizeof(result), "Unknown: %s  (type 'help')", cmd);
        }

        term_add_line(log_cont, result);
        lv_textarea_set_text(ta, "");
    }, LV_EVENT_READY, log);

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Trace — path tracing
// ════════════════════════════════════════════════════════
static lv_obj_t* trace_result_label = nullptr;

static void trace_screen_delete_cb(lv_event_t*) {
    trace_result_label = nullptr;
}

void trace_screen_show()
{
    lv_obj_t* scr = make_screen_full("Trace Route");
    slopos::mesh::clearTraceResult();
    trace_result_label = nullptr;

    lv_obj_add_event_cb(scr, trace_screen_delete_cb, LV_EVENT_DELETE, nullptr);

    char names[32][32];
    int total = slopos::mesh::exportContacts(names, 32);

    lv_obj_t* info = lv_label_create(scr);
    lv_obj_set_width(info, CONTENT_W);
    lv_obj_set_style_pad_left(info, 8, 0);
    lv_obj_set_style_pad_right(info, 8, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);

    if (total == 0) {
        lv_label_set_text(info,
            "No contacts discovered. Wait for adverts or incoming messages.");
        show_screen(scr);
        return;
    }

    lv_label_set_text(info, "Tap a contact to trace the path.");

    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - 26);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 24);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    static constexpr int ROW_H = 32;

    for (int i = 0; i < total; i++) {
        bool has_path = slopos::mesh::contactHasPath(i);

        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(ACCENT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        // Icon
        lv_obj_t* icon = lv_label_create(row);
        lv_label_set_text(icon, has_path ? LV_SYMBOL_GPS : LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(icon, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_12, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 6, 0);

        // Name
        lv_obj_t* name_l = lv_label_create(row);
        lv_label_set_text(name_l, names[i]);
        lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(name_l, &lv_font_montserrat_12, 0);
        lv_obj_align(name_l, LV_ALIGN_LEFT_MID, 28, 0);

        // Path status
        lv_obj_t* status_l = lv_label_create(row);
        lv_label_set_text(status_l, has_path ? "[path known]" : "[no path]");
        lv_obj_set_style_text_color(status_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(status_l, &lv_font_montserrat_10, 0);
        lv_obj_align(status_l, LV_ALIGN_RIGHT_MID, -6, 0);

        if (has_path) {
            int contact_idx = i;
            lv_obj_add_event_cb(row, [](lv_event_t* e) {
                int idx = (int)(intptr_t)lv_event_get_user_data(e);
                uint32_t tag;
                if (slopos::mesh::sendTrace(idx, &tag)) {
                    lv_obj_t* scr_ref = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
                    lv_obj_t* result_lbl = lv_label_create(scr_ref);
                    lv_obj_set_style_text_color(result_lbl, lv_color_hex(ACCENT), 0);
                    lv_obj_set_style_text_font(result_lbl, &lv_font_montserrat_10, 0);
                    lv_obj_align(result_lbl, LV_ALIGN_BOTTOM_MID, 0,
                                 -(BOT_BAR_H + DIVIDER_H + 24));
                    lv_label_set_text(result_lbl, "Trace sent, waiting...");
                    // Delete previous label if it exists
                    if (trace_result_label) {
                        lv_obj_del_async(trace_result_label);
                        trace_result_label = nullptr;
                    }
                    trace_result_label = result_lbl;

                    lv_timer_create([](lv_timer_t* t) {
                        if (!trace_result_label) { lv_timer_del(t); return; }
                        if (slopos::mesh::hasTraceResult()) {
                            uint8_t len = slopos::mesh::getTracePathLen();
                            if (len > 64) len = 64;  // MAX_PATH_SIZE
                            uint8_t snrs[64], hashes[64];
                            slopos::mesh::getTracePath(snrs, hashes);
                            char res[128];
                            if (len == 0) {
                                snprintf(res, sizeof(res), "Trace timed out");
                            } else {
                                int pos = snprintf(res, sizeof(res),
                                    "Trace: %d hop%s", len, len == 1 ? "" : "s");
                                for (int h = 0; h < len && pos < (int)sizeof(res) - 10; h++)
                                    pos += snprintf(res + pos, sizeof(res) - pos,
                                        "  [%ddBm]", snrs[h]);
                            }
                            lv_label_set_text(trace_result_label, res);
                            slopos::mesh::clearTraceResult();
                            lv_timer_del(t);
                        }
                    }, 500, nullptr);
                }
            }, LV_EVENT_CLICKED, (void*)(intptr_t)contact_idx);
        }
    }

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Channels — channel list with create/join
// ════════════════════════════════════════════════════════
static void refresh_channel_list(lv_obj_t* list);

static lv_obj_t* channel_create_dialog(lv_obj_t* parent)
{
    auto dlg_sz = dialog_size(260, 140);
    lv_obj_t* dialog = lv_obj_create(parent);
    lv_obj_set_size(dialog, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dialog, 0, 0);
    lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_pad_all(dialog, 8, 0);

    lv_obj_t* title = lv_label_create(dialog);
    lv_label_set_text(title, "Add # Channel");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* name_label = lv_label_create(dialog);
    lv_label_set_text(name_label, "Hashtag:");
    lv_obj_set_style_text_color(name_label, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 4, 28);

    lv_obj_t* name_input = lv_textarea_create(dialog);
    lv_obj_set_size(name_input, dlg_sz.w - 16, 28);
    lv_obj_align(name_input, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_color(name_input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(name_input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(name_input, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_width(name_input, 0, 0);
    lv_textarea_set_one_line(name_input, true);
    lv_textarea_set_max_length(name_input, 31);
    lv_textarea_set_placeholder_text(name_input, "e.g. #general");

    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, name_input);
        lv_group_focus_obj(name_input);
    }

    lv_obj_t* feedback = lv_label_create(dialog);
    lv_obj_set_style_text_color(feedback, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(feedback, &lv_font_montserrat_10, 0);
    lv_obj_align(feedback, LV_ALIGN_BOTTOM_MID, 0, -32);

    lv_obj_t* create_btn = lv_btn_create(dialog);
    lv_obj_set_size(create_btn, 100, 28);
    lv_obj_align(create_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(create_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(create_btn, 0, 0);
    lv_obj_t* cbl = lv_label_create(create_btn);
    lv_label_set_text(cbl, "Add");
    lv_obj_center(cbl);

    lv_obj_add_event_cb(create_btn, [](lv_event_t* e) {
        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
        lv_obj_t* scr = lv_obj_get_screen(dlg);
        lv_obj_t* fb  = (lv_obj_t*)lv_event_get_user_data(e);

        uint32_t n = lv_obj_get_child_cnt(dlg);
        lv_obj_t* name_in = nullptr;
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t* child = lv_obj_get_child(dlg, i);
            if (lv_obj_check_type(child, &lv_textarea_class)) {
                if (!name_in) name_in = child;
            }
        }

        const char* name = name_in ? lv_textarea_get_text(name_in) : "";

        if (!name[0]) { if (fb) lv_label_set_text(fb, "Enter a hashtag"); return; }

        bool ok = slopos::mesh::addHashtagChannel(name);
        if (ok) {
            lv_obj_del_async(dlg);
            for (uint32_t i = 0; i < lv_obj_get_child_cnt(scr); i++) {
                lv_obj_t* child = lv_obj_get_child(scr, i);
                if (lv_obj_check_type(child, &lv_list_class)) {
                    refresh_channel_list(child);
                    break;
                }
            }
        } else {
            if (fb) lv_label_set_text(fb, "Invalid or full");
        }
    }, LV_EVENT_CLICKED, (void*)feedback);

    return dialog;
}

static void refresh_channel_list(lv_obj_t* list)
{
    lv_obj_clean(list);
    char names[8][32];
    int n = slopos::mesh::exportChannels(names, 8);
    if (n == 0) {
        lv_obj_t* item = lv_list_add_btn(list, LV_SYMBOL_WARNING, "No channels joined");
        lv_obj_set_style_bg_color(item, lv_color_hex(BG_TERTIARY), 0);
    } else {
        for (int i = 0; i < n; i++) {
            lv_obj_t* item = lv_list_add_btn(list, LV_SYMBOL_LIST, names[i]);
            lv_obj_set_style_bg_color(item,
                lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        }
    }
}

void channels_screen_show()
{
    lv_obj_t* scr = make_screen_full("Channels");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - 36);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);

    refresh_channel_list(list);

    lv_obj_t* add_btn = lv_btn_create(scr);
    lv_obj_set_size(add_btn, 140, 28);
    lv_obj_align(add_btn, LV_ALIGN_BOTTOM_MID, 0, -(BOT_BAR_H + DIVIDER_H + 4));
    lv_obj_set_style_bg_color(add_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(add_btn, 0, 0);
    lv_obj_t* al = lv_label_create(add_btn);
    lv_label_set_text(al, LV_SYMBOL_PLUS "  Add # Channel");
    lv_obj_set_style_text_font(al, &lv_font_montserrat_10, 0);
    lv_obj_center(al);

    lv_obj_add_event_cb(add_btn, [](lv_event_t* e) {
        lv_obj_t* scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        channel_create_dialog(scr);
    }, LV_EVENT_CLICKED, nullptr);

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Advertise — broadcast presence
// ════════════════════════════════════════════════════════
void advertise_screen_show()
{
    lv_obj_t* scr = make_screen_full("Advertise");

    lv_obj_t* info = lv_label_create(scr);
    lv_label_set_text(info,
        "Advertise Presence\n\n"
        "Broadcast your node to the mesh network.\n\n"
        "Other nodes will see you in their Heard list.\n\n"
        "Tap below to send advert.");
    lv_obj_set_width(info, CONTENT_W);
    lv_obj_set_style_pad_left(info, 8, 0);
    lv_obj_set_style_pad_right(info, 8, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);

    lv_obj_t* status = lv_label_create(scr);
    g_advert_status_label = status;
    lv_label_set_text(status, "Status: never sent yet");
    lv_obj_set_width(status, CONTENT_W);
    lv_obj_set_style_pad_left(status, 8, 0);
    lv_obj_set_style_pad_right(status, 8, 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_10, 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 110);

    lv_obj_t* btn = lv_btn_create(scr);
    g_advert_button = btn;
    lv_obj_set_size(btn, 140, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -(BOT_BAR_H + DIVIDER_H + 8));
    lv_obj_set_style_bg_color(btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(TEXT_MUTED), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_t* bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_AUDIO "  Advertise Now");
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, [](lv_event_t*) {
        slopos::mesh::sendAdvert();
        advertise_status_update();
    }, LV_EVENT_CLICKED, nullptr);

    if (g_advert_status_timer) {
        lv_timer_del(g_advert_status_timer);
        g_advert_status_timer = nullptr;
    }
    g_advert_status_timer = lv_timer_create(advertise_status_timer_cb, 1000, nullptr);
    lv_obj_add_event_cb(scr, advertise_screen_delete_cb, LV_EVENT_DELETE, nullptr);
    advertise_status_update();

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Radio Setup state — shared between main screen and Custom RF screen
static float s_rf_freq = 869.618f;
static int   s_rf_sf   = 8;
static float s_rf_bw   = 62.5f;
static int   s_rf_cr   = 5;
static int   s_rf_pwr  = 22;

void custom_rf_screen_show()
{
    using namespace slopos::theme;
    using responsive::CONTENT_Y;
    using responsive::CONTENT_W;
    using responsive::CONTENT_H;

    lv_obj_t* scr = make_screen_full("Custom RF");

    int y = CONTENT_Y + 4;
    int row_h = 28;
    int lbl_w = 50;
    int inp_w = CONTENT_W - lbl_w - 12;

    // Pre-fill from shared state
    char freq_buf[16], sf_buf[8], bw_buf[12], cr_buf[8], pwr_buf[8];
    snprintf(freq_buf, sizeof(freq_buf), "%.3f", s_rf_freq);
    snprintf(sf_buf,   sizeof(sf_buf),   "%d",   s_rf_sf);
    snprintf(bw_buf,   sizeof(bw_buf),   "%.1f",  s_rf_bw);
    snprintf(cr_buf,   sizeof(cr_buf),   "%d",   s_rf_cr);
    snprintf(pwr_buf,  sizeof(pwr_buf),  "%d",   s_rf_pwr);

    struct { const char* label; const char* val; lv_obj_t* ta; } fields[] = {
        {"Freq", freq_buf, nullptr},
        {"SF",   sf_buf,   nullptr},
        {"BW",   bw_buf,   nullptr},
        {"CR",   cr_buf,   nullptr},
        {"Pwr",  pwr_buf,  nullptr},
    };

    lv_group_t* grp = lv_group_get_default();
    for (int i = 0; i < 5; i++) {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, fields[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, y + 3);

        lv_obj_t* ta = lv_textarea_create(scr);
        lv_obj_set_size(ta, inp_w, 20);
        lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 8 + lbl_w, y);
        lv_obj_set_style_bg_color(ta, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_text_color(ta, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_10, 0);
        lv_obj_set_style_border_width(ta, 0, 0);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_text(ta, fields[i].val);
        fields[i].ta = ta;
        if (grp) lv_group_add_obj(grp, ta);
        y += row_h;
    }
    if (grp && fields[0].ta) lv_group_focus_obj(fields[0].ta);

    // Error label
    lv_obj_t* err_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(err_lbl, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(err_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_width(err_lbl, CONTENT_W);
    lv_obj_align(err_lbl, LV_ALIGN_TOP_LEFT, 8, y);
    lv_label_set_text(err_lbl, "");

    y += 22;

    // Apply button
    auto* btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 160, 28);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(btn, 0, 0);
    auto* bl = lv_label_create(btn);
    lv_label_set_text(bl, "Apply");
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, [](lv_event_t*) {
        // Find textareas by scanning children of root scr
        lv_obj_t* scr = lv_scr_act();
        // Walk children looking for textareas (5 of them)
        lv_obj_t* ta_freq = nullptr;
        lv_obj_t* ta_sf   = nullptr;
        lv_obj_t* ta_bw   = nullptr;
        lv_obj_t* ta_cr   = nullptr;
        lv_obj_t* ta_pwr  = nullptr;
        int found = 0;
        uint32_t cnt = lv_obj_get_child_cnt(scr);
        for (uint32_t i = 0; i < cnt && found < 5; i++) {
            lv_obj_t* child = lv_obj_get_child(scr, i);
            if (lv_obj_check_type(child, &lv_textarea_class)) {
                switch (found) {
                    case 0: ta_freq = child; break;
                    case 1: ta_sf   = child; break;
                    case 2: ta_bw   = child; break;
                    case 3: ta_cr   = child; break;
                    case 4: ta_pwr  = child; break;
                }
                found++;
            }
        }

        float freq = atof(lv_textarea_get_text(ta_freq));
        int   sf   = atoi(lv_textarea_get_text(ta_sf));
        float bw   = atof(lv_textarea_get_text(ta_bw));
        int   cr   = atoi(lv_textarea_get_text(ta_cr));
        int   pwr  = atoi(lv_textarea_get_text(ta_pwr));

        const char* err = nullptr;
        if (freq < 400.0f || freq > 930.0f)              err = "Freq: 400.0 - 930.0 MHz";
        else if (sf < 7 || sf > 12)                      err = "SF: 7 - 12";
        else if (bw < 7.8f || bw > 500.0f)               err = "BW: 7.8 - 500 kHz";
        else if (cr < 5 || cr > 8)                       err = "CR: 4/5 - 4/8";
        else if (pwr < 2 || pwr > 22)                    err = "TX Pwr: 2 - 22 dBm";

        // Find err_lbl (last label before the button, positioned dynamically)
        // Simple approach: create a fresh label each time
        if (err) {
            lv_obj_t* el = lv_obj_get_child(scr, lv_obj_get_child_cnt(scr) - 2);
            if (lv_obj_check_type(el, &lv_label_class)) {
                lv_label_set_text(el, err);
            }
            return;
        }

        s_rf_freq = freq;
        s_rf_sf   = sf;
        s_rf_bw   = bw;
        s_rf_cr   = cr;
        s_rf_pwr  = pwr;
        go_back();
    }, LV_EVENT_CLICKED, nullptr);

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Radio Setup — configure frequency, SF, BW, power
// ════════════════════════════════════════════════════════
void radio_setup_screen_show()
{
    lv_obj_t* scr = make_screen_full("Radio Setup");

    const slopos::NodePrefs& p = slopos::prefs_get();

    s_rf_freq = p.configured ? p.freq          : 869.618f;
    s_rf_sf   = p.configured ? p.sf            : 8;
    s_rf_bw   = p.configured ? p.bw            : 62.5f;
    s_rf_cr   = p.configured ? p.cr            : 5;
    s_rf_pwr  = p.configured ? p.tx_power_dbm  : 22;

    // Warning
    auto* warn = lv_label_create(scr);
    lv_label_set_text(warn,
        "Check local regulations. Incorrect settings may be illegal.");
    lv_obj_set_width(warn, CONTENT_W);
    lv_obj_set_style_pad_left(warn, 8, 0);
    lv_obj_set_style_pad_right(warn, 8, 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(warn, lv_color_hex(0xccaa00), 0);
    lv_obj_set_style_text_font(warn, &lv_font_montserrat_10, 0);
    lv_obj_align(warn, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 2);

    // ── Left column: frequency presets ────────────────────
    int lx = 4;
    int lw = 148;
    static const struct { const char* label; float freq; } freqs[] = {
        {"868.000 (EU)",   868.000f},
        {"869.525 (UK)",   869.525f},
        {"869.618 (UK)",   869.618f},
        {"915.000 (US)",   915.000f},
        {"433.500 (EU)",   433.500f},
    };
    static constexpr int NUM_FREQS = 5;
    static lv_obj_t* freq_btns[NUM_FREQS] = {};

    int ly = CONTENT_Y + 26;
    int btn_h = 18;
    for (int i = 0; i < NUM_FREQS; i++) {
        auto* btn = lv_btn_create(scr);
        lv_obj_set_size(btn, lw, btn_h);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, lx, ly);
        lv_obj_set_style_bg_color(btn, lv_color_hex(
            fabsf(s_rf_freq - freqs[i].freq) < 0.001f ? 0x2a5a2a : BG_TERTIARY), 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        auto* tl = lv_label_create(btn);
        lv_label_set_text(tl, freqs[i].label);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_10, 0);
        lv_obj_center(tl);
        freq_btns[i] = btn;

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            s_rf_freq = freqs[idx].freq;
            for (int j = 0; j < NUM_FREQS; j++) {
                lv_obj_set_style_bg_color(freq_btns[j],
                    lv_color_hex(j == idx ? 0x2a5a2a : BG_TERTIARY), 0);
            }
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        ly += 22;
    }

    // ── Right column: controls ────────────────────────────
    int rx = lx + lw + 6;
    int rw = DISPLAY_W - rx - 4;
    int ry = CONTENT_Y + 26;
    char buf[64];

    // Custom RF button
    auto* custom_btn = lv_btn_create(scr);
    lv_obj_set_size(custom_btn, rw, 22);
    lv_obj_align(custom_btn, LV_ALIGN_TOP_LEFT, rx, ry);
    lv_obj_set_style_bg_color(custom_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(custom_btn, 0, 0);
    lv_obj_set_style_border_width(custom_btn, 0, 0);
    auto* ctl = lv_label_create(custom_btn);
    lv_label_set_text(ctl, "Custom RF...");
    lv_obj_set_style_text_font(ctl, &lv_font_montserrat_10, 0);
    lv_obj_center(ctl);
    lv_obj_add_event_cb(custom_btn, [](lv_event_t*) {
        custom_rf_screen_show();
    }, LV_EVENT_CLICKED, nullptr);
    ry += 28;

    // SF row
    snprintf(buf, sizeof(buf), "SF:%d", s_rf_sf);
    auto* sf_lbl = lv_label_create(scr);
    lv_label_set_text(sf_lbl, buf);
    lv_obj_set_style_text_color(sf_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(sf_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(sf_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* sf_minus = lv_btn_create(scr);
    lv_obj_set_size(sf_minus, 24, 20);
    lv_obj_align(sf_minus, LV_ALIGN_TOP_LEFT, rx + rw - 54, ry - 2);
    lv_obj_set_style_bg_color(sf_minus, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(sf_minus, 0, 0);
    auto* sml = lv_label_create(sf_minus); lv_label_set_text(sml, "-"); lv_obj_center(sml);
    lv_obj_add_event_cb(sf_minus, [](lv_event_t* e) {
        if (s_rf_sf > 6) { s_rf_sf--; char b[16]; snprintf(b, sizeof(b), "SF:%d", s_rf_sf); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b); }
    }, LV_EVENT_CLICKED, (void*)sf_lbl);

    auto* sf_plus = lv_btn_create(scr);
    lv_obj_set_size(sf_plus, 24, 20);
    lv_obj_align(sf_plus, LV_ALIGN_TOP_LEFT, rx + rw - 26, ry - 2);
    lv_obj_set_style_bg_color(sf_plus, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(sf_plus, 0, 0);
    auto* spl = lv_label_create(sf_plus); lv_label_set_text(spl, "+"); lv_obj_center(spl);
    lv_obj_add_event_cb(sf_plus, [](lv_event_t* e) {
        if (s_rf_sf < 12) { s_rf_sf++; char b[16]; snprintf(b, sizeof(b), "SF:%d", s_rf_sf); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b); }
    }, LV_EVENT_CLICKED, (void*)sf_lbl);
    ry += 24;

    // BW row
    snprintf(buf, sizeof(buf), "BW:%.1f", s_rf_bw);
    auto* bw_lbl = lv_label_create(scr);
    lv_label_set_text(bw_lbl, buf);
    lv_obj_set_style_text_color(bw_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(bw_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(bw_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* bw_minus = lv_btn_create(scr);
    lv_obj_set_size(bw_minus, 24, 20);
    lv_obj_align(bw_minus, LV_ALIGN_TOP_LEFT, rx + rw - 54, ry - 2);
    lv_obj_set_style_bg_color(bw_minus, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(bw_minus, 0, 0);
    auto* bml = lv_label_create(bw_minus); lv_label_set_text(bml, "-"); lv_obj_center(bml);
    lv_obj_add_event_cb(bw_minus, [](lv_event_t* e) {
        float v[] = {500.0f,250.0f,125.0f,62.5f,41.7f,31.25f,20.8f,15.6f,10.4f,7.8f};
        for (auto& x : v) if (s_rf_bw > x + 0.01f) { s_rf_bw = x; break; }
        char b[24]; snprintf(b, sizeof(b), "BW:%.1f", s_rf_bw); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b);
    }, LV_EVENT_CLICKED, (void*)bw_lbl);

    auto* bw_plus = lv_btn_create(scr);
    lv_obj_set_size(bw_plus, 24, 20);
    lv_obj_align(bw_plus, LV_ALIGN_TOP_LEFT, rx + rw - 26, ry - 2);
    lv_obj_set_style_bg_color(bw_plus, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(bw_plus, 0, 0);
    auto* bpl = lv_label_create(bw_plus); lv_label_set_text(bpl, "+"); lv_obj_center(bpl);
    lv_obj_add_event_cb(bw_plus, [](lv_event_t* e) {
        float v[] = {7.8f,10.4f,15.6f,20.8f,31.25f,41.7f,62.5f,125.0f,250.0f,500.0f};
        for (auto& x : v) if (s_rf_bw < x - 0.01f) { s_rf_bw = x; break; }
        char b[24]; snprintf(b, sizeof(b), "BW:%.1f", s_rf_bw); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b);
    }, LV_EVENT_CLICKED, (void*)bw_lbl);
    ry += 24;

    // TX power row
    snprintf(buf, sizeof(buf), "TX:%d", s_rf_pwr);
    auto* pwr_lbl = lv_label_create(scr);
    lv_label_set_text(pwr_lbl, buf);
    lv_obj_set_style_text_color(pwr_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(pwr_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(pwr_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* pwr_minus = lv_btn_create(scr);
    lv_obj_set_size(pwr_minus, 24, 20);
    lv_obj_align(pwr_minus, LV_ALIGN_TOP_LEFT, rx + rw - 54, ry - 2);
    lv_obj_set_style_bg_color(pwr_minus, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(pwr_minus, 0, 0);
    auto* pml = lv_label_create(pwr_minus); lv_label_set_text(pml, "-"); lv_obj_center(pml);
    lv_obj_add_event_cb(pwr_minus, [](lv_event_t* e) {
        if (s_rf_pwr > 2) { s_rf_pwr--; char b[24]; snprintf(b, sizeof(b), "TX:%d", s_rf_pwr); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b); }
    }, LV_EVENT_CLICKED, (void*)pwr_lbl);

    auto* pwr_plus = lv_btn_create(scr);
    lv_obj_set_size(pwr_plus, 24, 20);
    lv_obj_align(pwr_plus, LV_ALIGN_TOP_LEFT, rx + rw - 26, ry - 2);
    lv_obj_set_style_bg_color(pwr_plus, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(pwr_plus, 0, 0);
    auto* ppl = lv_label_create(pwr_plus); lv_label_set_text(ppl, "+"); lv_obj_center(ppl);
    lv_obj_add_event_cb(pwr_plus, [](lv_event_t* e) {
        if (s_rf_pwr < 22) { s_rf_pwr++; char b[24]; snprintf(b, sizeof(b), "TX:%d", s_rf_pwr); lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), b); }
    }, LV_EVENT_CLICKED, (void*)pwr_lbl);
    ry += 28;

    // Save & Reboot
    auto* save_btn = lv_btn_create(scr);
    lv_obj_set_size(save_btn, rw, 28);
    lv_obj_align(save_btn, LV_ALIGN_TOP_LEFT, rx, ry);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(save_btn, 0, 0);
    auto* svl = lv_label_create(save_btn);
    lv_label_set_text(svl, LV_SYMBOL_SAVE "  Save & Reboot");
    lv_obj_center(svl);
    lv_obj_add_event_cb(save_btn, [](lv_event_t*) {
        slopos::NodePrefs np = slopos::prefs_get();
        np.freq         = s_rf_freq;
        np.bw           = s_rf_bw;
        np.sf           = (uint8_t)s_rf_sf;
        np.cr           = (uint8_t)s_rf_cr;
        np.tx_power_dbm = (int8_t)s_rf_pwr;
        np.configured   = true;
        slopos::prefs_set(np);
        slopos::mesh::saveChannels();
        chat_save_messages();
        ESP.restart();
    }, LV_EVENT_CLICKED, nullptr);

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Back-button highlight for back-swipe visual feedback
// ════════════════════════════════════════════════════════
void highlight_back_button(bool show)
{
    if (!s_back_btn) return;

    if (show) {
        lv_obj_set_style_border_width(s_back_btn, 2, 0);
        lv_obj_set_style_border_color(s_back_btn, lv_color_hex(theme::ACCENT), 0);
    } else {
        lv_obj_set_style_border_width(s_back_btn, 1, 0);
        lv_obj_set_style_border_color(s_back_btn, lv_color_hex(theme::DIVIDER), 0);
    }
}

void screens_clear_back_btn()
{
    s_back_btn = nullptr;
}

} // namespace slopos::ui
