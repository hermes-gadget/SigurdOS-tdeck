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
#include <math.h>
#include <functional>
#include <SPIFFS.h>

namespace sigurdos::ui {

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

    uint32_t last = sigurdos::mesh::getLastAdvertTime();
    uint32_t now = sigurdos::mesh::getCurrentTime();
    bool success = sigurdos::mesh::getLastAdvertSuccess();
    bool used_gps = sigurdos::mesh::getLastAdvertUsedGps();

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

    // Screen title (centered, visible now that channels are gone)
    if (title && title[0]) {
        lv_obj_t* ttl = lv_label_create(top);
        lv_label_set_text(ttl, title);
        lv_obj_set_style_text_color(ttl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(ttl, &lv_font_montserrat_10, 0);
        lv_obj_align(ttl, LV_ALIGN_CENTER, 0, 0);
    }

    // Time (24h snapshot)
    {
        uint32_t epoch = sigurdos::mesh::getCurrentTime();
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

    // Signal dots (right of top bar, iOS-style)
    {
        int rssi = sigurdos::mesh::getLastRSSI();
        lv_obj_t* sig = create_signal_dots(top, rssi);
        lv_obj_align(sig, LV_ALIGN_RIGHT_MID, -54, 0);
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
    lv_label_set_text(dev, sigurdos::mesh::getOwnName());
    lv_obj_set_style_text_color(dev, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dev, &lv_font_montserrat_10, 0);
    lv_obj_align(dev, LV_ALIGN_LEFT_MID, 4, 0);

    // Battery % (right, snapshot)
    {
        char batt[8];
        int pct = sigurdos_battery_pct();
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

    int n = sigurdos::mesh::getPacketLogCount();
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
static int contacts_filter_type = -1; // -1=all, else ADV_TYPE_*

void contacts_screen_set_filter(int adv_type) {
    contacts_filter_type = adv_type;
}

void contacts_screen_show()
{
    lv_obj_t* scr = make_screen_full("Contacts");

    sigurdos::mesh::ContactInfo all_contacts[32];
    int total = sigurdos::mesh::exportContactsFull(all_contacts, 32);

    // Filter based on mode
    int n = 0;
    for (int i = 0; i < total; i++) {
        bool keep = false;
        if (contacts_filter_type >= 0) {
            // Specific type filter (e.g. ROOMS → ADV_TYPE_ROOM only)
            keep = (all_contacts[i].type == contacts_filter_type);
        } else {
            // Default: companions (CHAT) and room servers (ROOM)
            keep = (all_contacts[i].type == ADV_TYPE_CHAT ||
                    all_contacts[i].type == ADV_TYPE_ROOM);
        }
        if (keep) {
            if (n < i) all_contacts[n] = all_contacts[i];
            n++;
        }
    }
    sigurdos::mesh::ContactInfo* contacts = all_contacts;

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
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
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

        // RSSI
        char rssi_buf[12];
        snprintf(rssi_buf, sizeof(rssi_buf), "%ddBm", c.rssi);
        lv_obj_t* rssi_l = lv_label_create(row);
        lv_label_set_text(rssi_l, rssi_buf);
        lv_obj_set_style_text_color(rssi_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(rssi_l, &lv_font_montserrat_10, 0);
        lv_obj_align(rssi_l, LV_ALIGN_RIGHT_MID, -6, 0);

        // SNR
        char snr_buf[12];
        snprintf(snr_buf, sizeof(snr_buf), "%.1fdB", c.snr);
        lv_obj_t* snr_l = lv_label_create(row);
        lv_label_set_text(snr_l, snr_buf);
        lv_obj_set_style_text_color(snr_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(snr_l, &lv_font_montserrat_10, 0);
        lv_obj_align(snr_l, LV_ALIGN_RIGHT_MID, -56, 0);

        // Store name + type for click handler
        // Room servers open the detail screen; chat nodes open DM
        struct ContactRowData { char* name; uint8_t type; };
        ContactRowData* row_data = new ContactRowData{strdup(c.name), c.type};
        lv_obj_set_user_data(row, row_data);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_current_target(e);
            ContactRowData* d = (ContactRowData*)lv_obj_get_user_data(target);
            if (d && d->name) {
                if (d->type == ADV_TYPE_ROOM) {
                    repeater_detail_screen_show(d->name);
                } else {
                    chat_screen_open_dm(d->name);
                }
            }
        }, LV_EVENT_CLICKED, nullptr);

        // Free the heap-allocated name copy when the row is deleted
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            ContactRowData* d = (ContactRowData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
            if (d) { free(d->name); delete d; }
        }, LV_EVENT_DELETE, nullptr);
    }

    show_screen(scr);
}

// Forward declaration of login-polling timer (defined after dialog functions)
static void start_login_poll_timer(const char* name);

// ── Login password dialog ─────────────────────────
// Shows a modal dialog for entering a password to log into a repeater/room server.
// Includes a "Save Password" checkbox that persists the password to NVS.
static void show_login_password_dialog(const char* contact_name)
{
    if (!contact_name) return;

    lv_obj_t* scr = lv_obj_get_screen(lv_scr_act());
    auto dlg_sz = dialog_size(240, 130);
    lv_obj_t* dlg = lv_obj_create(scr);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    // Title
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "Login to %s", contact_name);
    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // Password textarea
    lv_obj_t* ta = lv_textarea_create(dlg);
    lv_obj_set_size(ta, dlg_sz.w - 16, 28);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 26);
    lv_textarea_set_placeholder_text(ta, "Password (press Enter to submit)");
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    // Focus the textarea so keyboard input is routed here
    lv_group_t* g = lv_group_get_default();
    if (g) lv_group_focus_obj(ta);

    // "Save Password" checkbox
    lv_obj_t* save_cb = lv_checkbox_create(dlg);
    lv_checkbox_set_text(save_cb, "Save Password");
    lv_obj_align(save_cb, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_text_color(save_cb, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(save_cb, &lv_font_montserrat_10, 0);

    // Pre-fill password from NVS if saved
    char saved_pw[64] = {0};
    if (sigurdos::loadRepeaterPassword(contact_name, saved_pw, sizeof(saved_pw))) {
        lv_textarea_set_text(ta, saved_pw);
        lv_obj_add_state(save_cb, LV_STATE_CHECKED);
    }

    // Cancel button
    lv_obj_t* cancel_btn = lv_btn_create(dlg);
    lv_obj_set_size(cancel_btn, 80, 24);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -4);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_radius(cancel_btn, 0, 0);
    lv_obj_t* cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ce) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
    }, LV_EVENT_CLICKED, nullptr);

    // Login button
    char* pw_name = strdup(contact_name);
    lv_obj_t* login_btn = lv_btn_create(dlg);
    lv_obj_set_size(login_btn, 80, 24);
    lv_obj_align(login_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
    lv_obj_set_style_bg_color(login_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(login_btn, 0, 0);
    lv_obj_t* lb = lv_label_create(login_btn);
    lv_label_set_text(lb, "Login");
    lv_obj_center(lb);
    lv_obj_set_style_text_color(lb, lv_color_hex(BG_PRIMARY), 0);

    // Store references for the click handler
    struct PwDialogData { char* name; lv_obj_t* ta; lv_obj_t* save_cb; };
    PwDialogData* dd = new PwDialogData{pw_name, ta, save_cb};
    lv_obj_set_user_data(login_btn, dd);

    lv_obj_add_event_cb(login_btn, [](lv_event_t* le) {
        PwDialogData* d = (PwDialogData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(le));
        if (d && d->name) {
            const char* pw = lv_textarea_get_text(d->ta);
            if (pw && pw[0]) {
                sigurdos::mesh::sendLogin(d->name, pw);
                // Start a login-polling timer on the main screen
                start_login_poll_timer(d->name);
                // Save password to NVS if checkbox is checked
                if (d->save_cb && (lv_obj_get_state(d->save_cb) & LV_STATE_CHECKED)) {
                    sigurdos::saveRepeaterPassword(d->name, pw);
                }
            }
        }
        // Close dialog
        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(le));
        lv_obj_del_async(dlg);
    }, LV_EVENT_CLICKED, nullptr);

    // Add Enter-key handler to textarea (submit on Enter)
    lv_obj_add_event_cb(ta, [](lv_event_t* te) {
        lv_obj_t* t = (lv_obj_t*)lv_event_get_target(te);
        uint32_t key = lv_event_get_key(te);
        if (key == LV_KEY_ENTER) {
            const char* pwt = lv_textarea_get_text(t);
            if (pwt && pwt[0]) {
                lv_obj_t* parent = lv_obj_get_parent(t);
                if (parent) {
                    uint32_t c = lv_obj_get_child_cnt(parent);
                    for (uint32_t i = 0; i < c; i++) {
                        lv_obj_t* child = lv_obj_get_child(parent, i);
                        if (child && lv_obj_check_type(child, &lv_button_class)) {
                            PwDialogData* data = (PwDialogData*)lv_obj_get_user_data(child);
                            if (data) {
                                lv_obj_send_event(child, LV_EVENT_CLICKED, nullptr);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }, LV_EVENT_KEY, nullptr);

    // Cleanup on delete
    lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
        PwDialogData* d = (PwDialogData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de));
        if (d) {
            free(d->name);
            delete d;
        }
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_set_user_data(dlg, dd);
}

// ── Login status polling timer ────────────────────
// After a login attempt, this timer polls getLoginStatus() every 2 seconds.
// On LOGIN_OK → rebuilds the screen with the post-login (management) view.
// On LOGIN_FAILED → rebuilds pre-login so the failure text is visible.
// Cancels itself if the user navigates away.
struct LoginPollCtx {
    char* name;
    lv_obj_t* screen;

    uint32_t gen;        // matches g_login_poll_gen at creation time; stale if timer restarted
};
static lv_timer_t* g_login_poll_timer = nullptr;
static uint32_t g_login_poll_gen = 0;


static void on_login_poll_timer(lv_timer_t* t) {
    LoginPollCtx* ctx = (LoginPollCtx*)lv_timer_get_user_data(t);
    if (!ctx || !ctx->name) {
        lv_timer_del(t);
        if (g_login_poll_timer == t) g_login_poll_timer = nullptr;
        return;
    }


    // If a newer generation timer was started, this ctx is stale
    // If user navigated away from the screen, stop polling
    if (!ctx->screen || ctx->gen != g_login_poll_gen || lv_scr_act() != ctx->screen) {

        free(ctx->name);
        delete ctx;
        lv_timer_del(t);
        if (g_login_poll_timer == t) g_login_poll_timer = nullptr;
        return;
    }

    uint8_t st = sigurdos::mesh::getLoginStatus(ctx->name);
    if (st == LOGIN_STATUS_OK) {
        char* n = strdup(ctx->name);
        free(ctx->name);
        delete ctx;
        lv_timer_del(t);
        if (g_login_poll_timer == t) g_login_poll_timer = nullptr;
        // Rebuild screen in post-login mode
        repeater_detail_screen_show(n, true);
        free(n);
    } else if (st == LOGIN_STATUS_FAILED) {
        // Rebuild pre-login view so it shows "Login failed" and the Login button
        char* n = strdup(ctx->name);
        free(ctx->name);
        delete ctx;
        lv_timer_del(t);
        if (g_login_poll_timer == t) g_login_poll_timer = nullptr;
        repeater_detail_screen_show(n, false);
        free(n);
    }
    // LOGIN_PENDING or LOGIN_NONE → keep polling
}

static void start_login_poll_timer(const char* name) {
    if (!name) return;
    // Cancel any existing timer first
    if (g_login_poll_timer) {
        LoginPollCtx* old = (LoginPollCtx*)lv_timer_get_user_data(g_login_poll_timer);
        if (old) { free(old->name); delete old; }
        lv_timer_del(g_login_poll_timer);
        g_login_poll_timer = nullptr;
    }

    g_login_poll_gen++;
    LoginPollCtx* ctx = new LoginPollCtx{strdup(name), lv_scr_act(), g_login_poll_gen};

    g_login_poll_timer = lv_timer_create(on_login_poll_timer, 2000, ctx);
}

// ── Admin command dialog ──────────────────────────
// Shows a modal dialog for entering an admin CLI command to send to a logged-in server.
static void show_admin_cmd_dialog(const char* contact_name)
{
    if (!contact_name) return;

    // Clear stale responses from previous session
    sigurdos::mesh::clearCmdResponses();

    static constexpr int TERM_TOP_H    = TOP_BAR_H;         // 21
    static constexpr int TERM_INPUT_H  = 28;
    static constexpr int TERM_OUTPUT_H = DISPLAY_H - TERM_TOP_H - DIVIDER_H - TERM_INPUT_H;  // 190

    lv_obj_t* scr = lv_obj_get_screen(lv_scr_act());
    lv_obj_t* dlg = lv_obj_create(scr);
    lv_obj_set_size(dlg, LV_PCT(100), LV_PCT(100));
    lv_obj_align(dlg, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 0, 0);

    // ── Top bar (matches make_screen_full) ──────
    lv_obj_t* top = lv_obj_create(dlg);
    lv_obj_set_size(top, LV_PCT(100), TERM_TOP_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);

    lv_obj_t* title = lv_label_create(top);
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "> Admin: %s", contact_name);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_color(title, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    // Close button (top-right)
    lv_obj_t* close_btn = lv_btn_create(top);
    lv_obj_set_size(close_btn, 20, TERM_TOP_H - 2);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(close_btn, 0, 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* x_lbl = lv_label_create(close_btn);
    lv_label_set_text(x_lbl, LV_SYMBOL_CLOSE);
    lv_obj_center(x_lbl);
    lv_obj_set_style_text_color(x_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* top = lv_obj_get_parent(btn);
        lv_obj_t* dlg = lv_obj_get_parent(top);
        lv_obj_del_async(dlg);
    }, LV_EVENT_CLICKED, nullptr);

    // Divider below top bar
    lv_obj_t* tdiv = lv_obj_create(dlg);
    lv_obj_set_size(tdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(tdiv, LV_ALIGN_TOP_MID, 0, TERM_TOP_H);
    lv_obj_set_style_bg_color(tdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(tdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tdiv, 0, 0);

    // ── Output area (scrollable, read-only, pure black) ─
    lv_obj_t* out = lv_textarea_create(dlg);
    lv_obj_set_size(out, LV_PCT(100), TERM_OUTPUT_H);
    lv_obj_align(out, LV_ALIGN_TOP_MID, 0, TERM_TOP_H + DIVIDER_H);
    lv_obj_set_style_bg_color(out, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(out, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(out, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(out, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(out, 0, 0);
    lv_obj_set_style_border_width(out, 0, 0);
    lv_obj_set_style_pad_all(out, 4, 0);
    lv_textarea_set_cursor_click_pos(out, false);
    lv_textarea_set_max_length(out, 4096);  // prevent unbounded growth

    // Welcome banner
    lv_textarea_add_text(out, "-- Admin Terminal --\n");
    lv_textarea_add_text(out, "Commands: ver, reboot, password,\n");
    lv_textarea_add_text(out, "set freq, get name, etc.\n");

    // Divider above input
    lv_obj_t* bdiv = lv_obj_create(dlg);
    lv_obj_set_size(bdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(bdiv, LV_ALIGN_BOTTOM_MID, 0, -TERM_INPUT_H);
    lv_obj_set_style_bg_color(bdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(bdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bdiv, 0, 0);

    // ── Input row (full-width flex) ──────────────
    lv_obj_t* input_row = lv_obj_create(dlg);
    lv_obj_set_size(input_row, LV_PCT(100), TERM_INPUT_H);
    lv_obj_align(input_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_row, 0, 0);
    lv_obj_set_style_pad_all(input_row, 2, 0);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* ta = lv_textarea_create(input_row);
    lv_obj_set_size(ta, LV_PCT(80), TERM_INPUT_H - 4);
    lv_textarea_set_placeholder_text(ta, "> command...");
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 240);  // prevent buffer overflow in echo buffer
    lv_obj_set_style_bg_color(ta, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_group_t* g = lv_group_get_default();
    if (g) lv_group_focus_obj(ta);

    // Store contact name + widget handles
    char* cmd_name = strdup(contact_name);
    struct TermData { char* name; lv_obj_t* ta; lv_obj_t* out; lv_timer_t* timer; bool deleted; };
    TermData* td = new TermData{cmd_name, ta, out, nullptr, false};
    lv_obj_set_user_data(dlg, td);

    // ── Send button ──────────────────────────────
    lv_obj_t* send_btn = lv_btn_create(input_row);
    lv_obj_set_size(send_btn, LV_PCT(18), TERM_INPUT_H - 4);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(send_btn, 0, 0);
    lv_obj_set_style_pad_all(send_btn, 0, 0);
    lv_obj_t* sb = lv_label_create(send_btn);
    lv_label_set_text(sb, "Send");
    lv_obj_center(sb);
    lv_obj_set_style_text_color(sb, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_user_data(send_btn, td);
    lv_obj_add_event_cb(send_btn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        TermData* d = (TermData*)lv_obj_get_user_data(btn);
        if (d) {
            auto send = [](TermData* dd) {
                if (!dd || !dd->name) return;
                const char* cmd = lv_textarea_get_text(dd->ta);
                if (!cmd || !cmd[0]) return;
                char echo[256];
                snprintf(echo, sizeof(echo), "\n> %s\n", cmd);
                lv_textarea_add_text(dd->out, echo);
                bool ok = sigurdos::mesh::sendCommand(dd->name, cmd);
                lv_textarea_set_text(dd->ta, "");
                if (lv_group_get_default()) lv_group_focus_obj(dd->ta);
                if (!ok) lv_textarea_add_text(dd->out, "! Send failed\n");
                if (!dd->timer) {
                    dd->timer = lv_timer_create([](lv_timer_t* t) {
                        TermData* dd2 = (TermData*)lv_timer_get_user_data(t);
                        if (!dd2 || dd2->deleted) return;
                        char nb[32], tb[160];
                        while (sigurdos::mesh::pollCmdResponse(nb, sizeof(nb), tb, sizeof(tb))) {
                            if (dd2->deleted) return;
                            if (dd2->name && strcmp(nb, dd2->name) == 0) {
                                char rb[256];
                                snprintf(rb, sizeof(rb), "< %s\n", tb);
                                // Trim output if near max to prevent overflow
                                const char* full = lv_textarea_get_text(dd2->out);
                                if (full && strlen(full) > 3500) {
                                    const char* mid = full + 1500;
                                    while (*mid && *mid != '\n') mid++;
                                    if (*mid) mid++;
                                    char mid_copy[2048];
                                    size_t copy_len = strlen(mid);
                                    if (copy_len >= sizeof(mid_copy)) copy_len = sizeof(mid_copy) - 1;
                                    memcpy(mid_copy, mid, copy_len);
                                    mid_copy[copy_len] = '\0';
                                    lv_textarea_set_text(dd2->out, "(...trimmed...)\n");
                                    lv_textarea_add_text(dd2->out, mid_copy);
                                }
                                lv_textarea_add_text(dd2->out, rb);
                            }
                        }
                    }, 500, dd);
                }
            };
            send(d);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // ── Enter key on textarea ────────────────────
    lv_obj_add_event_cb(ta, [](lv_event_t* te) {
        uint32_t key = lv_event_get_key(te);
        if (key == LV_KEY_ENTER) {
            lv_obj_t* t = (lv_obj_t*)lv_event_get_target(te);
            lv_obj_t* row = lv_obj_get_parent(t);
            uint32_t c = lv_obj_get_child_cnt(row);
            for (uint32_t i = 0; i < c; i++) {
                lv_obj_t* child = lv_obj_get_child(row, i);
                if (child && lv_obj_check_type(child, &lv_button_class)) {
                    TermData* d = (TermData*)lv_obj_get_user_data(child);
                    if (d) {
                        lv_obj_send_event(child, LV_EVENT_CLICKED, nullptr);
                        break;
                    }
                }
            }
        }
    }, LV_EVENT_KEY, nullptr);

    // ── Cleanup ──────────────────────────────────
    lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
        TermData* d = (TermData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de));
        if (d) {
            d->deleted = true;  // prevent timer callback from using members
            if (d->timer) lv_timer_del(d->timer);
            if (d->name) free(d->name);
            delete d;
        }
        sigurdos::mesh::clearCmdResponses();
    }, LV_EVENT_DELETE, nullptr);
}

// Public test hook — callable from test_controller
void admin_cmd_show(const char* contact_name) {
    show_admin_cmd_dialog(contact_name);
}

// ── Room message fetch dialog (Phase 4.6) ──────────
// Shows a modal dialog for entering a channel name to fetch messages from.
static void show_fetch_msgs_dialog(const char* contact_name)
{
    if (!contact_name) return;

    lv_obj_t* scr = lv_obj_get_screen(lv_scr_act());
    auto dlg_sz = dialog_size(260, 124);
    lv_obj_t* dlg = lv_obj_create(scr);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    // Title
    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Fetch room messages");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // Hint
    lv_obj_t* hint = lv_label_create(dlg);
    lv_label_set_text(hint, "Enter channel name (e.g. #general)");
    lv_obj_set_style_text_color(hint, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 24);

    // Channel name textarea
    lv_obj_t* ta = lv_textarea_create(dlg);
    lv_obj_set_size(ta, dlg_sz.w - 16, 28);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 38);
    lv_textarea_set_placeholder_text(ta, "#channel");
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_group_t* g = lv_group_get_default();
    if (g) lv_group_focus_obj(ta);

    // Cancel button
    lv_obj_t* cancel_btn = lv_btn_create(dlg);
    lv_obj_set_size(cancel_btn, 80, 24);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -4);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_radius(cancel_btn, 0, 0);
    lv_obj_t* cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ce) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
    }, LV_EVENT_CLICKED, nullptr);

    // Fetch button
    struct FetchDialogData { char* name; lv_obj_t* ta; };
    char* fm_name = strdup(contact_name);
    FetchDialogData* fd = new FetchDialogData{fm_name, ta};
    lv_obj_t* fetch_btn = lv_btn_create(dlg);
    lv_obj_set_size(fetch_btn, 80, 24);
    lv_obj_align(fetch_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
    lv_obj_set_style_bg_color(fetch_btn, lv_color_hex(0x0088cc), 0);
    lv_obj_set_style_radius(fetch_btn, 0, 0);
    lv_obj_t* fb = lv_label_create(fetch_btn);
    lv_label_set_text(fb, "Fetch");
    lv_obj_center(fb);
    lv_obj_set_style_text_color(fb, lv_color_hex(0xffffff), 0);
    lv_obj_set_user_data(fetch_btn, fd);

    lv_obj_add_event_cb(fetch_btn, [](lv_event_t* le) {
        FetchDialogData* d = (FetchDialogData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(le));
        if (d && d->name) {
            const char* channel = lv_textarea_get_text(d->ta);
            if (channel && channel[0]) {
                sigurdos::mesh::sendRoomMsgFetchRequest(d->name, channel);
                char confirm[64];
                snprintf(confirm, sizeof(confirm), "Fetching msgs from %s channel %s",
                         d->name, channel);
                sigurdos::mesh::mesh_v2_queue_push("System", "", confirm, 0, 0.0f);
                // Navigate to Chat screen so user sees incoming messages
                sigurdos::ui::navigate_to(sigurdos::ui::Screen::Chat);
            }
        }
        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(le));
        lv_obj_del_async(dlg);
    }, LV_EVENT_CLICKED, nullptr);

    // Enter-key handler on textarea
    lv_obj_add_event_cb(ta, [](lv_event_t* te) {
        lv_obj_t* t = (lv_obj_t*)lv_event_get_target(te);
        uint32_t key = lv_event_get_key(te);
        if (key == LV_KEY_ENTER) {
            lv_obj_t* parent = lv_obj_get_parent(t);
            if (parent) {
                uint32_t c = lv_obj_get_child_cnt(parent);
                for (uint32_t i = 0; i < c; i++) {
                    lv_obj_t* child = lv_obj_get_child(parent, i);
                    if (child && lv_obj_check_type(child, &lv_button_class)) {
                        FetchDialogData* data = (FetchDialogData*)lv_obj_get_user_data(child);
                        if (data) {
                            lv_obj_send_event(child, LV_EVENT_CLICKED, nullptr);
                            break;
                        }
                    }
                }
            }
        }
    }, LV_EVENT_KEY, nullptr);

    // Cleanup
    lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
        FetchDialogData* d = (FetchDialogData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de));
        if (d) {
            free(d->name);
            delete d;
        }
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_set_user_data(dlg, fd);
}

// ════════════════════════════════════════════════════════
// Contact Detail — full info about a single contact
// ════════════════════════════════════════════════════════
void contact_detail_screen_show(const char* contact_name)
{
    if (!contact_name || !contact_name[0]) {
        Serial.println("[ui] contact_detail: empty name");
        return;
    }

    lv_obj_t* scr = make_screen_full("Contact");

    // Look up the contact via exportContactsFull
    sigurdos::mesh::ContactInfo contacts[64];
    int total = sigurdos::mesh::exportContactsFull(contacts, 64);
    const sigurdos::mesh::ContactInfo* target = nullptr;
    for (int i = 0; i < total; i++) {
        if (strcmp(contacts[i].name, contact_name) == 0) {
            target = &contacts[i];
            break;
        }
    }
    if (!target) {
        lv_obj_t* err = lv_label_create(scr);
        lv_label_set_text(err, "Contact not found");
        lv_obj_set_style_text_color(err, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_text_font(err, &lv_font_montserrat_12, 0);
        lv_obj_align(err, LV_ALIGN_CENTER, 0, 0);
        show_screen(scr);
        return;
    }

    // Content area — scrollable vertical column for all fields
    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - 66);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    // Helper: add a labeled info row
    auto add_row = [&](const char* label, const char* value, uint32_t color) {
        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_PCT(100), 22);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_left(row, 8, 0);
        lv_obj_set_style_pad_right(row, 8, 0);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);

        lv_obj_t* val = lv_label_create(row);
        lv_label_set_text(val, value);
        lv_obj_set_style_text_color(val, lv_color_hex(color), 0);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -8, 0);
    };

    // Name (large, top)
    lv_obj_t* name_l = lv_label_create(list);
    lv_label_set_text(name_l, target->name);
    lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(name_l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_left(name_l, 8, 0);
    lv_obj_set_style_pad_bottom(name_l, 4, 0);

    // Node type
    const char* type_str = "Unknown";
    switch (target->type) {
        case ADV_TYPE_CHAT:     type_str = "Chat Node"; break;
        case ADV_TYPE_REPEATER: type_str = "Repeater"; break;
        case ADV_TYPE_ROOM:     type_str = "Room Server"; break;
        case ADV_TYPE_SENSOR:   type_str = "Sensor"; break;
    }
    add_row("Type", type_str, ACCENT);

    // RSSI
    char rssi_buf[16];
    snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm", target->rssi);
    add_row("RSSI", rssi_buf, TEXT_PRIMARY);

    // SNR
    char snr_buf[16];
    snprintf(snr_buf, sizeof(snr_buf), "%.1f dB", target->snr);
    add_row("SNR", snr_buf, TEXT_PRIMARY);

    // Last seen
    if (target->last_seen > 0) {
        // Get contract index to look up path info later
        int cidx = sigurdos::mesh::findContactIndex(contact_name);
        char time_buf[24];
        time_t t = (time_t)target->last_seen;
        struct tm* tm_info = localtime(&t);
        if (tm_info) {
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm_info);
            add_row("Last Seen", time_buf, TEXT_PRIMARY);
        } else {
            add_row("Last Seen", "?", TEXT_SECONDARY);
        }

        // Show path status
        if (cidx >= 0 && sigurdos::mesh::contactHasPath(cidx)) {
            add_row("Path", "Direct", ACCENT_GREEN);
        } else if (cidx >= 0) {
            add_row("Path", "Flood", TEXT_SECONDARY);
        }
    } else {
        add_row("Last Seen", "N/A", TEXT_SECONDARY);
    }

    // GPS location
    if (target->has_location) {
        char loc_buf[48];
        snprintf(loc_buf, sizeof(loc_buf), "%.4f, %.4f",
                 target->latitude, target->longitude);
        add_row("Location", loc_buf, ACCENT_GREEN);
    } else {
        add_row("Location", "Not shared", TEXT_SECONDARY);
    // ── Login status row (repeater/room only) ─────────
    if (target->type == ADV_TYPE_REPEATER || target->type == ADV_TYPE_ROOM) {
        uint8_t st = sigurdos::mesh::getLoginStatus(contact_name);
        const char* login_text = "Not logged in";
        uint32_t login_color = TEXT_SECONDARY;
        switch (st) {
            case LOGIN_STATUS_PENDING: login_text = "Login pending..."; login_color = ACCENT; break;
            case LOGIN_STATUS_OK:      login_text = "Logged in";        login_color = ACCENT_GREEN; break;
            case LOGIN_STATUS_FAILED:  login_text = "Login failed";     login_color = ACCENT_RED; break;
        }
        add_row("Login", login_text, login_color);

        // When logged in, show permission level
        if (st == LOGIN_STATUS_OK) {
            uint8_t perm = sigurdos::mesh::getLoginPermission(contact_name);
            char perm_buf[24];
            if (perm) {
                snprintf(perm_buf, sizeof(perm_buf), "Admin (perm=%d)", perm);
            } else {
                snprintf(perm_buf, sizeof(perm_buf), "Guest");
            }
            add_row("Permission", perm_buf, ACCENT);
        }
    }

    }

    bool is_room_type = (target->type == ADV_TYPE_ROOM || target->type == ADV_TYPE_REPEATER);

    // ── Action button row ───────────────────────────
    lv_obj_t* btn_row = lv_obj_create(scr);
    lv_obj_set_size(btn_row, CONTENT_W, 30);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_LEFT, 0, -(BOT_BAR_H + DIVIDER_H));
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Send DM button (skip for room/repeater — you don't DM a server)
    if (!is_room_type) {
        lv_obj_t* dm_btn = lv_btn_create(btn_row);
        lv_obj_set_size(dm_btn, 110, 24);
        lv_obj_set_style_bg_color(dm_btn, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_radius(dm_btn, 0, 0);
        lv_obj_t* dm_lbl = lv_label_create(dm_btn);
        lv_label_set_text(dm_lbl, LV_SYMBOL_ENVELOPE " DM");
        lv_obj_center(dm_lbl);
        lv_obj_set_style_text_color(dm_lbl, lv_color_hex(BG_PRIMARY), 0);
        char* dm_name = strdup(contact_name);
        lv_obj_set_user_data(dm_btn, dm_name);
        lv_obj_add_event_cb(dm_btn, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(btn);
            if (name) {
                sigurdos::ui::chat_screen_open_dm(name);
            }
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(dm_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // Send Trace button (skip for room/repeater)
    int trace_idx = sigurdos::mesh::findContactIndex(contact_name);
    if (!is_room_type && trace_idx >= 0) {
        lv_obj_t* trace_btn = lv_btn_create(btn_row);
        lv_obj_set_size(trace_btn, 110, 24);
        lv_obj_set_style_bg_color(trace_btn, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_radius(trace_btn, 0, 0);
        lv_obj_t* trace_lbl = lv_label_create(trace_btn);
        lv_label_set_text(trace_lbl, LV_SYMBOL_SHUFFLE " Trace");
        lv_obj_center(trace_lbl);
        lv_obj_set_style_text_color(trace_lbl, lv_color_hex(TEXT_PRIMARY), 0);
        // Store contact_idx in user_data (value fits in intptr_t, no heap alloc needed)
        lv_obj_set_user_data(trace_btn, (void*)(intptr_t)trace_idx);
        lv_obj_add_event_cb(trace_btn, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
            if (idx >= 0) {
                uint32_t tag = 0;
                sigurdos::mesh::sendTrace(idx, &tag);
                // Brief snackbar-style feedback — navigate to trace screen
                // so the user can see the result
                sigurdos::ui::navigate_to(sigurdos::ui::Screen::Trace);
            }
        }, LV_EVENT_CLICKED, nullptr);
        // No LV_EVENT_DELETE handler needed — no heap allocation
    }

    // Request Status button
    {
        lv_obj_t* st_btn = lv_btn_create(btn_row);
        lv_obj_set_size(st_btn, 75, 24);
        lv_obj_set_style_bg_color(st_btn, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_radius(st_btn, 0, 0);
        lv_obj_t* st_lbl = lv_label_create(st_btn);
        lv_label_set_text(st_lbl, LV_SYMBOL_SETTINGS " Status");
        lv_obj_center(st_lbl);
        lv_obj_set_style_text_color(st_lbl, lv_color_hex(TEXT_PRIMARY), 0);
        char* st_name = strdup(contact_name);
        lv_obj_set_user_data(st_btn, st_name);
        lv_obj_add_event_cb(st_btn, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(btn);
            if (name) {
                sigurdos::mesh::requestStatus(name);
                sigurdos::ui::navigate_to(sigurdos::ui::Screen::NodeStatus);
            }
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(st_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // Request Telemetry button — second row below main buttons (skip for room/repeater)
    if (!is_room_type) {
        lv_obj_t* tm_row = lv_obj_create(scr);
        lv_obj_set_size(tm_row, CONTENT_W, 26);
        lv_obj_align(tm_row, LV_ALIGN_BOTTOM_LEFT, 0, -(BOT_BAR_H + DIVIDER_H + 28));
        lv_obj_set_style_bg_opa(tm_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tm_row, 0, 0);
        lv_obj_set_flex_flow(tm_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tm_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* tm_btn = lv_btn_create(tm_row);
        lv_obj_set_size(tm_btn, 180, 22);
        lv_obj_set_style_bg_color(tm_btn, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_radius(tm_btn, 0, 0);
        lv_obj_t* tm_lbl = lv_label_create(tm_btn);
        lv_label_set_text(tm_lbl, LV_SYMBOL_WIFI " Telemetry");
        lv_obj_center(tm_lbl);
        lv_obj_set_style_text_color(tm_lbl, lv_color_hex(TEXT_PRIMARY), 0);
        char* tm_name = strdup(contact_name);
        lv_obj_set_user_data(tm_btn, tm_name);
        lv_obj_add_event_cb(tm_btn, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(btn);
            if (name) {
                sigurdos::mesh::requestTelemetry(name);
                sigurdos::ui::navigate_to(sigurdos::ui::Screen::Telemetry);
            }
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(tm_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // Remove Contact button
    {
        const char* remove_name = contact_name;
        lv_obj_t* remove_btn = lv_btn_create(btn_row);
        lv_obj_set_size(remove_btn, 110, 24);
        lv_obj_set_style_bg_color(remove_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(remove_btn, 0, 0);
        lv_obj_t* rl = lv_label_create(remove_btn);
        lv_label_set_text(rl, LV_SYMBOL_CLOSE " Remove");
        lv_obj_set_style_text_color(rl, lv_color_hex(0xffffff), 0);
        lv_obj_center(rl);
        char* name_dup = strdup(remove_name);
        lv_obj_set_user_data(remove_btn, name_dup);
        lv_obj_add_event_cb(remove_btn, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(target);
            if (!name) return;
            lv_obj_t* scr = lv_obj_get_screen(target);
            auto dlg_sz = dialog_size(220, 100);
            lv_obj_t* dlg = lv_obj_create(scr);
            lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
            lv_obj_center(dlg);
            lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
            lv_obj_set_style_radius(dlg, 0, 0);
            lv_obj_set_style_border_width(dlg, 0, 0);
            lv_obj_set_style_pad_all(dlg, 8, 0);

            lv_obj_t* title = lv_label_create(dlg);
            lv_label_set_text(title, "Remove contact?");
            lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
            lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

            lv_obj_t* msg = lv_label_create(dlg);
            char msg_buf[64];
            snprintf(msg_buf, sizeof(msg_buf), "Remove %s from contacts?", name);
            lv_label_set_text(msg, msg_buf);
            lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(msg, &lv_font_montserrat_10, 0);
            lv_obj_align(msg, LV_ALIGN_CENTER, 0, -4);

            lv_obj_t* cancel_btn = lv_btn_create(dlg);
            lv_obj_set_size(cancel_btn, 64, 24);
            lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -4);
            lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_INPUT), 0);
            lv_obj_set_style_radius(cancel_btn, 0, 0);
            lv_obj_t* cl = lv_label_create(cancel_btn);
            lv_label_set_text(cl, "Cancel");
            lv_obj_center(cl);
            lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ce) {
                lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
            }, LV_EVENT_CLICKED, nullptr);

            char* cn = strdup(name);
            lv_obj_t* confirm_btn = lv_btn_create(dlg);
            lv_obj_set_size(confirm_btn, 64, 24);
            lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
            lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
            lv_obj_set_style_radius(confirm_btn, 0, 0);
            lv_obj_t* cfl_lb = lv_label_create(confirm_btn);
            lv_label_set_text(cfl_lb, "Remove");
            lv_obj_center(cfl_lb);
            lv_obj_set_user_data(confirm_btn, cn);
            lv_obj_add_event_cb(confirm_btn, [](lv_event_t* ce) {
                const char* cn = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ce));
                sigurdos::mesh::removeContact(cn);
                go_back();
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_user_data(dlg, cn);
            lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de)));
            }, LV_EVENT_DELETE, nullptr);
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(remove_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // Reset Path + Discover Path buttons (second action row) — skip for room/repeater
    if (!is_room_type) {
        lv_obj_t* btn_row2 = lv_obj_create(scr);
        lv_obj_set_size(btn_row2, CONTENT_W, 30);
        lv_obj_align(btn_row2, LV_ALIGN_BOTTOM_LEFT, 0, -(BOT_BAR_H + DIVIDER_H + 32));
        lv_obj_set_style_bg_opa(btn_row2, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_row2, 0, 0);
        lv_obj_set_flex_flow(btn_row2, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Reset Path button
        char* rp_name = strdup(contact_name);
        lv_obj_t* rp_btn = lv_btn_create(btn_row2);
        lv_obj_set_size(rp_btn, 140, 24);
        lv_obj_set_style_bg_color(rp_btn, lv_color_hex(ACCENT_ORANGE), 0);
        lv_obj_set_style_radius(rp_btn, 0, 0);
        lv_obj_t* rp_lbl = lv_label_create(rp_btn);
        lv_label_set_text(rp_lbl, LV_SYMBOL_REFRESH " Reset Path");
        lv_obj_set_style_text_color(rp_lbl, lv_color_hex(0xffffff), 0);
        lv_obj_center(rp_lbl);
        lv_obj_set_user_data(rp_btn, rp_name);
        lv_obj_add_event_cb(rp_btn, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(target);
            if (name) {
                sigurdos::mesh::resetPathTo(name);
            }
            lv_timer_create([](lv_timer_t* t) {
                go_back();
                lv_timer_del(t);
            }, 800, nullptr);
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(rp_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);

        // Discover Path button
        char* dp_name = strdup(contact_name);
        lv_obj_t* dp_btn = lv_btn_create(btn_row2);
        lv_obj_set_size(dp_btn, 140, 24);
        lv_obj_set_style_bg_color(dp_btn, lv_color_hex(0x0088cc), 0);
        lv_obj_set_style_radius(dp_btn, 0, 0);
        lv_obj_t* dp_lbl = lv_label_create(dp_btn);
        lv_label_set_text(dp_lbl, LV_SYMBOL_DIRECTORY " Discover");
        lv_obj_set_style_text_color(dp_lbl, lv_color_hex(0xffffff), 0);
        lv_obj_center(dp_lbl);
        lv_obj_set_user_data(dp_btn, dp_name);
        lv_obj_add_event_cb(dp_btn, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(target);
            if (name) {
                sigurdos::mesh::discoverPath(name);
            }
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(dp_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }


    // ── Login/Logout/Admin Cmd buttons (repeater/room) ──
    if (target->type == ADV_TYPE_REPEATER || target->type == ADV_TYPE_ROOM) {
        uint8_t login_st = sigurdos::mesh::getLoginStatus(contact_name);
        lv_obj_t* login_row = lv_obj_create(scr);
        lv_obj_set_size(login_row, CONTENT_W, 30);
        // Place the login row above the action btn_row so it's always visible.
        // btn_row is at -(BOT_BAR_H + DIVIDER_H) from bottom. Place above it.
        lv_obj_align(login_row, LV_ALIGN_BOTTOM_LEFT, 0, -(BOT_BAR_H + DIVIDER_H + 36));
        lv_obj_set_style_bg_opa(login_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(login_row, 0, 0);
        lv_obj_set_flex_flow(login_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(login_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        if (login_st == LOGIN_STATUS_OK) {
            // ── Admin Cmd button ──
            char* ac_name = strdup(contact_name);
            lv_obj_t* ac_btn = lv_btn_create(login_row);
            lv_obj_set_size(ac_btn, 150, 24);
            lv_obj_set_style_bg_color(ac_btn, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_radius(ac_btn, 0, 0);
            lv_obj_t* ac_lbl = lv_label_create(ac_btn);
            lv_label_set_text(ac_lbl, LV_SYMBOL_SETTINGS " Admin Cmd");
            lv_obj_center(ac_lbl);
            lv_obj_set_style_text_color(ac_lbl, lv_color_hex(BG_PRIMARY), 0);
            lv_obj_set_user_data(ac_btn, ac_name);
            lv_obj_add_event_cb(ac_btn, [](lv_event_t* e) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(btn);
                if (name) show_admin_cmd_dialog(name);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(ac_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);

            // ── Logout button ──
            char* lo_name = strdup(contact_name);
            lv_obj_t* lo_btn = lv_btn_create(login_row);
            lv_obj_set_size(lo_btn, 110, 24);
            lv_obj_set_style_bg_color(lo_btn, lv_color_hex(ACCENT_ORANGE), 0);
            lv_obj_set_style_radius(lo_btn, 0, 0);
            lv_obj_t* lo_lbl = lv_label_create(lo_btn);
            lv_label_set_text(lo_lbl, LV_SYMBOL_REFRESH " Logout");
            lv_obj_center(lo_lbl);
            lv_obj_set_style_text_color(lo_lbl, lv_color_hex(0xffffff), 0);
            lv_obj_set_user_data(lo_btn, lo_name);
            lv_obj_add_event_cb(lo_btn, [](lv_event_t* e) {
                lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(target);
                if (name) {
                    sigurdos::mesh::sendLogout(name);
                    lv_timer_create([](lv_timer_t* t) {
                        go_back();
                        lv_timer_del(t);
                    }, 600, nullptr);
                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(lo_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);

            // ── Fetch Msgs row (second row of buttons) ──
            lv_obj_t* fetch_row = lv_obj_create(scr);
            lv_obj_set_size(fetch_row, CONTENT_W, 30);
            lv_obj_align(fetch_row, LV_ALIGN_BOTTOM_LEFT, 0, -(BOT_BAR_H + DIVIDER_H + 126));
            lv_obj_set_style_bg_opa(fetch_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(fetch_row, 0, 0);
            lv_obj_set_flex_flow(fetch_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(fetch_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // ── Fetch Msgs button ──
            char* fm_name = strdup(contact_name);
            lv_obj_t* fm_btn = lv_btn_create(fetch_row);
            lv_obj_set_size(fm_btn, 180, 24);
            lv_obj_set_style_bg_color(fm_btn, lv_color_hex(0x0088cc), 0);
            lv_obj_set_style_radius(fm_btn, 0, 0);
            lv_obj_t* fm_lbl = lv_label_create(fm_btn);
            lv_label_set_text(fm_lbl, LV_SYMBOL_LIST " Fetch Msgs");
            lv_obj_center(fm_lbl);
            lv_obj_set_style_text_color(fm_lbl, lv_color_hex(0xffffff), 0);
            lv_obj_set_user_data(fm_btn, fm_name);
            lv_obj_add_event_cb(fm_btn, [](lv_event_t* e) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(btn);
                if (name) {
                    show_fetch_msgs_dialog(name);
                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(fm_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);

        } else {
            // ── Login button ──
            char* li_name = strdup(contact_name);
            lv_obj_t* li_btn = lv_btn_create(login_row);
            lv_obj_set_size(li_btn, 150, 24);
            lv_obj_set_style_bg_color(li_btn, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_radius(li_btn, 0, 0);
            lv_obj_t* li_lbl = lv_label_create(li_btn);
            lv_label_set_text(li_lbl, LV_SYMBOL_DIRECTORY " Login");
            lv_obj_center(li_lbl);
            lv_obj_set_style_text_color(li_lbl, lv_color_hex(BG_PRIMARY), 0);
            lv_obj_set_user_data(li_btn, li_name);
            lv_obj_add_event_cb(li_btn, [](lv_event_t* e) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(btn);
                if (name) show_login_password_dialog(name);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(li_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);

            // When login pending or failed, show Cancel button
            if (login_st == LOGIN_STATUS_PENDING || login_st == LOGIN_STATUS_FAILED) {
                char* cx_name = strdup(contact_name);
                lv_obj_t* cx_btn = lv_btn_create(login_row);
                lv_obj_set_size(cx_btn, 100, 24);
                lv_obj_set_style_bg_color(cx_btn, lv_color_hex(BG_TERTIARY), 0);
                lv_obj_set_style_radius(cx_btn, 0, 0);
                lv_obj_t* cx_lbl = lv_label_create(cx_btn);
                lv_label_set_text(cx_lbl, LV_SYMBOL_CLOSE " Cancel");
                lv_obj_center(cx_lbl);
                lv_obj_set_style_text_color(cx_lbl, lv_color_hex(TEXT_SECONDARY), 0);
                lv_obj_set_user_data(cx_btn, cx_name);
                lv_obj_add_event_cb(cx_btn, [](lv_event_t* ce) {
                    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(ce);
                    const char* n = (const char*)lv_obj_get_user_data(t);
                    if (n) {
                        sigurdos::mesh::sendLogout(n); // clears pending/failed state
                        go_back();
                    }
                }, LV_EVENT_CLICKED, nullptr);
                lv_obj_add_event_cb(cx_btn, [](lv_event_t* ce) {
                    free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ce)));
                }, LV_EVENT_DELETE, nullptr);
            }
        }
    }

    show_screen(scr);
}
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
            snprintf(buf, sizeof(buf), "%s  %ddBm", r->name, r->rssi);
            lv_obj_t* item = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
            lv_obj_set_style_bg_color(item,
                lv_color_hex(row_n % 2 == 1 ? BG_TERTIARY : BG_INPUT), 0);
        }
    }

    // ── Repeaters from contact list ────────────────
    sigurdos::mesh::ContactInfo contacts[32];
    int total = sigurdos::mesh::exportContactsFull(contacts, 32);
    if (total > 32) total = 32;
    if (total < 0) total = 0;

    // Build a set of ping responder names so we don't double-show
    bool is_ping_responder[32] = {};
    if (have_ping) {
        int np = sigurdos::mesh::getPingResultCount();
        for (int i = 0; i < np && i < 32; i++) {
            auto* r = sigurdos::mesh::getPingResult(i);
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

    sigurdos::mesh::ContactInfo contacts[32];
    int total = sigurdos::mesh::exportContactsFull(contacts, 32);

    // Filter to repeaters only
    int n = 0;
    for (int i = 0; i < total; i++) {
        if (contacts[i].type == ADV_TYPE_REPEATER) {
            if (n < i) contacts[n] = contacts[i];
            n++;
        }
    }

    // Sort by last_seen descending (most recently heard first)
    if (n > 1) {
        for (int i = 0; i < n - 1; i++) {
            for (int j = 0; j < n - 1 - i; j++) {
                if (contacts[j].last_seen < contacts[j + 1].last_seen) {
                    sigurdos::mesh::ContactInfo tmp = contacts[j];
                    contacts[j] = contacts[j + 1];
                    contacts[j + 1] = tmp;
                }
            }
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
        lv_obj_set_style_bg_color(row,
            lv_color_hex(ACCENT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

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

        // Store name for click handler
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

        // SNR
        char snr_buf[12];
        snprintf(snr_buf, sizeof(snr_buf), "%.1fdB", c.snr);
        lv_obj_t* snr_l = lv_label_create(row);
        lv_label_set_text(snr_l, snr_buf);
        lv_obj_set_style_text_color(snr_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(snr_l, &lv_font_montserrat_10, 0);
        lv_obj_align(snr_l, LV_ALIGN_RIGHT_MID, -56, 0);

        // Click handler — open dedicated repeater detail with login flow
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(target);
            if (name) {
                repeater_detail_screen_show(name);
            }
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    show_screen(scr);
}

// ── Repeater command helpers ───────────────────────────
// Sends an admin CLI command to a logged-in repeater/server and pushes
// a confirmation message into the message queue. The actual response
// arrives later as a chat message from the server.
static void repeater_send(const char* contact_name, const char* cmd, const char* fmt) {
    if (!contact_name || !cmd || !cmd[0]) return;
    sigurdos::mesh::sendCommand(contact_name, cmd);
    char buf[80];
    snprintf(buf, sizeof(buf), fmt, cmd);
    sigurdos::mesh::mesh_v2_queue_push("System", "", buf, 0, 0.0f);
}

// Generic input dialog — shows a text entry box and sends
// <cmd_prefix><user_input> as an admin CLI command to the logged-in server.
static void repeater_input_dialog(const char* contact_name,
                                  const char* title,
                                  const char* hint,
                                  const char* cmd_prefix,
                                  bool password_mode)
{
    if (!contact_name) return;
    lv_obj_t* scr = lv_obj_get_screen(lv_scr_act());
    auto dlg_sz = dialog_size(260, 124);
    lv_obj_t* dlg = lv_obj_create(scr);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    // Title
    lv_obj_t* ttl = lv_label_create(dlg);
    lv_label_set_text(ttl, title);
    lv_obj_set_style_text_color(ttl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_12, 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 4);

    // Hint
    lv_obj_t* hnt = lv_label_create(dlg);
    lv_label_set_text(hnt, hint);
    lv_obj_set_style_text_color(hnt, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(hnt, &lv_font_montserrat_10, 0);
    lv_obj_align(hnt, LV_ALIGN_TOP_MID, 0, 24);

    // Textarea
    lv_obj_t* ta = lv_textarea_create(dlg);
    lv_obj_set_size(ta, dlg_sz.w - 16, 28);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 38);
    lv_textarea_set_placeholder_text(ta, "Value (Enter to send)");
    lv_textarea_set_password_mode(ta, password_mode);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_group_t* g = lv_group_get_default();
    if (g) lv_group_focus_obj(ta);

    // Cancel button
    lv_obj_t* cancel_btn = lv_btn_create(dlg);
    lv_obj_set_size(cancel_btn, 80, 24);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -4);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_radius(cancel_btn, 0, 0);
    lv_obj_t* cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ce) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
    }, LV_EVENT_CLICKED, nullptr);

    // Send button
    struct RiData { char* name; const char* prefix; lv_obj_t* ta; };
    RiData* rd = new RiData{strdup(contact_name), strdup(cmd_prefix), ta};

    lv_obj_t* send_btn = lv_btn_create(dlg);
    lv_obj_set_size(send_btn, 80, 24);
    lv_obj_align(send_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(send_btn, 0, 0);
    lv_obj_t* sb = lv_label_create(send_btn);
    lv_label_set_text(sb, "Send");
    lv_obj_center(sb);
    lv_obj_set_style_text_color(sb, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_user_data(send_btn, rd);

    lv_obj_add_event_cb(send_btn, [](lv_event_t* le) {
        RiData* d = (RiData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(le));
        if (d && d->name && d->prefix && d->ta) {
            const char* val = lv_textarea_get_text(d->ta);
            if (val && val[0]) {
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "%s%s", d->prefix, val);
                repeater_send(d->name, cmd, "Sent: %s");
            }
        }
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(le)));
    }, LV_EVENT_CLICKED, nullptr);

    // Enter key handler
    lv_obj_add_event_cb(ta, [](lv_event_t* te) {
        lv_obj_t* t = (lv_obj_t*)lv_event_get_target(te);
        uint32_t key = lv_event_get_key(te);
        if (key == LV_KEY_ENTER) {
            const char* val = lv_textarea_get_text(t);
            if (val && val[0]) {
                lv_obj_t* parent = lv_obj_get_parent(t);
                if (parent) {
                    uint32_t c = lv_obj_get_child_cnt(parent);
                    for (uint32_t i = 0; i < c; i++) {
                        lv_obj_t* child = lv_obj_get_child(parent, i);
                        if (child && lv_obj_check_type(child, &lv_button_class)) {
                            RiData* data = (RiData*)lv_obj_get_user_data(child);
                            if (data) {
                                lv_obj_send_event(child, LV_EVENT_CLICKED, nullptr);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }, LV_EVENT_KEY, nullptr);

    // Cleanup
    lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
        RiData* d = (RiData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de));
        if (d) {
            free(d->name);
            free((void*)d->prefix);
            delete d;
        }
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_set_user_data(dlg, rd);
}

// ════════════════════════════════════════════════════════
// Repeater Detail — login-first, then settings-style sections
// skip_login=true → show post-login view (used by login-polling timer)
// skip_login=false (default) → always show pre-login + Login button
// ════════════════════════════════════════════════════════
void repeater_detail_screen_show(const char* contact_name, bool skip_login)
{
    if (!contact_name || !contact_name[0]) return;

    // Look up the contact first (need type for screen title)
    sigurdos::mesh::ContactInfo contacts[64];
    int total = sigurdos::mesh::exportContactsFull(contacts, 64);
    const sigurdos::mesh::ContactInfo* target = nullptr;
    for (int i = 0; i < total; i++) {
        if (strcmp(contacts[i].name, contact_name) == 0) {
            target = &contacts[i];
            break;
        }
    }

    // Determine screen title from contact type
    const char* screen_title = "Repeater";
    if (target) {
        switch (target->type) {
            case ADV_TYPE_REPEATER: screen_title = "Repeater"; break;
            case ADV_TYPE_ROOM:     screen_title = "Room Server"; break;
            default:                screen_title = "Node"; break;
        }
    }

    lv_obj_t* scr = make_screen_full(screen_title);

    if (!target) {
        lv_obj_t* err = lv_label_create(scr);
        lv_label_set_text(err, "Contact not found");
        lv_obj_set_style_text_color(err, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_text_font(err, &lv_font_montserrat_12, 0);
        lv_obj_align(err, LV_ALIGN_CENTER, 0, 0);
        show_screen(scr);
        return;
    }

    uint8_t login_st = sigurdos::mesh::getLoginStatus(contact_name);
    bool is_fav = sigurdos::mesh::isContactFavourite(contact_name);

    // ── Content list ──────────────────────────────────
    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    // Ensure flex column layout for proper vertical stacking
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    int row = 0;

    // skip_login=false: ALWAYS show pre-login (don't trust cached login state)
    // skip_login=true:  show post-login only if actually logged in (timer path)
    if (!skip_login || login_st != LOGIN_STATUS_OK) {
        // ── Pre-login view (compact) ────────────────────────
        // Content area is ~198px tall. Items are sized explicitly
        // to fit without overlapping or squishing.

        // Title row (28px) — repeater name + fav toggle
        lv_obj_t* title_row = lv_obj_create(list);
        lv_obj_set_size(title_row, LV_PCT(100), 28);
        lv_obj_set_style_bg_color(title_row, lv_color_hex(BG_PRIMARY), 0);
        lv_obj_set_style_bg_opa(title_row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(title_row, 0, 0);
        lv_obj_set_style_radius(title_row, 0, 0);
        lv_obj_set_style_pad_all(title_row, 0, 0);
        lv_obj_clear_flag(title_row, LV_OBJ_FLAG_CLICKABLE);
        {
            lv_obj_t* title_lbl = lv_label_create(title_row);
            lv_label_set_text(title_lbl, contact_name);
            lv_obj_set_style_text_color(title_lbl, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
            lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 8, 0);
        }
        // Favourite star toggle
        {
            char* fav_name = strdup(contact_name);
            lv_obj_t* fav_btn = lv_btn_create(title_row);
            lv_obj_set_size(fav_btn, 30, 24);
            lv_obj_align(fav_btn, LV_ALIGN_RIGHT_MID, -4, 0);
            lv_obj_set_style_bg_color(fav_btn, lv_color_hex(BG_INPUT), 0);
            lv_obj_set_style_radius(fav_btn, 0, 0);
            lv_obj_set_style_border_width(fav_btn, 0, 0);
            lv_obj_set_style_pad_all(fav_btn, 0, 0);
            lv_obj_t* fav_icon = lv_label_create(fav_btn);
            lv_label_set_text(fav_icon, is_fav ? LV_SYMBOL_CLOSE : LV_SYMBOL_OK);
            lv_obj_set_style_text_color(fav_icon,
                lv_color_hex(is_fav ? ACCENT : TEXT_MUTED), 0);
            lv_obj_center(fav_icon);
            lv_obj_set_user_data(fav_btn, fav_name);
            lv_obj_add_event_cb(fav_btn, [](lv_event_t* e) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(btn);
                if (name) {
                    bool cur = sigurdos::mesh::isContactFavourite(name);
                    sigurdos::mesh::setContactFavourite(name, !cur);

                    repeater_detail_screen_show(name, true);

                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(fav_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
        }
        row++;

        // Info rows (compact, no section header)
        {
            char snr_str[16], rssi_str[16], type_str[24];
            snprintf(snr_str, sizeof(snr_str), "%.1f dB", target->snr);
            snprintf(rssi_str, sizeof(rssi_str), "%d dBm", target->rssi);
            switch (target->type) {
                case ADV_TYPE_REPEATER: strcpy(type_str, "Repeater"); break;
                case ADV_TYPE_ROOM:     strcpy(type_str, "Room Server"); break;
                default:                strcpy(type_str, "Unknown"); break;
            }
            // Type row: left label, right value
            lv_obj_t* tr = lv_obj_create(list);
            lv_obj_set_size(tr, LV_PCT(100), 24);
            lv_obj_set_style_bg_color(tr, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(tr, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(tr, 0, 0);
            lv_obj_set_style_radius(tr, 0, 0);
            lv_obj_set_style_pad_all(tr, 0, 0);
            lv_obj_clear_flag(tr, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t* tl = lv_label_create(tr);
            lv_label_set_text(tl, "  Type");
            lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(tl, &lv_font_montserrat_10, 0);
            lv_obj_align(tl, LV_ALIGN_LEFT_MID, 4, 0);
            lv_obj_t* tv = lv_label_create(tr);
            lv_label_set_text(tv, type_str);
            lv_obj_set_style_text_color(tv, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_text_font(tv, &lv_font_montserrat_10, 0);
            lv_obj_align(tv, LV_ALIGN_RIGHT_MID, -4, 0);
            // SNR + RSSI on one row
            lv_obj_t* sr = lv_obj_create(list);
            lv_obj_set_size(sr, LV_PCT(100), 24);
            lv_obj_set_style_bg_color(sr, lv_color_hex(row % 2 == 1 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(sr, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(sr, 0, 0);
            lv_obj_set_style_radius(sr, 0, 0);
            lv_obj_set_style_pad_all(sr, 0, 0);
            lv_obj_clear_flag(sr, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t* snr_l = lv_label_create(sr);
            lv_label_set_text(snr_l, "  SNR");
            lv_obj_set_style_text_color(snr_l, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(snr_l, &lv_font_montserrat_10, 0);
            lv_obj_align(snr_l, LV_ALIGN_LEFT_MID, 4, 0);
            lv_obj_t* snr_v = lv_label_create(sr);
            lv_label_set_text(snr_v, snr_str);
            lv_obj_set_style_text_color(snr_v, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(snr_v, &lv_font_montserrat_10, 0);
            lv_obj_align(snr_v, LV_ALIGN_LEFT_MID, 40, 0);
            lv_obj_t* rssi_l = lv_label_create(sr);
            lv_label_set_text(rssi_l, "RSSI");
            lv_obj_set_style_text_color(rssi_l, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(rssi_l, &lv_font_montserrat_10, 0);
            lv_obj_align(rssi_l, LV_ALIGN_LEFT_MID, 120, 0);
            lv_obj_t* rssi_v = lv_label_create(sr);
            lv_label_set_text(rssi_v, rssi_str);
            lv_obj_set_style_text_color(rssi_v, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(rssi_v, &lv_font_montserrat_10, 0);
            lv_obj_align(rssi_v, LV_ALIGN_LEFT_MID, 158, 0);
            row += 2;
        }

        // Login status row
        {
            const char* login_text = "Not logged in";
            uint32_t login_color = TEXT_SECONDARY;
            switch (login_st) {

                case LOGIN_STATUS_OK:     login_text = "Logged in";      login_color = ACCENT_GREEN; break;

                case LOGIN_STATUS_PENDING: login_text = "Login pending..."; login_color = ACCENT; break;
                case LOGIN_STATUS_FAILED:  login_text = "Login failed";     login_color = ACCENT_RED; break;
            }
            lv_obj_t* lr = lv_obj_create(list);
            lv_obj_set_size(lr, LV_PCT(100), 24);
            lv_obj_set_style_bg_color(lr, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(lr, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(lr, 0, 0);
            lv_obj_set_style_radius(lr, 0, 0);
            lv_obj_set_style_pad_all(lr, 0, 0);
            lv_obj_clear_flag(lr, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t* login_l = lv_label_create(lr);
            lv_label_set_text(login_l, "  Login");
            lv_obj_set_style_text_color(login_l, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(login_l, &lv_font_montserrat_10, 0);
            lv_obj_align(login_l, LV_ALIGN_LEFT_MID, 4, 0);
            lv_obj_t* login_v = lv_label_create(lr);
            lv_label_set_text(login_v, login_text);
            lv_obj_set_style_text_color(login_v, lv_color_hex(login_color), 0);
            lv_obj_set_style_text_font(login_v, &lv_font_montserrat_10, 0);
            lv_obj_align(login_v, LV_ALIGN_RIGHT_MID, -4, 0);
            row++;
        }

        // Spacer (flex grow — fills remaining space)
        lv_obj_t* spacer = lv_obj_create(list);
        lv_obj_set_size(spacer, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(spacer, 1);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
        lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
        row++;

        // Login button (prominent)
        if (login_st != LOGIN_STATUS_PENDING) {
            char* li_name = strdup(contact_name);
            lv_obj_t* login_btn = lv_btn_create(list);
            lv_obj_set_size(login_btn, LV_PCT(100), 32);
            lv_obj_set_style_bg_color(login_btn, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_bg_opa(login_btn, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(login_btn, 0, 0);
            lv_obj_set_style_border_width(login_btn, 0, 0);
            lv_obj_t* login_lbl = lv_label_create(login_btn);
            lv_label_set_text(login_lbl, LV_SYMBOL_DIRECTORY "  Login");
            lv_obj_set_style_text_color(login_lbl, lv_color_hex(BG_PRIMARY), 0);
            lv_obj_center(login_lbl);
            lv_obj_set_user_data(login_btn, li_name);
            lv_obj_add_event_cb(login_btn, [](lv_event_t* e) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(btn);
                if (name) show_login_password_dialog(name);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(login_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
            row++;
        }

    } else {
        // ── Post-login: full management UI ─────────────

        // ── Section helpers ────────────────────────────
        auto sec_header = [&](const char* name) {
            lv_obj_t* h = lv_list_add_btn(list, nullptr, name);
            lv_obj_set_style_bg_color(h, lv_color_hex(BG_TERTIARY), 0);
            lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(h, lv_color_hex(TEXT_MUTED), 0);
            lv_obj_set_style_text_font(h, &lv_font_montserrat_10, 0);
            lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE);
            row++;
        };

        // Set-value button: opens an input dialog and sends <prefix> + user input
        struct SetCtx { char* name; char prefix[24]; char title[24]; char hint[32]; bool pw; };
        auto add_set = [&](const char* icon_lbl, const char* title, const char* hint, const char* prefix, bool pw) {
            auto* ctx = new SetCtx();
            ctx->name = strdup(contact_name);
            ctx->pw = pw;
            strncpy(ctx->prefix, prefix, sizeof(ctx->prefix)-1);
            strncpy(ctx->title, title, sizeof(ctx->title)-1);
            strncpy(ctx->hint, hint, sizeof(ctx->hint)-1);
            lv_obj_t* r = lv_list_add_btn(list, icon_lbl, "set >");
            lv_obj_set_style_bg_color(r, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_t* vl = lv_obj_get_child(r, 1);
            if (vl && lv_obj_check_type(vl, &lv_label_class))
                lv_obj_set_style_text_color(vl, lv_color_hex(ACCENT), 0);
            lv_obj_set_user_data(r, ctx);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                auto* c = (SetCtx*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (c && c->name) repeater_input_dialog(c->name, c->title, c->hint, c->prefix, c->pw);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                auto* c = (SetCtx*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (c) { free(c->name); delete c; }
            }, LV_EVENT_DELETE, nullptr);
            row++;
        };

        // Action button: sends a command immediately and pushes confirmation
        struct ActCtx { char* name; char cmd[32]; char fmt[48]; };
        auto add_act = [&](const char* icon_lbl, const char* cmd, const char* fmt) {
            auto* ctx = new ActCtx();
            ctx->name = strdup(contact_name);
            strncpy(ctx->cmd, cmd, sizeof(ctx->cmd)-1);
            strncpy(ctx->fmt, fmt, sizeof(ctx->fmt)-1);
            lv_obj_t* r = lv_list_add_btn(list, icon_lbl, ">");
            lv_obj_set_style_bg_color(r, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_t* vl = lv_obj_get_child(r, 1);
            if (vl && lv_obj_check_type(vl, &lv_label_class))
                lv_obj_set_style_text_color(vl, lv_color_hex(ACCENT), 0);
            lv_obj_set_user_data(r, ctx);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                auto* c = (ActCtx*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (c && c->name) repeater_send(c->name, c->cmd, c->fmt);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                auto* c = (ActCtx*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (c) { free(c->name); delete c; }
            }, LV_EVENT_DELETE, nullptr);
            row++;
        };

        // ── Section: Connection ───────────────────────────
        sec_header("  Connection");
        {
            char buf[128];
            auto add_con = [&](const char* label, const char* val, uint32_t color) {
                snprintf(buf, sizeof(buf), "  %s", label);
                lv_obj_t* r = lv_list_add_btn(list, buf, val);
                lv_obj_set_style_bg_color(r, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
                lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(r, lv_color_hex(TEXT_PRIMARY), 0);
                lv_obj_t* val_lbl = lv_obj_get_child(r, 1);
                if (val_lbl && lv_obj_check_type(val_lbl, &lv_label_class))
                    lv_obj_set_style_text_color(val_lbl, lv_color_hex(color), 0);
                row++;
            };
            char snr_str[16], rssi_str[16];
            snprintf(snr_str, sizeof(snr_str), "%.1f dB", target->snr);
            snprintf(rssi_str, sizeof(rssi_str), "%d dBm", target->rssi);
            add_con("Status", "Connected", ACCENT_GREEN);
            add_con("SNR", snr_str, TEXT_PRIMARY);
            add_con("RSSI", rssi_str, TEXT_PRIMARY);
            add_con("Login", "Logged in", ACCENT_GREEN);
            uint8_t perm = sigurdos::mesh::getLoginPermission(contact_name);
            char perm_buf[24];
            snprintf(perm_buf, sizeof(perm_buf), "Admin (perm=%d)", perm);
            add_con("Permission", perm_buf, ACCENT);
        }

        // ── Section: Radio Settings ──────────────────────
        sec_header("  Radio Settings");
        add_set(LV_SYMBOL_WIFI "  Freq (MHz)",  "Set Freq",       "e.g. 868.0",       "set freq ",  false);
        add_set(LV_SYMBOL_WIFI "  Bandwidth",   "Set Bandwidth",  "e.g. 125.0",       "set bw ",    false);
        add_set(LV_SYMBOL_WIFI "  Spreading Factor", "Set SF",    "Range 5-12",       "set sf ",    false);
        add_set(LV_SYMBOL_WIFI "  Coding Rate",  "Set CR",        "Range 5-8",        "set cr ",    false);

        // ── Section: Management ──────────────────────────
        sec_header("  Management");
        add_set(LV_SYMBOL_REFRESH "  Advert Interval", "Advert Interval", "Minutes (60-240)", "set advert.interval ", false);
        add_act(LV_SYMBOL_REFRESH "  Sync Clock", "clock sync", "Sent: clock sync");
        add_set(LV_SYMBOL_CLOSE "  Admin Password",   "Admin Password",   "New admin password", "password ",  true);
        add_set(LV_SYMBOL_CLOSE "  Guest Password",   "Guest Password",   "New guest password", "set guest.password ", false);
        add_act(LV_SYMBOL_LIST "  Version",           "ver",              "Sent: ver");

        // ── Section: Network ─────────────────────────────
        sec_header("  Network");
        add_act(LV_SYMBOL_LIST "  Neighbours",        "neighbors",        "Sent: neighbors");
        add_act(LV_SYMBOL_LIST "  Regions",           "region",           "Sent: region");
        add_act(LV_SYMBOL_REFRESH "  Repeat On",      "set repeat on",    "Sent: repeat on");
        add_act(LV_SYMBOL_REFRESH "  Repeat Off",     "set repeat off",   "Sent: repeat off");
        add_act(LV_SYMBOL_REFRESH "  Advert (flood)",  "advert",          "Sent: flood advert");
        add_act(LV_SYMBOL_EDIT "  Public Key",        "get public.key",   "Sent: get public.key");

        // ── Section: Commands ────────────────────────────
        sec_header("  Commands");

        // Admin Cmd
        {
            char* n = strdup(contact_name);
            lv_obj_t* r = lv_list_add_btn(list, LV_SYMBOL_KEYBOARD "  Admin Cmd", ">");
            lv_obj_set_style_bg_color(r, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_user_data(r, n);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                const char* name = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (name) show_admin_cmd_dialog(name);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
            row++;
        }

        // Fetch Msgs
        {
            char* n = strdup(contact_name);
            lv_obj_t* r = lv_list_add_btn(list, LV_SYMBOL_LIST "  Fetch Msgs", ">");
            lv_obj_set_style_bg_color(r, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_user_data(r, n);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                const char* name = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (name) show_fetch_msgs_dialog(name);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
            row++;
        }

        // Reboot (with confirmation dialog)
        {
            char* n = strdup(contact_name);
            lv_obj_t* r = lv_list_add_btn(list, LV_SYMBOL_REFRESH "  Reboot", "!!");
            lv_obj_set_style_bg_color(r, lv_color_hex(ACCENT_RED), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(0xffffff), 0);
            lv_obj_set_user_data(r, n);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                const char* name = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (name) {
                    // Confirmation dialog before reboot
                    lv_obj_t* act = lv_scr_act();
                    auto dsz = dialog_size(220, 80);
                    lv_obj_t* dlg = lv_obj_create(act);
                    lv_obj_set_size(dlg, dsz.w, dsz.h);
                    lv_obj_center(dlg);
                    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
                    lv_obj_set_style_radius(dlg, 0, 0);
                    lv_obj_set_style_border_width(dlg, 0, 0);
                    lv_obj_t* tl = lv_label_create(dlg);
                    lv_label_set_text(tl, "Reboot repeater?");
                    lv_obj_set_style_text_color(tl, lv_color_hex(ACCENT_RED), 0);
                    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 10);
                    char* cn = strdup(name);
                    lv_obj_set_user_data(dlg, cn);
                    lv_obj_t* yb = lv_btn_create(dlg);
                    lv_obj_set_size(yb, 80, 24);
                    lv_obj_align(yb, LV_ALIGN_BOTTOM_LEFT, 10, -4);
                    lv_obj_set_style_bg_color(yb, lv_color_hex(ACCENT_RED), 0);
                    lv_obj_set_style_radius(yb, 0, 0);
                    lv_obj_t* yl = lv_label_create(yb);
                    lv_label_set_text(yl, "Reboot");
                    lv_obj_center(yl);
                    lv_obj_set_style_text_color(yl, lv_color_hex(0xffffff), 0);
                    lv_obj_add_event_cb(yb, [](lv_event_t* ce) {
                        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce));
                        const char* cn = (const char*)lv_obj_get_user_data(dlg);

                        if (cn) { repeater_send(cn, "reboot", "Reboot sent"); }

                        lv_obj_del_async(dlg);
                    }, LV_EVENT_CLICKED, nullptr);
                    lv_obj_t* nb = lv_btn_create(dlg);
                    lv_obj_set_size(nb, 80, 24);
                    lv_obj_align(nb, LV_ALIGN_BOTTOM_RIGHT, -10, -4);
                    lv_obj_set_style_bg_color(nb, lv_color_hex(BG_INPUT), 0);
                    lv_obj_set_style_radius(nb, 0, 0);
                    lv_obj_t* nl = lv_label_create(nb);
                    lv_label_set_text(nl, "Cancel");
                    lv_obj_center(nl);
                    lv_obj_add_event_cb(nb, [](lv_event_t* ce) {
                        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
                    }, LV_EVENT_CLICKED, nullptr);
                    lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
                        free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de)));
                    }, LV_EVENT_DELETE, nullptr);
                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
            row++;
        }

        // Logout
        {
            char* n = strdup(contact_name);
            lv_obj_t* r = lv_list_add_btn(list, LV_SYMBOL_REFRESH "  Logout", ">");
            lv_obj_set_style_bg_color(r, lv_color_hex(ACCENT_ORANGE), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(0xffffff), 0);
            lv_obj_set_user_data(r, n);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                const char* name = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (name) {
                    sigurdos::mesh::sendLogout(name);
                    lv_timer_create([](lv_timer_t* t) {
                        go_back();
                        lv_timer_del(t);
                    }, 600, nullptr);
                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
            row++;
        }
    }

    // Add all clickable list items to the default group for trackball/keyboard navigation
    lv_group_t* grp = lv_group_get_default();
    if (grp) {
        uint32_t child_cnt = lv_obj_get_child_cnt(list);
        bool focused = false;
        for (uint32_t ci = 0; ci < child_cnt; ci++) {
            lv_obj_t* child = lv_obj_get_child(list, ci);
            if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
                lv_group_add_obj(grp, child);
                if (!focused) {
                    lv_group_focus_obj(child);
                    focused = true;
                }
            }
        }
    }

    show_screen(scr);
}

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
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
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
            lv_obj_set_style_text_color(ch_label, lv_color_hex(TEXT_MUTED), 0);
            lv_obj_set_style_text_font(ch_label, &lv_font_montserrat_12, 0);
            lv_obj_align_to(ch_label, chart, LV_ALIGN_OUT_TOP_LEFT, 0, -2);
        }
    } else {
        // ── Two-column flex row ─────────────────────────────
        lv_obj_t* row = lv_obj_create(scr);
        lv_obj_set_size(row, CONTENT_W, LV_SIZE_CONTENT);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        // Left column — signal metrics + airtime
        lv_obj_t* left = lv_label_create(row);
        lv_obj_set_width(left, LV_PCT(48));
        lv_obj_set_style_text_color(left, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(left, &lv_font_montserrat_12, 0);

        char left_buf[256];
        snprintf(left_buf, sizeof(left_buf),
            "RSSI    %d dBm\n"
            "SNR     %.1f dB\n"
            "Noise   %d dBm\n\n"
            "Freq    %.3f MHz\n"
            "BW      %.1f kHz\n"
            "SF      %d\n"
            "CR      4/%d\n"
            "TX Pwr  %d dBm\n"
            "RX Gain %s",
            rssi, snr, noise,
            p.freq, p.bw, p.sf, p.cr, p.tx_power_dbm,
            p.rx_boosted_gain ? "BOOST" : "NORMAL");
        lv_label_set_text(left, left_buf);

        // Right column — packet counters + airtime + duty cycle
        lv_obj_t* right = lv_label_create(row);
        lv_obj_set_width(right, LV_PCT(48));
        lv_obj_set_style_text_color(right, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(right, &lv_font_montserrat_12, 0);

        char right_buf[256];
        snprintf(right_buf, sizeof(right_buf),
            "TX Fld  %u\n"
            "TX Dir  %u\n"
            "RX Fld  %u\n"
            "RX Dir  %u\n"
            "TX Air  %lu ms\n"
            "RX Air  %lu ms\n\n"
            "Duty    %u%%\n"
            "Budget  %lu ms",
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
            lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 6, CONTENT_Y + 4 + 190);
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
            lv_obj_set_style_text_color(ch_label, lv_color_hex(TEXT_MUTED), 0);
            lv_obj_set_style_text_font(ch_label, &lv_font_montserrat_12, 0);
            lv_obj_align_to(ch_label, chart, LV_ALIGN_OUT_TOP_LEFT, 0, -2);
        }
    }

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Map — offline tile maps
// ════════════════════════════════════════════════════════
void map_screen_show()
{
    lv_obj_t* scr = make_screen_full("Map");

    sigurdos_map_init();
    sigurdos_map_reparent(scr);
    sigurdos_map_render();

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
            if (dx != 0 || dy != 0) sigurdos_map_pan(dx, dy);
            uint32_t now = millis();
            if (now - map_last_render_ms >= 200) {
                sigurdos_map_render();
                map_last_render_ms = now;
            }
        } else if (code == LV_EVENT_RELEASED) {
            sigurdos_map_render();
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
    lv_obj_add_event_cb(zoom_in, [](lv_event_t*) { sigurdos_map_zoom_in(); sigurdos_map_render(); },
                        LV_EVENT_CLICKED, nullptr);

    lv_obj_t* zoom_out = lv_btn_create(scr);
    lv_obj_set_size(zoom_out, 32, 32);
    lv_obj_align(zoom_out, LV_ALIGN_BOTTOM_RIGHT, -8, -(BOT_BAR_H + DIVIDER_H + 48));
    lv_obj_set_style_bg_color(zoom_out, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(zoom_out, 0, 0);
    lv_obj_t* zo = lv_label_create(zoom_out);
    lv_label_set_text(zo, "-"); lv_obj_center(zo);
    lv_obj_add_event_cb(zoom_out, [](lv_event_t*) { sigurdos_map_zoom_out(); sigurdos_map_render(); },
                        LV_EVENT_CLICKED, nullptr);

    (void)zoom_y_base;
    show_screen(scr);
    lv_timer_create([](lv_timer_t* t) {
        sigurdos_map_render();
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
    sigurdos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);

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
    apply_focus_style(input);

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
            // Days in month lookup: jan=31, feb=28, mar=31, ...
            static const uint8_t DAYS_IN_MONTH[] = {31,28,31,30,31,30,31,31,30,31,30,31};
            if (sscanf(s, "%d-%d-%d", &ny, &nm, &nd) == 3 &&
                ny > 2020 && nm >= 1 && nm <= 12 && nd >= 1) {
                // Check days in month (with leap year for February)
                uint8_t max_days = DAYS_IN_MONTH[nm - 1];
                if (nm == 2 && (ny % 4 == 0 && (ny % 100 != 0 || ny % 400 == 0)))
                    max_days = 29;
                if (nd <= max_days) {
                    int cy, cmo, cd, ch, cmi;
                    sigurdos::mesh::getCurrentLocalDateTime(&cy, &cmo, &cd, &ch, &cmi);
                    epoch = sigurdos::mesh::makeEpoch(ny, nm, nd, ch, cmi);
                    valid = true;
                } else {
                    lv_label_set_text(ctx->feedback, "Invalid day for month");
                }
            } else {
                lv_label_set_text(ctx->feedback, "Invalid date (YYYY-MM-DD)");
            }
        } else {
            int nh, nm_v;
            if (sscanf(s, "%d:%d", &nh, &nm_v) == 2 &&
                nh >= 0 && nh <= 23 && nm_v >= 0 && nm_v <= 59) {
                int cy, cmo, cd, ch, cmi;
                sigurdos::mesh::getCurrentLocalDateTime(&cy, &cmo, &cd, &ch, &cmi);
                epoch = sigurdos::mesh::makeEpoch(cy, cmo, cd, nh, nm_v);
                valid = true;
            } else {
                lv_label_set_text(ctx->feedback, "Invalid time (HH:MM)");
            }
        }

        if (valid && sigurdos::mesh::setSystemTime(epoch)) {
            int yy, mmo, dd, hh, mmi;
            sigurdos::mesh::getCurrentLocalDateTime(&yy, &mmo, &dd, &hh, &mmi);
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

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
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
        sigurdos_keyboard_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
        auto* c = (BacklightCtx*)lv_event_get_user_data(e);
        if (c->brightness <= 230) c->brightness += 25;
        else c->brightness = 255;
        sigurdos_keyboard_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* c = (BacklightCtx*)lv_event_get_user_data(e);
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.kbd_backlight = (uint8_t)c->brightness;
        sigurdos::prefs_set(np);
        sigurdos_keyboard_set_default_brightness(c->brightness);

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

    uint16_t current = sigurdos::prefs_get().auto_off_timeout;
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

        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.auto_off_timeout = value;
        sigurdos::prefs_set(np);
        sigurdos_display_reset_auto_off();

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

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
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
        sigurdos_display_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(plus_btn, [](lv_event_t* e) {
        auto* c = (DisplayBrightnessCtx*)lv_event_get_user_data(e);
        if (c->brightness <= 230) c->brightness += 25;
        else c->brightness = 255;
        sigurdos_display_set_brightness(c->brightness);
        char b[24];
        snprintf(b, sizeof(b), "%d (%d%%)", c->brightness, c->brightness * 100 / 255);
        lv_label_set_text(c->value_label, b);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* c = (DisplayBrightnessCtx*)lv_event_get_user_data(e);
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.display_brightness = (uint8_t)c->brightness;
        sigurdos::prefs_set(np);
        sigurdos_display_set_brightness(c->brightness);

        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Display: %d (%d%%)",
                 c->brightness, c->brightness * 100 / 255);
        update_row_label(c->row_label, row_buf);

        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, (void*)ctx);
}

// ════════════════════════════════════════════════════════
// Settings — category menu + sub-screens
// ════════════════════════════════════════════════════════

void settings_radio_show()
{
    lv_obj_t* scr = make_screen_full("Radio / Mesh");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    char buf[128];
    int row = 0;

    // Radio config
    if (p.configured) {
        snprintf(buf, sizeof(buf), "  Radio: %.3f MHz / %.1f kHz / SF%d / %d dBm",
                 p.freq, p.bw, p.sf, p.tx_power_dbm);
    } else {
        snprintf(buf, sizeof(buf), "  Radio: NOT CONFIGURED");
    }
    lv_obj_t* btn_rf = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
    lv_obj_set_style_bg_color(btn_rf, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_rf, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_rf, lv_color_hex(TEXT_PRIMARY), 0);
    if (!p.configured) {
        lv_obj_set_style_bg_color(btn_rf, lv_color_hex(0x4a2020), 0);
        lv_obj_set_style_bg_color(btn_rf, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    }
    lv_obj_add_event_cb(btn_rf, [](lv_event_t*) {
        radio_setup_screen_show();
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Flood max hops
    {
        static constexpr uint8_t FLOOD_HOPS_VALUES[] = {0, 3, 5, 10, 20, 50};
        static constexpr const char* FLOOD_HOPS_LABELS[] = {"No limit", "3", "5", "10", "20", "50"};
        static constexpr int NUM_FLOOD_HOPS = 6;
        int cur_idx = 0;
        for (int i = 0; i < NUM_FLOOD_HOPS; i++) {
            if (p.flood_max_hops == FLOOD_HOPS_VALUES[i]) { cur_idx = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Flood max hops: %s", FLOOD_HOPS_LABELS[cur_idx]);
        lv_obj_t* btn_flood = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_flood, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_flood, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_flood, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_flood, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            int idx = 0;
            for (int i = 0; i < 6; i++) {
                if (np.flood_max_hops == (uint8_t[]){0,3,5,10,20,50}[i]) { idx = i; break; }
            }
            idx = (idx + 1) % 6;
            np.flood_max_hops = (uint8_t[]){0,3,5,10,20,50}[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Flood max hops: %s",
                     (const char*[]){"No limit","3","5","10","20","50"}[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Auto-add contact types
    {
        static constexpr uint8_t AA_CONFIG_VALUES[] = {0x1E, 0x06, 0x0A, 0x02};
        static constexpr const char* AA_CONFIG_LABELS[] = {
            "All types", "Chat+Repeater", "Chat+Room", "Chat only"};
        static constexpr int NUM_AA_CONFIG = 4;
        int cur_aa = 0;
        for (int i = 0; i < NUM_AA_CONFIG; i++) {
            if (p.autoadd_config == AA_CONFIG_VALUES[i]) { cur_aa = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Auto-add: %s", AA_CONFIG_LABELS[cur_aa]);
        lv_obj_t* btn_aa = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_aa, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_aa, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_aa, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_aa, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr uint8_t VALS[] = {0x1E, 0x06, 0x0A, 0x02};
            static constexpr const char* LABELS[] = {
                "All types", "Chat+Repeater", "Chat+Room", "Chat only"};
            static constexpr int N = 4;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (np.autoadd_config == VALS[i]) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.autoadd_config = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Auto-add: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Auto-add max hops
    {
        static constexpr uint8_t AA_HOPS_VALUES[] = {0, 3, 5, 10, 20, 50};
        static constexpr const char* AA_HOPS_LABELS[] = {"No limit", "3", "5", "10", "20", "50"};
        static constexpr int NUM_AA_HOPS = 6;
        int cur_ah = 0;
        for (int i = 0; i < NUM_AA_HOPS; i++) {
            if (p.autoadd_max_hops == AA_HOPS_VALUES[i]) { cur_ah = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Add max hops: %s", AA_HOPS_LABELS[cur_ah]);
        lv_obj_t* btn_ah = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_ah, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_ah, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_ah, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_ah, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr uint8_t VALS[] = {0, 3, 5, 10, 20, 50};
            static constexpr const char* LABELS[] = {"No limit","3","5","10","20","50"};
            static constexpr int N = 6;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (np.autoadd_max_hops == VALS[i]) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.autoadd_max_hops = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Add max hops: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // RX delay base
    {
        static constexpr float RX_DELAY_VALUES[] = {0.0f, 5.0f, 10.0f, 15.0f, 20.0f};
        static constexpr const char* RX_DELAY_LABELS[] = {"0 (off)", "5", "10", "15", "20"};
        static constexpr int NUM_RX_DELAY = 5;
        int cur_rx = 0;
        for (int i = 0; i < NUM_RX_DELAY; i++) {
            if (fabsf(p.rx_delay_base - RX_DELAY_VALUES[i]) < 0.1f) { cur_rx = i; break; }
        }
        snprintf(buf, sizeof(buf), "  RX delay base: %s", RX_DELAY_LABELS[cur_rx]);
        lv_obj_t* btn_rx = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_rx, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_rx, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_rx, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_rx, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr float VALS[] = {0.0f, 5.0f, 10.0f, 15.0f, 20.0f};
            static constexpr const char* LABELS[] = {"0 (off)","5","10","15","20"};
            static constexpr int N = 5;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (fabsf(np.rx_delay_base - VALS[i]) < 0.1f) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.rx_delay_base = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  RX delay base: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // TX delay factor
    {
        static constexpr float TX_DELAY_VALUES[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
        static constexpr const char* TX_DELAY_LABELS[] = {"0 (off)", "0.5", "1.0", "1.5", "2.0"};
        static constexpr int NUM_TX_DELAY = 5;
        int cur_tx = 0;
        for (int i = 0; i < NUM_TX_DELAY; i++) {
            if (fabsf(p.tx_delay_factor - TX_DELAY_VALUES[i]) < 0.1f) { cur_tx = i; break; }
        }
        snprintf(buf, sizeof(buf), "  TX delay factor: %s", TX_DELAY_LABELS[cur_tx]);
        lv_obj_t* btn_tx = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_tx, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_tx, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_tx, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_tx, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr float VALS[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
            static constexpr const char* LABELS[] = {"0 (off)","0.5","1.0","1.5","2.0"};
            static constexpr int N = 5;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (fabsf(np.tx_delay_factor - VALS[i]) < 0.1f) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.tx_delay_factor = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  TX delay factor: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Direct TX delay factor
    {
        static constexpr float DIR_TX_DELAY_VALUES[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
        static constexpr const char* DIR_TX_DELAY_LABELS[] = {"0 (off)", "0.5", "1.0", "1.5", "2.0"};
        static constexpr int NUM_DIR_TX_DELAY = 5;
        int cur_dir = 0;
        for (int i = 0; i < NUM_DIR_TX_DELAY; i++) {
            if (fabsf(p.direct_tx_delay_factor - DIR_TX_DELAY_VALUES[i]) < 0.1f) { cur_dir = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Direct TX delay: %s", DIR_TX_DELAY_LABELS[cur_dir]);
        lv_obj_t* btn_dir = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_dir, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_dir, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_dir, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_dir, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr float VALS[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
            static constexpr const char* LABELS[] = {"0 (off)","0.5","1.0","1.5","2.0"};
            static constexpr int N = 5;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (fabsf(np.direct_tx_delay_factor - VALS[i]) < 0.1f) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.direct_tx_delay_factor = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Direct TX delay: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Auto-advert interval
    {
        static constexpr uint8_t ADV_INT_VALUES[] = {0, 10, 20, 40, 60, 120};
        static constexpr const char* ADV_INT_LABELS[] = {"Disabled", "5 min", "10 min", "20 min", "30 min", "1 hour"};
        static constexpr int NUM_ADV_INT = 6;
        int cur_adv = 0;
        for (int i = 0; i < NUM_ADV_INT; i++) {
            if (p.advert_interval == ADV_INT_VALUES[i]) { cur_adv = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Auto-advert: %s", ADV_INT_LABELS[cur_adv]);
        lv_obj_t* btn_adv = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_adv, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_adv, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_adv, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_adv, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr uint8_t VALS[] = {0, 10, 20, 40, 60, 120};
            static constexpr const char* LABELS[] = {"Disabled","5 min","10 min","20 min","30 min","1 hour"};
            static constexpr int N = 6;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (np.advert_interval == VALS[i]) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.advert_interval = VALS[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Auto-advert: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Duty cycle
    {
        static constexpr uint8_t DUTY_VALUES[] = {0, 1, 5, 10, 25, 50, 100};
        static constexpr const char* DUTY_LABELS[] = {"Disabled", "1%", "5%", "10%", "25%", "50%", "100%"};
        static constexpr int NUM_DUTY = 7;
        int cur_dc = 0;
        for (int i = 0; i < NUM_DUTY; i++) {
            if (p.duty_cycle == DUTY_VALUES[i]) { cur_dc = i; break; }
        }
        snprintf(buf, sizeof(buf), "  Duty cycle: %s", DUTY_LABELS[cur_dc]);
        lv_obj_t* btn_dc = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_dc, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_dc, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_dc, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_dc, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            static constexpr uint8_t VALS[] = {0, 1, 5, 10, 25, 50, 100};
            static constexpr const char* LABELS[] = {"Disabled","1%","5%","10%","25%","50%","100%"};
            static constexpr int N = 7;
            int idx = 0;
            for (int i = 0; i < N; i++) {
                if (np.duty_cycle == VALS[i]) { idx = i; break; }
            }
            idx = (idx + 1) % N;
            np.duty_cycle = VALS[idx];
            sigurdos::prefs_set(np);
            sigurdos::mesh::setDutyCycle(VALS[idx]);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Duty cycle: %s", LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    show_screen(scr);
}

void settings_gps_show()
{
    lv_obj_t* scr = make_screen_full("GPS / Location");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    char buf[128];
    int row = 0;

    // GPS status
    snprintf(buf, sizeof(buf), "  GPS: %s", sigurdos_gps_has_fix() ? "Fix acquired" : "No fix");
    lv_obj_t* row0 = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
    lv_obj_set_style_bg_color(row0, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(row0, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(row0, lv_color_hex(TEXT_PRIMARY), 0);
    row++;

    // GPS enable toggle
    snprintf(buf, sizeof(buf), "  GPS: %s", p.gps_enabled ? "ON" : "OFF");
    lv_obj_t* btn_gps_en = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
    lv_obj_set_style_bg_color(btn_gps_en, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_gps_en, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_gps_en, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_gps_en, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.gps_enabled = !np.gps_enabled;
        sigurdos::prefs_set(np);
        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  GPS: %s", np.gps_enabled ? "ON" : "OFF");
        lv_obj_t* lbl = lv_obj_get_child(target, 1);
        if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
            lv_label_set_text(lbl, row_buf);
        }
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // GPS interval cycle
    {
        static constexpr uint16_t GPS_INT_VALUES[] = {0, 1, 5, 10, 30, 60};
        static constexpr const char* GPS_INT_LABELS[] = {"Every loop", "1s", "5s", "10s", "30s", "60s"};
        static constexpr int NUM_GPS_INT = 6;
        int cur_gps = 0;
        for (int i = 0; i < NUM_GPS_INT; i++) {
            if (p.gps_interval == GPS_INT_VALUES[i]) { cur_gps = i; break; }
        }
        snprintf(buf, sizeof(buf), "  GPS interval: %s", GPS_INT_LABELS[cur_gps]);
        lv_obj_t* btn_gps_int = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
        lv_obj_set_style_bg_color(btn_gps_int, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_bg_opa(btn_gps_int, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_gps_int, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_gps_int, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            int idx = 0;
            for (int i = 0; i < 6; i++) {
                if (np.gps_interval == (uint16_t[]){0,1,5,10,30,60}[i]) { idx = i; break; }
            }
            idx = (idx + 1) % 6;
            np.gps_interval = (uint16_t[]){0,1,5,10,30,60}[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  GPS interval: %s",
                     (const char*[]){"Every loop","1s","5s","10s","30s","60s"}[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Share location toggle
    snprintf(buf, sizeof(buf), "  Share location: %s", p.share_location ? "ON" : "OFF");
    lv_obj_t* btn_share = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
    lv_obj_set_style_bg_color(btn_share, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_share, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_share, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_share, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.share_location = !np.share_location;
        sigurdos::prefs_set(np);
        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Share location: %s", np.share_location ? "ON" : "OFF");
        lv_obj_t* lbl = lv_obj_get_child(target, 1);
        if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
            lv_label_set_text(lbl, row_buf);
        }
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    show_screen(scr);
}

void settings_display_show()
{
    lv_obj_t* scr = make_screen_full("Display / UI");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    char buf[128];
    int row = 0;

    // Keyboard backlight
    snprintf(buf, sizeof(buf), "  Keyboard BL: %d (%d%%)", p.kbd_backlight, p.kbd_backlight * 100 / 255);
    lv_obj_t* btn_bl = lv_list_add_btn(list, LV_SYMBOL_KEYBOARD, buf);
    lv_obj_set_style_bg_color(btn_bl, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_bl, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_bl, lv_color_hex(TEXT_PRIMARY), 0);
    g_backlight_row = btn_bl;
    lv_obj_add_event_cb(btn_bl, [](lv_event_t* e) {
        backlight_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                         (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Display brightness
    snprintf(buf, sizeof(buf), "  Display: %d (%d%%)", p.display_brightness, p.display_brightness * 100 / 255);
    lv_obj_t* btn_disp = lv_list_add_btn(list, LV_SYMBOL_IMAGE, buf);
    lv_obj_set_style_bg_color(btn_disp, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_disp, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_disp, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_disp, [](lv_event_t* e) {
        display_brightness_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                                 (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Auto-off timeout
    if (p.auto_off_timeout == 0) {
        snprintf(buf, sizeof(buf), "  Auto-off: Off");
    } else {
        snprintf(buf, sizeof(buf), "  Auto-off: %ds", p.auto_off_timeout);
    }
    lv_obj_t* btn_auto_off = lv_list_add_btn(list, LV_SYMBOL_IMAGE, buf);
    lv_obj_set_style_bg_color(btn_auto_off, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_auto_off, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_auto_off, lv_color_hex(TEXT_PRIMARY), 0);
    g_auto_off_row = btn_auto_off;
    lv_obj_add_event_cb(btn_auto_off, [](lv_event_t* e) {
        auto_off_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                       (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Chat history cap
    snprintf(buf, sizeof(buf), "  Chat history: %d messages", chat_screen_get_message_cap());
    lv_obj_t* btn_chat_cap = lv_list_add_btn(list, LV_SYMBOL_LIST, buf);
    lv_obj_set_style_bg_color(btn_chat_cap, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_chat_cap, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_chat_cap, lv_color_hex(TEXT_PRIMARY), 0);
    g_chat_history_row = btn_chat_cap;
    lv_obj_add_event_cb(btn_chat_cap, [](lv_event_t* e) {
        chat_message_cap_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)),
                               (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Theme selector
    {
        uint8_t cur_theme = sigurdos::prefs_get().theme_id;
        if (cur_theme >= NUM_THEMES) cur_theme = 0;
        snprintf(buf, sizeof(buf), "  Theme: %s", THEMES[cur_theme].name);
        lv_obj_t* btn_theme = lv_list_add_btn(list, LV_SYMBOL_IMAGE, buf);
        lv_obj_set_style_bg_color(btn_theme, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_theme, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_theme, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_theme, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            np.theme_id = (np.theme_id + 1) % NUM_THEMES;
            theme_apply(np.theme_id);
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  Theme: %s", THEMES[np.theme_id].name);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        g_backlight_row = nullptr;
        g_chat_history_row = nullptr;
        g_auto_off_row = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    show_screen(scr);
}

void settings_system_show()
{
    lv_obj_t* scr = make_screen_full("System");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    char buf[128];
    int row = 0;

    // Node name
    snprintf(buf, sizeof(buf), "  Name: %s", p.node_name);
    lv_obj_t* r0 = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
    lv_obj_set_style_bg_color(r0, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r0, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r0, lv_color_hex(TEXT_PRIMARY), 0);
    row++;

    // SD Card
    snprintf(buf, sizeof(buf), "  SD Card: %s", sigurdos_sdcard_mounted() ? "Mounted" : "Not mounted");
    lv_obj_t* r1 = lv_list_add_btn(list, LV_SYMBOL_SD_CARD, buf);
    lv_obj_set_style_bg_color(r1, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r1, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r1, lv_color_hex(TEXT_PRIMARY), 0);
    row++;

    // Date
    {
        int y, mo, d, h, mi;
        sigurdos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);
        snprintf(buf, sizeof(buf), "  Date: %04d-%02d-%02d", y, mo, d);
        lv_obj_t* btn_date = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_date, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_date, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_date, lv_color_hex(TEXT_PRIMARY), 0);
        g_date_row = btn_date;
        lv_obj_add_event_cb(btn_date, [](lv_event_t* e) {
            datetime_set_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)), true);
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Time
    {
        int y, mo, d, h, mi;
        sigurdos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);
        snprintf(buf, sizeof(buf), "  Time: %02d:%02d", h, mi);
        lv_obj_t* btn_time = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_time, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_time, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_time, lv_color_hex(TEXT_PRIMARY), 0);
        g_time_row = btn_time;
        lv_obj_add_event_cb(btn_time, [](lv_event_t* e) {
            datetime_set_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)), false);
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Run Setup Wizard
    lv_obj_t* btn_wizard = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "  Run Setup Wizard");
    lv_obj_set_style_bg_color(btn_wizard, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_wizard, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_wizard, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_wizard, [](lv_event_t*) {
        navigate_to(Screen::Onboarding);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Shut down
    lv_obj_t* btn_shutdown = lv_list_add_btn(list, LV_SYMBOL_POWER, "  Shut down");
    lv_obj_set_style_bg_color(btn_shutdown, lv_color_hex(0x4a2020), 0);
    lv_obj_set_style_bg_color(btn_shutdown, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_shutdown, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_shutdown, [](lv_event_t* e) {
        lv_obj_t* scr_sh = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        auto dlg_sz = dialog_size(240, 100);
        lv_obj_t* dlg = lv_obj_create(scr_sh);
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

        lv_obj_t* confirm_btn = lv_btn_create(dlg);
        lv_obj_set_size(confirm_btn, 64, 24);
        lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
        lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(confirm_btn, 0, 0);
        lv_obj_t* cfl = lv_label_create(confirm_btn);
        lv_label_set_text(cfl, "Shut down");
        lv_obj_center(cfl);
        lv_obj_add_event_cb(confirm_btn, [](lv_event_t*) {
            sigurdos::mesh::shutdown();
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Factory reset
    lv_obj_t* btn_reset = lv_list_add_btn(list, LV_SYMBOL_WARNING, "  Factory reset");
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x4a2020), 0);
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_reset, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_reset, [](lv_event_t* e) {
        lv_obj_t* scr_fr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        auto dlg_sz = dialog_size(250, 120);
        lv_obj_t* dlg = lv_obj_create(scr_fr);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(DIVIDER), 0);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);
        lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, "Factory reset?");
        lv_obj_set_style_text_color(title, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* msg = lv_label_create(dlg);
        lv_label_set_text(msg, "Erase all data and reboot?\nAll settings, contacts and\nidentity will be lost.");
        lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_10, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 22);

        // Cancel
        lv_obj_t* cancel_btn = lv_btn_create(dlg);
        lv_obj_set_size(cancel_btn, 100, 28);
        lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_radius(cancel_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(cancel_btn);
        lv_label_set_text(cl, "Cancel");
        lv_obj_center(cl);
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ev) {
            lv_obj_del(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
        }, LV_EVENT_CLICKED, nullptr);

        // Confirm (red, dangerous)
        lv_obj_t* confirm_btn = lv_btn_create(dlg);
        lv_obj_set_size(confirm_btn, 100, 28);
        lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
        lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(confirm_btn, 0, 0);
        lv_obj_t* cfl = lv_label_create(confirm_btn);
        lv_label_set_text(cfl, "Reset");
        lv_obj_center(cfl);
        lv_obj_add_event_cb(confirm_btn, [](lv_event_t*) {
            sigurdos::mesh::factoryReset();
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;
    snprintf(buf, sizeof(buf), "  SigurdOS " SIGURDOS_VERSION);
    lv_obj_t* rv = lv_list_add_btn(list, LV_SYMBOL_HOME, buf);
    lv_obj_set_style_bg_color(rv, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(rv, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(rv, lv_color_hex(TEXT_PRIMARY), 0);
    row++;

    // Null row pointers on delete
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        g_date_row = nullptr;
        g_time_row = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    show_screen(scr);
}

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
    lv_obj_set_style_text_font(r0, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(r0, 4, 0);
    lv_obj_set_style_pad_bottom(r0, 4, 0);
    row++;

    snprintf(buf, sizeof(buf), "  Sent direct:  %u", sigurdos::mesh::getNumSentDirect());
    lv_obj_t* r1 = lv_list_add_btn(list, LV_SYMBOL_UPLOAD, buf);
    lv_obj_set_style_bg_color(r1, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r1, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r1, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r1, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(r1, 4, 0);
    lv_obj_set_style_pad_bottom(r1, 4, 0);
    row++;

    // ── Received ──
    snprintf(buf, sizeof(buf), "  Recv flood:   %u", sigurdos::mesh::getNumRecvFlood());
    lv_obj_t* r2 = lv_list_add_btn(list, LV_SYMBOL_DOWNLOAD, buf);
    lv_obj_set_style_bg_color(r2, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r2, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r2, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(r2, 4, 0);
    lv_obj_set_style_pad_bottom(r2, 4, 0);
    row++;

    snprintf(buf, sizeof(buf), "  Recv direct:  %u", sigurdos::mesh::getNumRecvDirect());
    lv_obj_t* r3 = lv_list_add_btn(list, LV_SYMBOL_DOWNLOAD, buf);
    lv_obj_set_style_bg_color(r3, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r3, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r3, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r3, &lv_font_montserrat_12, 0);
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
    lv_obj_set_style_text_font(r4, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(r4, 4, 0);
    lv_obj_set_style_pad_bottom(r4, 4, 0);
    row++;

    snprintf(buf, sizeof(buf), "  RX airtime:   %lu ms", rx_air);
    lv_obj_t* r5 = lv_list_add_btn(list, LV_SYMBOL_CHARGE, buf);
    lv_obj_set_style_bg_color(r5, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r5, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r5, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r5, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(r5, 4, 0);
    lv_obj_set_style_pad_bottom(r5, 4, 0);
    row++;

    // ── Budget & ACKs ──
    snprintf(buf, sizeof(buf), "  TX budget:    %lu ms", sigurdos::mesh::getRemainingTxBudget());
    lv_obj_t* r6 = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
    lv_obj_set_style_bg_color(r6, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r6, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r6, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r6, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(r6, 4, 0);
    lv_obj_set_style_pad_bottom(r6, 4, 0);
    row++;

    snprintf(buf, sizeof(buf), "  ACKs received: %d", sigurdos::mesh::getAckCounter());
    lv_obj_t* r7 = lv_list_add_btn(list, LV_SYMBOL_OK, buf);
    lv_obj_set_style_bg_color(r7, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r7, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r7, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(r7, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(r7, 4, 0);
    lv_obj_set_style_pad_bottom(r7, 4, 0);
    row++;

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
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* msg = lv_label_create(dlg);
        lv_label_set_text(msg, "All counters will be zeroed.");
        lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_10, 0);
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

// ════════════════════════════════════════════════════════
// Settings — category menu
// ════════════════════════════════════════════════════════
void settings_screen_show()
{
    lv_obj_t* scr = make_screen_full("Settings");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    // Category helpers — compact row with accent icon
    struct Cat { const char* icon; const char* label; Screen target; };
    Cat cats[] = {
        {LV_SYMBOL_WIFI,    "Radio / Mesh",     Screen::SettingsRadio},
        {LV_SYMBOL_GPS,     "GPS / Location",   Screen::SettingsGPS},
        {LV_SYMBOL_IMAGE,   "Display / UI",     Screen::SettingsDisplay},
        {LV_SYMBOL_SETTINGS,"System",           Screen::SettingsSystem},
        {LV_SYMBOL_SETTINGS,"Node Stats",       Screen::NodeStats},
    };

    for (int i = 0; i < 5; i++) {
        lv_obj_t* btn = lv_list_add_btn(list, cats[i].icon, cats[i].label);
        lv_obj_set_style_bg_color(btn, lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_t* arrow = lv_label_create(btn);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arrow, lv_color_hex(TEXT_MUTED), 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -4, 0);
        Screen target = cats[i].target;
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            Screen s = (Screen)(intptr_t)lv_event_get_user_data(e);
            navigate_to(s);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)target);
    }

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
        if (first) lv_obj_del_async(first);
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
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    term_add_line(log, "SigurdOS T-Deck Terminal");
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
    apply_focus_style(input);

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
        static char echo[280];
        snprintf(echo, sizeof(echo), "> %s", cmd);
        term_add_line(log_cont, echo);

        static char result[256];
        if (strcmp(cmd, "help") == 0) {
            snprintf(result, sizeof(result), "Commands: help status advert ping anon fetchmsgs groupdata emoji-list getvar setvar delvar listvars");
        } else if (strncmp(cmd, "getvar ", 7) == 0) {
            const char* key = cmd + 7;
            if (!key[0]) {
                snprintf(result, sizeof(result), "Usage: getvar <key>");
            } else {
                File f = SPIFFS.open("/custom_vars.txt", "r");
                bool found = false;
                if (f) {
                    while (f.available()) {
                        String line = f.readStringUntil('\n');
                        line.trim();
                        if (line.startsWith(key) && line.charAt(strlen(key)) == '=') {
                            const char* val = line.c_str() + strlen(key) + 1;
                            snprintf(result, sizeof(result), "%s = %s", key, val);
                            found = true;
                            break;
                        }
                    }
                    f.close();
                }
                if (!found) {
                    snprintf(result, sizeof(result), "Key '%s' not found", key);
                }
            }
        } else if (strncmp(cmd, "setvar ", 7) == 0) {
            const char* arg = cmd + 7;
            const char* sep = strchr(arg, ' ');
            if (!sep || sep == arg) {
                snprintf(result, sizeof(result), "Usage: setvar <key> <value>");
            } else {
                char key[48]; size_t klen = (size_t)(sep - arg);
                if (klen > 47) klen = 47;
                memcpy(key, arg, klen); key[klen] = '\0';
                const char* value = sep + 1;
                // Read existing vars, update or append
                String all;
                File f = SPIFFS.open("/custom_vars.txt", "r");
                if (f) {
                    while (f.available()) {
                        String line = f.readStringUntil('\n');
                        line.trim();
                        if (line.length() > 0) {
                            if (!line.startsWith(key) || line.charAt(strlen(key)) != '=') {
                                all += line + "\n";
                            }
                        }
                    }
                    f.close();
                }
                all += String(key) + "=" + value + "\n";
                File wf = SPIFFS.open("/custom_vars.txt", "w");
                if (wf) { wf.print(all); wf.close(); }
                snprintf(result, sizeof(result), "Set: %s = %s", key, value);
            }
        } else if (strncmp(cmd, "delvar ", 7) == 0) {
            const char* key = cmd + 7;
            if (!key[0]) {
                snprintf(result, sizeof(result), "Usage: delvar <key>");
            } else {
                String all;
                bool found = false;
                File f = SPIFFS.open("/custom_vars.txt", "r");
                if (f) {
                    while (f.available()) {
                        String line = f.readStringUntil('\n');
                        line.trim();
                        if (line.length() > 0) {
                            if (line.startsWith(key) && line.charAt(strlen(key)) == '=') {
                                found = true;
                            } else {
                                all += line + "\n";
                            }
                        }
                    }
                    f.close();
                }
                if (found) {
                    File wf = SPIFFS.open("/custom_vars.txt", "w");
                    if (wf) { wf.print(all); wf.close(); }
                    snprintf(result, sizeof(result), "Deleted: %s", key);
                } else {
                    snprintf(result, sizeof(result), "Key '%s' not found", key);
                }
            }
        } else if (strcmp(cmd, "listvars") == 0) {
            File f = SPIFFS.open("/custom_vars.txt", "r");
            if (!f) {
                snprintf(result, sizeof(result), "No custom variables set");
            } else {
                int count = 0;
                char lb[128];
                while (f.available()) {
                    String line = f.readStringUntil('\n');
                    line.trim();
                    if (line.length() > 0 && line.indexOf('=') > 0) {
                        snprintf(lb, sizeof(lb), "  %s", line.c_str());
                        term_add_line(log_cont, lb);
                        count++;
                    }
                }
                f.close();
                snprintf(result, sizeof(result), "%d variable%s", count, count == 1 ? "" : "s");
            }
        } else if (strcmp(cmd, "status") == 0) {
            int rssi  = sigurdos::mesh::getLastRSSI();
            float snr = sigurdos::mesh::getLastSNR();
            int noise = sigurdos::mesh::getNoiseFloor();
            snprintf(result, sizeof(result),
                "RSSI:%ddBm SNR:%.1fdB Noise:%ddBm  Contacts:%d Channels:%d",
                rssi, snr, noise,
                sigurdos::mesh::getContactCount(),
                sigurdos::mesh::getChannelCount());
        } else if (strcmp(cmd, "advert") == 0) {
            bool ok = sigurdos::mesh::sendAdvert();
            snprintf(result, sizeof(result), ok ? "Advert sent" : "Send failed");
        } else if (strcmp(cmd, "ping") == 0) {
            snprintf(result, sizeof(result), "Pong! Uptime: %lums", millis());
        } else if (strncmp(cmd, "anon ", 5) == 0) {
            const char* anon_arg = cmd + 5;
            const char* anon_space = strchr(anon_arg, ' ');
            if (!anon_space || anon_space == anon_arg) {
                snprintf(result, sizeof(result), "Usage: anon <64hex_pubkey> <text>");
            } else {
                char pubkey_hex[65];
                size_t pklen = (size_t)(anon_space - anon_arg);
                if (pklen > 64) pklen = 64;
                memcpy(pubkey_hex, anon_arg, pklen);
                pubkey_hex[pklen] = '\0';
                const char* msg = anon_space + 1;
                if (sigurdos::mesh::sendAnonMessage(pubkey_hex, msg)) {
                    snprintf(result, sizeof(result), "Anon msg sent to %s", pubkey_hex);
                } else {
                    snprintf(result, sizeof(result), "Send failed (bad hex? %zu chars)", pklen);
                }
            }
        } else if (strncmp(cmd, "fetchmsgs ", 10) == 0) {
            const char* fetch_arg = cmd + 10;
            const char* fetch_space = strchr(fetch_arg, ' ');
            if (!fetch_space || fetch_space == fetch_arg) {
                snprintf(result, sizeof(result), "Usage: fetchmsgs <contact> <channel>");
            } else {
                char contact_name[64];
                size_t cn_len = (size_t)(fetch_space - fetch_arg);
                if (cn_len > 63) cn_len = 63;
                memcpy(contact_name, fetch_arg, cn_len);
                contact_name[cn_len] = '\0';
                const char* channel = fetch_space + 1;
                if (sigurdos::mesh::sendRoomMsgFetchRequest(contact_name, channel)) {
                    snprintf(result, sizeof(result), "Room fetch sent to %s for %s", contact_name, channel);
                } else {
                    snprintf(result, sizeof(result), "Fetch failed (contact '%s' not found?)", contact_name);
                }
            }
        } else if (strncmp(cmd, "groupdata ", 10) == 0) {
            // Format: groupdata <channel_idx> <type_hex> <hex_payload>
            const char* gd_arg = cmd + 10;
            int ch_idx = atoi(gd_arg);
            const char* after_ch = gd_arg;
            while (*after_ch && *after_ch != ' ') after_ch++;
            if (!*after_ch) {
                snprintf(result, sizeof(result), "Usage: groupdata <channel_idx> <type_hex> <hex_payload>");
            } else {
                after_ch++; // skip space
                unsigned int dt = 0;
                if (sscanf(after_ch, "%x", &dt) != 1 || dt > 0xFFFF) {
                    snprintf(result, sizeof(result), "Bad type hex");
                } else {
                    const char* hex_start = after_ch;
                    while (*hex_start && *hex_start != ' ') hex_start++;
                    if (!*hex_start) {
                        snprintf(result, sizeof(result), "Usage: groupdata <channel_idx> <type_hex> <hex_payload>");
                    } else {
                        hex_start++; // skip space
                        uint8_t payload[64];
                        int plen = sigurdos::mesh::hexToBytes(hex_start, payload, sizeof(payload));
                        if (plen <= 0) {
                            snprintf(result, sizeof(result), "Bad hex payload");
                        } else {
                            bool ok = sigurdos::mesh::sendGroupDataToChannel(
                                ch_idx, (uint16_t)dt, payload, plen);
                            snprintf(result, sizeof(result), ok
                                ? "Group data sent to ch%d type=0x%04x (%d bytes)"
                                : "Send failed (bad channel? %d)", ch_idx, dt, plen);
                        }
                    }
                }
            }
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
    sigurdos::mesh::clearTraceResult();
    trace_result_label = nullptr;

    lv_obj_add_event_cb(scr, trace_screen_delete_cb, LV_EVENT_DELETE, nullptr);

    char names[32][32];
    int total = sigurdos::mesh::exportContacts(names, 32);

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
        bool has_path = sigurdos::mesh::contactHasPath(i);

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
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
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
                if (sigurdos::mesh::sendTrace(idx, &tag)) {
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
                        if (sigurdos::mesh::hasTraceResult()) {
                            uint8_t len = sigurdos::mesh::getTracePathLen();
                            if (len > 64) len = 64;  // MAX_PATH_SIZE
                            uint8_t snrs[64], hashes[64];
                            sigurdos::mesh::getTracePath(snrs, hashes);
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
                            sigurdos::mesh::clearTraceResult();
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
// Channels — channel list with create/join and delete
// ════════════════════════════════════════════════════════
static std::function<void()> g_channels_rebuild = nullptr;  // set by channels_screen_show

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
    apply_focus_style(name_input);

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

        bool ok = sigurdos::mesh::addHashtagChannel(name);
        if (ok) {
            lv_obj_del_async(dlg);
            if (g_channels_rebuild) g_channels_rebuild();
        } else {
            if (fb) lv_label_set_text(fb, "Invalid or full");
        }
    }, LV_EVENT_CLICKED, (void*)feedback);

    return dialog;
}

void channels_screen_show()
{
    lv_obj_t* scr = make_screen_full("Channels");

    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - 36);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    std::function<void()> rebuild = [list, &rebuild]() {
        lv_obj_clean(list);
        char names[8][32];
        int n = sigurdos::mesh::exportChannels(names, 8);
        if (n == 0) {
            auto* item = lv_list_add_btn(list, LV_SYMBOL_WARNING, "No channels joined");
            lv_obj_set_style_bg_color(item, lv_color_hex(BG_TERTIARY), 0);
        } else {
            for (int i = 0; i < n; i++) {
                lv_obj_t* row = lv_obj_create(list);
                lv_obj_set_size(row, LV_PCT(100), 36);
                lv_obj_set_style_bg_color(row,
                    lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_pad_all(row, 0, 0);
                lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                lv_obj_t* icon = lv_label_create(row);
                lv_label_set_text(icon, LV_SYMBOL_LIST);
                lv_obj_set_style_text_color(icon, lv_color_hex(ACCENT), 0);
                lv_obj_set_style_text_font(icon, &lv_font_montserrat_12, 0);
                lv_obj_set_style_pad_left(icon, 8, 0);

                lv_obj_t* name_l = lv_label_create(row);
                lv_label_set_text(name_l, names[i]);
                lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
                lv_obj_set_style_text_font(name_l, &lv_font_montserrat_12, 0);
                lv_obj_set_flex_grow(name_l, 1);
                lv_obj_set_style_pad_left(name_l, 8, 0);

                // Delete button (hidden if only 1 channel remaining)
                if (n > 1) {
                    lv_obj_t* del_btn = lv_btn_create(row);
                    lv_obj_set_size(del_btn, 28, 24);
                    lv_obj_set_style_bg_color(del_btn, lv_color_hex(ACCENT_RED), 0);
                    lv_obj_set_style_radius(del_btn, 0, 0);
                    lv_obj_set_style_border_width(del_btn, 0, 0);
                    lv_obj_set_style_pad_all(del_btn, 0, 0);
                    auto* dl = lv_label_create(del_btn);
                    lv_label_set_text(dl, LV_SYMBOL_CLOSE);
                    lv_obj_set_style_text_font(dl, &lv_font_montserrat_10, 0);
                    lv_obj_center(dl);
                    lv_obj_set_style_pad_right(del_btn, 4, 0);
                    int ch_idx = i;
                    lv_obj_add_event_cb(del_btn, [](lv_event_t* e) {
                        int idx = (int)(intptr_t)lv_event_get_user_data(e);
                        sigurdos::mesh::removeChannel(idx);
                        if (g_channels_rebuild) g_channels_rebuild();
                    }, LV_EVENT_CLICKED, (void*)(intptr_t)ch_idx);
                }
            }
        }
    };

    g_channels_rebuild = rebuild;
    rebuild();

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
        lv_obj_t* s = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        channel_create_dialog(s);
    }, LV_EVENT_CLICKED, nullptr);

    // Null the rebuild callback when this screen is destroyed
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        g_channels_rebuild = nullptr;
    }, LV_EVENT_DELETE, nullptr);

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
        sigurdos::mesh::sendAdvert();
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
static bool  s_rx_gain    = false;  // RX boosted gain toggle state
static uint8_t s_duty_cycle = 0;    // duty cycle percentage (0 = disabled)

void custom_rf_screen_show()
{
    using namespace sigurdos::theme;
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
        apply_focus_style(ta);
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

        // Guard: all 5 textareas must have been found — layout change or
        // allocation failure leaves dangling pointers that would crash below.
        if (found != 5) {
            lv_obj_t* el = lv_obj_get_child(scr, lv_obj_get_child_cnt(scr) - 2);
            if (lv_obj_check_type(el, &lv_label_class)) {
                lv_label_set_text(el, "Error: textarea not found");
            }
            return;
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

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();

    s_rf_freq = p.configured ? p.freq          : 869.618f;
    s_rf_sf   = p.configured ? p.sf            : 8;
    s_rf_bw   = p.configured ? p.bw            : 62.5f;
    s_rf_cr   = p.configured ? p.cr            : 5;
    s_rf_pwr  = p.configured ? p.tx_power_dbm  : 22;
    s_rx_gain    = p.rx_boosted_gain;
    s_duty_cycle = p.duty_cycle;

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

    // ── RX boosted gain toggle ──────────────────────
    snprintf(buf, sizeof(buf), "RX Gain: %s", s_rx_gain ? "BOOST" : "NORMAL");
    auto* gain_lbl = lv_label_create(scr);
    lv_label_set_text(gain_lbl, buf);
    lv_obj_set_style_text_color(gain_lbl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(gain_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(gain_lbl, LV_ALIGN_TOP_LEFT, rx, ry);

    auto* gain_toggle = lv_btn_create(scr);
    lv_obj_set_size(gain_toggle, 48, 20);
    lv_obj_align(gain_toggle, LV_ALIGN_TOP_LEFT, rx + rw - 52, ry - 2);
    lv_obj_set_style_bg_color(gain_toggle, lv_color_hex(s_rx_gain ? ACCENT : ACCENT_RED), 0);
    lv_obj_set_style_radius(gain_toggle, 0, 0);
    auto* gtl = lv_label_create(gain_toggle);
    lv_label_set_text(gtl, s_rx_gain ? "ON" : "OFF");
    lv_obj_center(gtl);
    lv_obj_add_event_cb(gain_toggle, [](lv_event_t* e) {
        s_rx_gain = !s_rx_gain;
        lv_obj_t* lbl = (lv_obj_t*)lv_event_get_user_data(e);
        char b[32]; snprintf(b, sizeof(b), "RX Gain: %s", s_rx_gain ? "BOOST" : "NORMAL");
        lv_label_set_text(lbl, b);
        // Update toggle button appearance
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_set_style_bg_color(target, lv_color_hex(s_rx_gain ? ACCENT : ACCENT_RED), 0);
        lv_obj_t* bl = lv_obj_get_child(target, 0);
        if (bl && lv_obj_check_type(bl, &lv_label_class)) {
            lv_label_set_text(bl, s_rx_gain ? "ON" : "OFF");
        }
    }, LV_EVENT_CLICKED, (void*)gain_lbl);
    ry += 24;

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
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.freq         = s_rf_freq;
        np.bw           = s_rf_bw;
        np.sf           = (uint8_t)s_rf_sf;
        np.cr           = (uint8_t)s_rf_cr;
        np.tx_power_dbm = (int8_t)s_rf_pwr;
        np.rx_boosted_gain = s_rx_gain;
        np.duty_cycle   = s_duty_cycle;
        np.configured   = true;
        sigurdos::prefs_set(np);
        sigurdos::mesh::saveChannels();
        chat_save_messages();
        delay(100); // allow flash writes to complete before restart
        ESP.restart();
    }, LV_EVENT_CLICKED, nullptr);

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Telemetry screen (Phase 4.3)
// ════════════════════════════════════════════════════════
void telemetry_screen_show()
{
    static constexpr int ROW_H = 20;
    lv_obj_t* scr = make_screen_full("Telemetry");

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
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);

        lv_obj_t* val = lv_label_create(row);
        lv_label_set_text(val, value);
        lv_obj_set_style_text_color(val, lv_color_hex(color), 0);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -8, 0);
    };

    if (sigurdos::mesh::hasTelemetryResponse()) {
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
    } else {
        lv_obj_t* waiting = lv_label_create(list);
        lv_label_set_text(waiting, "Requesting telemetry...\nWaiting for response...");
        lv_obj_set_style_text_color(waiting, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(waiting, &lv_font_montserrat_12, 0);
        lv_obj_align(waiting, LV_ALIGN_CENTER, 0, 0);
    }

    show_screen(scr);
}

// ════════════════════════════════════════════════════════
// Node Status screen (Phase 4.2)
// ════════════════════════════════════════════════════════
void node_status_screen_show()
{
    static constexpr int ROW_H = 18;
    lv_obj_t* scr = make_screen_full("Node Status");

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
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);

        lv_obj_t* val = lv_label_create(row);
        lv_label_set_text(val, value);
        lv_obj_set_style_text_color(val, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -8, 0);
    };

    if (sigurdos::mesh::hasStatusResponse()) {
        sigurdos::mesh::NodeStatus st;
        sigurdos::mesh::getStatusResult(&st);

        char buf[32];

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
    } else {
        lv_obj_t* waiting = lv_label_create(list);
        lv_label_set_text(waiting, "Requesting status...\nWaiting for response...");
        lv_obj_set_style_text_color(waiting, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(waiting, &lv_font_montserrat_12, 0);
        lv_obj_align(waiting, LV_ALIGN_CENTER, 0, 0);
    }

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

} // namespace sigurdos::ui
