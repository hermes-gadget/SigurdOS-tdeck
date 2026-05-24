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


#include "chat_screen.h"
#include "navigation.h"
#include "theme.h"
#include "responsive.h"
#include "../hal/tdeck_pins.h"
#include "../hal/battery.h"
#include "../mesh/mesh_wrapper.h"
#include <lvgl.h>
#include <cstring>
#include <cstdio>

namespace slopos::ui {

using namespace theme;

static constexpr lv_obj_flag_t no_scroll_flags()
{
    return (lv_obj_flag_t)(
        LV_OBJ_FLAG_SCROLLABLE |
        LV_OBJ_FLAG_SCROLL_ELASTIC |
        LV_OBJ_FLAG_SCROLL_MOMENTUM |
        LV_OBJ_FLAG_SCROLL_CHAIN |
        LV_OBJ_FLAG_SCROLL_ON_FOCUS |
        LV_OBJ_FLAG_SCROLL_WITH_ARROW);
}

static void disable_scroll(lv_obj_t* obj)
{
    lv_obj_remove_flag(obj, no_scroll_flags());
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void stabilize_topbar_pill(lv_obj_t* obj)
{
    disable_scroll(obj);
    lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(obj, 0, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_EDITED));
}

// ── Messaging-view widgets ─────────────────────────────────
static lv_obj_t* scr            = nullptr;
static lv_obj_t* top_bar        = nullptr;
static lv_obj_t* channel_ribbon = nullptr;
static lv_obj_t* msg_list       = nullptr;
static lv_obj_t* input_bar      = nullptr;
static lv_obj_t* input_field    = nullptr;

// ── Messaging-view layout ──────────────────────────────────
using responsive::TOP_BAR_H;
using responsive::BOT_BAR_H;
using responsive::DIVIDER_H;
using responsive::DISPLAY_H;
using responsive::DISPLAY_W;
using responsive::HASHTAG_LABEL_W;
using responsive::CONTENT_W;
using responsive::dialog_size;
static constexpr int TOP_H      = TOP_BAR_H;
static constexpr int INPUT_H    = 35;
// BOT_BAR_H, DIVIDER_H used directly from responsive namespace
static constexpr int BUBBLE_PAD = 6;
static constexpr int MAX_MSGS   = 8;
static constexpr int MSG_LIST_Y = TOP_H + DIVIDER_H;
static constexpr int MSG_LIST_H = DISPLAY_H - TOP_H - DIVIDER_H - INPUT_H - DIVIDER_H - BOT_BAR_H;

// ── Channel-list layout (matches screens.cpp constants) ────
static constexpr int LIST_BAR_H  = 22;
static constexpr int LIST_DIV_H  = 1;
static constexpr int LIST_CONT_Y = LIST_BAR_H + LIST_DIV_H;   // 23
static constexpr int LIST_CONT_H = DISPLAY_H - LIST_CONT_Y - LIST_DIV_H - BOT_BAR_H; // 196
static constexpr int LIST_ROW_H  = 44;

// ── Channel state ──────────────────────────────────────────
static constexpr int MAX_CHANNELS = 16;
static char  dyn_channels[MAX_CHANNELS][32];
static int   dyn_count      = 0;
static int   active_channel = 0;

// ── Per-channel metadata ───────────────────────────────────
struct ChannelMeta {
    char     preview[64];
    uint32_t timestamp;
    int      unread;
};
static ChannelMeta ch_meta[MAX_CHANNELS];

struct ChannelMessage {
    char     sender[32];
    char     text[160];
    uint32_t timestamp;
    bool     is_self;
};
static ChannelMessage ch_msgs[MAX_CHANNELS][MAX_MSGS];
static uint8_t        ch_msg_count[MAX_CHANNELS];

// ── Forward declarations ───────────────────────────────────
static void show_channel_list(lv_scr_load_anim_t anim);
static void open_channel_messaging(int idx);
static void rebuild_channel_ribbon();
static void show_add_channel_options(lv_obj_t* parent);
static void render_active_messages();

static int channel_pill_width(const char* name)
{
    const size_t len = name ? strnlen(name, 31) : 0;
    int width = 20 + (int)len * 7;
    if (width < 48) width = 48;
    if (width > 112) width = 112;
    return width;
}

static lv_obj_t* create_channel_pill(lv_obj_t* parent, int idx)
{
    const bool selected = idx == active_channel;
    const int width = channel_pill_width(dyn_channels[idx]);

    lv_obj_t* pill = lv_obj_create(parent);
    lv_obj_set_size(pill, width, TOP_H - 8);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(pill, 0, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_set_style_shadow_width(pill, 0, 0);
    lv_obj_set_style_outline_width(pill, 0, 0);
    stabilize_topbar_pill(pill);

    const lv_color_t bg = lv_color_hex(selected ? ACCENT : BG_TERTIARY);
    lv_obj_set_style_bg_color(pill, bg, 0);
    lv_obj_set_style_bg_color(pill, bg, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(pill, bg, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_STATE_FOCUSED);

    lv_obj_t* label = lv_label_create(pill);
    lv_label_set_text(label, dyn_channels[idx]);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width - 8);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(label,
        selected ? lv_color_hex(0xffffff) : lv_color_hex(CHANNEL_HASH), 0);
    disable_scroll(label);
    lv_obj_center(label);

    lv_obj_add_event_cb(pill, [](lv_event_t* e) {
        int ch = (int)(intptr_t)lv_event_get_user_data(e);
        active_channel = ch;
        ch_meta[ch].unread = 0;
        rebuild_channel_ribbon();
        render_active_messages();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    return pill;
}

// ════════════════════════════════════════════════════
// Dynamic channels — pulled from mesh, sorted by MRU
// ════════════════════════════════════════════════════
static void refresh_channels()
{
    dyn_count = slopos::mesh::exportChannels(dyn_channels, MAX_CHANNELS);
    if (dyn_count == 0) {
        if (slopos::mesh::joinPublicChannel()) {
            dyn_count = slopos::mesh::exportChannels(dyn_channels, MAX_CHANNELS);
        }
        if (dyn_count == 0) {
            strncpy(dyn_channels[0], "#general", 31);
            dyn_count = 1;
        }
    }
    if (active_channel >= dyn_count) active_channel = 0;
}

static void mark_channel_used(int idx)
{
    if (idx >= 0 && idx < dyn_count) active_channel = idx;
}

// ════════════════════════════════════════════════════
// Timestamp helper
// ════════════════════════════════════════════════════
static void format_time(char* buf, size_t sz, uint32_t epoch)
{
    if (epoch == 0) { snprintf(buf, sz, "--:--"); return; }
    uint32_t t = epoch % 86400;
    snprintf(buf, sz, "%02d:%02d", (t / 3600) % 24, (t / 60) % 60);
}

static void populate_channel_rows(lv_obj_t* list) {
    for (int i = 0; i < dyn_count; i++) {
        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_PCT(100), LIST_ROW_H);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* avatar = lv_obj_create(row);
        lv_obj_set_size(avatar, 32, 32);
        lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_set_style_bg_color(avatar, lv_color_hex(0x5865F2), 0);
        lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(avatar, 0, 0);
        lv_obj_set_style_border_width(avatar, 0, 0);
        lv_obj_clear_flag(avatar, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* hash = lv_label_create(avatar);
        lv_label_set_text(hash, "#");
        lv_obj_set_style_text_color(hash, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(hash, &lv_font_montserrat_12, 0);
        lv_obj_center(hash);

        lv_obj_t* name_lbl = lv_label_create(row);
        lv_label_set_text(name_lbl, dyn_channels[i]);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);
        lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 46, 6);

        if (ch_meta[i].timestamp > 0) {
            char tbuf[8];
            format_time(tbuf, sizeof(tbuf), ch_meta[i].timestamp);
            lv_obj_t* ts = lv_label_create(row);
            lv_label_set_text(ts, tbuf);
            lv_obj_set_style_text_color(ts, lv_color_hex(TEXT_MUTED), 0);
            lv_obj_set_style_text_font(ts, &lv_font_montserrat_10, 0);
            lv_obj_align(ts, LV_ALIGN_TOP_RIGHT,
                ch_meta[i].unread > 0 ? -26 : -4, 8);
        }

        lv_obj_t* prev = lv_label_create(row);
        lv_label_set_text(prev,
            ch_meta[i].preview[0] ? ch_meta[i].preview : "No messages yet");
        lv_obj_set_style_text_color(prev, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(prev, &lv_font_montserrat_10, 0);
        lv_label_set_long_mode(prev, LV_LABEL_LONG_DOT);
        lv_obj_set_width(prev, CONTENT_W - 70);
        lv_obj_align(prev, LV_ALIGN_TOP_LEFT, 46, 26);

        if (ch_meta[i].unread > 0) {
            lv_obj_t* badge = lv_obj_create(row);
            lv_obj_set_size(badge, 18, 18);
            lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -4, 0);
            lv_obj_set_style_bg_color(badge, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(badge, 0, 0);
            lv_obj_set_style_border_width(badge, 0, 0);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

            char cnt_buf[4];
            int cnt = ch_meta[i].unread;
            if (cnt > 9) snprintf(cnt_buf, sizeof(cnt_buf), "9+");
            else         snprintf(cnt_buf, sizeof(cnt_buf), "%d", cnt);
            lv_obj_t* cnt_lbl = lv_label_create(badge);
            lv_label_set_text(cnt_lbl, cnt_buf);
            lv_obj_set_style_text_color(cnt_lbl, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_text_font(cnt_lbl, &lv_font_montserrat_10, 0);
            lv_obj_center(cnt_lbl);
        }

        int ch_idx = i;
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            ch_meta[idx].unread = 0;
            open_channel_messaging(idx);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)ch_idx);
    }
}

static int find_channel_idx(const char* channel)
{
    if (!channel || !channel[0]) return active_channel;
    for (int i = 0; i < dyn_count; i++) {
        if (strcmp(dyn_channels[i], channel) == 0) return i;
    }
    return -1;
}

static void update_channel_meta(int idx, const char* text, uint32_t timestamp)
{
    if (idx < 0 || idx >= MAX_CHANNELS) return;
    strncpy(ch_meta[idx].preview, text ? text : "", sizeof(ch_meta[idx].preview) - 1);
    ch_meta[idx].preview[sizeof(ch_meta[idx].preview) - 1] = '\0';
    ch_meta[idx].timestamp = timestamp;
}

static void append_channel_message(int idx, const char* sender, const char* text,
                                   uint32_t timestamp, bool is_self)
{
    if (idx < 0 || idx >= MAX_CHANNELS) return;

    uint8_t pos = ch_msg_count[idx];
    if (pos >= MAX_MSGS) {
        for (int i = 1; i < MAX_MSGS; i++) ch_msgs[idx][i - 1] = ch_msgs[idx][i];
        pos = MAX_MSGS - 1;
    } else {
        ch_msg_count[idx]++;
    }

    ChannelMessage& msg = ch_msgs[idx][pos];
    strncpy(msg.sender, sender ? sender : "", sizeof(msg.sender) - 1);
    msg.sender[sizeof(msg.sender) - 1] = '\0';
    strncpy(msg.text, text ? text : "", sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';
    msg.timestamp = timestamp;
    msg.is_self = is_self;

    update_channel_meta(idx, msg.text, timestamp);
}

static lv_obj_t* make_chat_list_screen()
{
    lv_obj_t* s = lv_obj_create(nullptr);
    apply_dark_bg(s);

    // Top bar
    lv_obj_t* top = lv_obj_create(s);
    lv_obj_set_size(top, LV_PCT(100), LIST_BAR_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);

    // ← back button → go back to Home
    lv_obj_t* back = lv_btn_create(top);
    lv_obj_set_size(back, 24, LIST_BAR_H - 4);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(back, 0, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_t* bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(bl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
    lv_obj_center(bl);
    disable_scroll(bl);
    lv_obj_add_event_cb(back, [](lv_event_t*) { go_back(); }, LV_EVENT_CLICKED, nullptr);

    // Channel hashtags snapshot
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
    lv_obj_set_width(ch_lbl, HASHTAG_LABEL_W());
    lv_obj_set_style_text_color(ch_lbl, lv_color_hex(CHANNEL_HASH), 0);
    lv_obj_set_style_text_font(ch_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(ch_lbl, LV_ALIGN_LEFT_MID, 28, 0);

    // Time snapshot
    {
        uint32_t epoch = slopos::mesh::getCurrentTime();
        char t[8];
        if (epoch == 0) snprintf(t, sizeof(t), "--:--");
        else {
            uint32_t sec = epoch % 86400;
            snprintf(t, sizeof(t), "%02d:%02d", (sec/3600)%24, (sec/60)%60);
        }
        lv_obj_t* tl = lv_label_create(top);
        lv_label_set_text(tl, t);
        lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_12, 0);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Top divider
    lv_obj_t* tdiv = lv_obj_create(s);
    lv_obj_set_size(tdiv, LV_PCT(100), LIST_DIV_H);
    lv_obj_align(tdiv, LV_ALIGN_TOP_MID, 0, LIST_BAR_H);
    lv_obj_set_style_bg_color(tdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(tdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tdiv, 0, 0);

    // Bottom bar
    lv_obj_t* bot = lv_obj_create(s);
    lv_obj_set_size(bot, LV_PCT(100), BOT_BAR_H);
    lv_obj_align(bot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(bot, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bot, 0, 0);
    lv_obj_set_style_border_width(bot, 0, 0);

    lv_obj_t* dev = lv_label_create(bot);
    lv_label_set_text(dev, slopos::mesh::getOwnName());
    lv_obj_set_style_text_color(dev, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dev, &lv_font_montserrat_10, 0);
    lv_obj_align(dev, LV_ALIGN_LEFT_MID, 4, 0);

    {
        int rssi = slopos::mesh::getLastRSSI();
        const char* bars = rssi > -70  ? "▂▄▆█" :
                           rssi > -85  ? "▂▄▆ " :
                           rssi > -100 ? "▂▄  " :
                           rssi > -115 ? "▂   " : "    ";
        lv_obj_t* sig = lv_label_create(bot);
        lv_label_set_text(sig, bars);
        lv_obj_set_style_text_color(sig, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(sig, &lv_font_montserrat_10, 0);
        lv_obj_align(sig, LV_ALIGN_CENTER, -20, 0);
    }

    {
        char batt[8];
        int pct = slopos_battery_pct();
        snprintf(batt, sizeof(batt), "%d%%", pct);
        lv_obj_t* bl = lv_label_create(bot);
        lv_label_set_text(bl, batt);
        lv_obj_set_style_text_color(bl, lv_color_hex(pct > 20 ? ACCENT : ACCENT_RED), 0);
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_10, 0);
        lv_obj_align(bl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Bottom divider
    lv_obj_t* bdiv = lv_obj_create(s);
    lv_obj_set_size(bdiv, LV_PCT(100), LIST_DIV_H);
    lv_obj_align(bdiv, LV_ALIGN_TOP_MID, 0, DISPLAY_H - BOT_BAR_H - LIST_DIV_H);
    lv_obj_set_style_bg_color(bdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(bdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bdiv, 0, 0);

    return s;
}

// ════════════════════════════════════════════════════
// Channel-list view
// ════════════════════════════════════════════════════
static void show_channel_list(lv_scr_load_anim_t anim)
{
    // Null messaging-view pointers — they're invalid once we leave
    scr = top_bar = channel_ribbon = msg_list = input_bar = input_field = nullptr;

    refresh_channels();

    lv_obj_t* s = make_chat_list_screen();

    lv_obj_t* list = lv_obj_create(s);
    lv_obj_set_size(list, LV_PCT(100), LIST_CONT_H - 32);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, LIST_CONT_Y);
    lv_obj_set_user_data(list, (void*)0xCA7C);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    populate_channel_rows(list);

    lv_obj_t* add_btn = lv_btn_create(s);
    lv_obj_set_size(add_btn, CONTENT_W > 200 ? 180 : CONTENT_W - 20, 28);
    lv_obj_align(add_btn, LV_ALIGN_TOP_MID, 0, LIST_CONT_Y + LIST_CONT_H - 32);
    lv_obj_set_style_bg_color(add_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(add_btn, 0, 0);
    lv_obj_t* al = lv_label_create(add_btn);
    lv_label_set_text(al, LV_SYMBOL_PLUS " Add # Channel");
    lv_obj_set_style_text_font(al, &lv_font_montserrat_10, 0);
    lv_obj_center(al);
    lv_obj_add_event_cb(add_btn, [](lv_event_t* e) {
        lv_obj_t* scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        show_add_channel_options(scr);
    }, LV_EVENT_CLICKED, nullptr);

    lv_scr_load_anim(s, anim, 200, 0, true);
}

// ════════════════════════════════════════════════════
// Messaging view — channel ribbon rebuild
// ════════════════════════════════════════════════════
static void rebuild_channel_ribbon()
{
    if (!channel_ribbon) return;
    lv_obj_set_style_bg_color(channel_ribbon, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(channel_ribbon, LV_OPA_COVER, 0);
    lv_obj_clean(channel_ribbon);

    for (int i = 0; i < dyn_count; i++) {
        create_channel_pill(channel_ribbon, i);
    }

    lv_obj_invalidate(channel_ribbon);
    if (top_bar) lv_obj_invalidate(top_bar);
}

// ════════════════════════════════════════════════════
// Messaging view — top bar (← back + channel pills)
// ════════════════════════════════════════════════════
static void create_top_bar()
{
    top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, LV_PCT(100), TOP_H);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    disable_scroll(top_bar);

    // ← back button → return to channel list
    lv_obj_t* back = lv_btn_create(top_bar);
    lv_obj_set_size(back, 24, TOP_H - 4);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(back, 0, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_t* bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(bl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
    lv_obj_center(bl);
    disable_scroll(bl);
    lv_obj_add_event_cb(back, [](lv_event_t*) {
        show_channel_list(LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    }, LV_EVENT_CLICKED, nullptr);

    // Horizontal scrollable channel ribbon — exact width for no warp (matches home grid uniform sizing)
    int ribbon_w = CONTENT_W - 28 - 44; // back button + margins + time space
    channel_ribbon = lv_obj_create(top_bar);
    lv_obj_set_size(channel_ribbon, ribbon_w, TOP_H - 4);
    lv_obj_align(channel_ribbon, LV_ALIGN_LEFT_MID, 28, 0);
    lv_obj_set_style_bg_color(channel_ribbon, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(channel_ribbon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(channel_ribbon, 0, 0);
    lv_obj_set_style_pad_all(channel_ribbon, 0, 0);
    lv_obj_set_flex_flow(channel_ribbon, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(channel_ribbon, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(channel_ribbon, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(channel_ribbon, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(channel_ribbon, (lv_obj_flag_t)(
        LV_OBJ_FLAG_SCROLL_ELASTIC |
        LV_OBJ_FLAG_SCROLL_MOMENTUM |
        LV_OBJ_FLAG_SCROLL_CHAIN |
        LV_OBJ_FLAG_SCROLL_ON_FOCUS |
        LV_OBJ_FLAG_SCROLL_WITH_ARROW));

    // 24h time (right side)
    {
        uint32_t epoch = slopos::mesh::getCurrentTime();
        char t[8];
        if (epoch == 0) snprintf(t, sizeof(t), "--:--");
        else {
            uint32_t sec = epoch % 86400;
            snprintf(t, sizeof(t), "%02d:%02d", (sec/3600)%24, (sec/60)%60);
        }
        lv_obj_t* tl = lv_label_create(top_bar);
        lv_label_set_text(tl, t);
        lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_12, 0);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    for (int i = 0; i < dyn_count; i++) {
        create_channel_pill(channel_ribbon, i);
    }

    // Divider
    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, TOP_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
}

// ════════════════════════════════════════════════════
// Message bubble — Discord style
// ════════════════════════════════════════════════════
static lv_obj_t* create_bubble(lv_obj_t* parent, const char* sender,
                                const char* text, uint32_t timestamp,
                                bool is_self)
{
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, BUBBLE_PAD / 2, 0);
    disable_scroll(container);

    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    if (is_self) {
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }

    lv_obj_t* bubble = lv_obj_create(container);
    lv_obj_set_width(bubble, LV_PCT(78));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, 0, 0);
    lv_obj_set_style_pad_all(bubble, 6, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    disable_scroll(bubble);

    if (is_self) {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(MSG_INCOMING), 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    }

    // Sender name + timestamp row
    lv_obj_t* header = lv_obj_create(bubble);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    disable_scroll(header);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* name = lv_label_create(header);
    lv_label_set_text(name, sender);
    lv_obj_set_style_text_color(name,
        is_self ? lv_color_hex(0xffffff) : lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_10, 0);

    char time_buf[8];
    format_time(time_buf, sizeof(time_buf), timestamp);
    lv_obj_t* ts = lv_label_create(header);
    lv_label_set_text(ts, time_buf);
    lv_obj_set_style_text_color(ts,
        is_self ? lv_color_hex(0xb0d4ff) : lv_color_hex(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(ts, &lv_font_montserrat_10, 0);

    lv_obj_t* msg_text = lv_label_create(bubble);
    lv_label_set_text(msg_text, text);
    lv_obj_set_style_text_color(msg_text,
        is_self ? lv_color_hex(0xffffff) : lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(msg_text, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_text, LV_PCT(100));

    return container;
}

// ════════════════════════════════════════════════════
// Message list (vertical scroll)
// ════════════════════════════════════════════════════
static void create_message_list()
{
    msg_list = lv_obj_create(scr);
    lv_obj_set_size(msg_list, LV_PCT(100), MSG_LIST_H);
    lv_obj_align(msg_list, LV_ALIGN_TOP_MID, 0, MSG_LIST_Y);
    lv_obj_set_style_bg_color(msg_list, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_style_bg_opa(msg_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(msg_list, 0, 0);
    lv_obj_set_style_pad_all(msg_list, 2, 0);
    lv_obj_set_flex_flow(msg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(msg_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(msg_list, LV_SCROLLBAR_MODE_OFF);
}

static void render_active_messages()
{
    if (!msg_list || active_channel < 0 || active_channel >= MAX_CHANNELS) return;

    lv_obj_clean(msg_list);
    for (uint8_t i = 0; i < ch_msg_count[active_channel]; i++) {
        ChannelMessage& msg = ch_msgs[active_channel][i];
        create_bubble(msg_list, msg.sender, msg.text, msg.timestamp, msg.is_self);
    }

    uint32_t count = lv_obj_get_child_cnt(msg_list);
    if (count > 0) {
        lv_obj_t* last = lv_obj_get_child(msg_list, count - 1);
        if (last) lv_obj_scroll_to_view(last, LV_ANIM_OFF);
    }
}

// ════════════════════════════════════════════════════
// Send helper
// ════════════════════════════════════════════════════
static void do_send()
{
    const char* text = lv_textarea_get_text(input_field);
    if (!text || !text[0]) return;

    const char* chan = dyn_channels[active_channel];
    bool is_dm = (strncmp(chan, "DM: ", 4) == 0);
    const char* dest = is_dm ? (chan + 4) : chan;

    if (is_dm) slopos::mesh::sendMessage(dest, text);
    else       slopos::mesh::sendChannelMessage(dest, text);

    uint32_t now = slopos::mesh::getCurrentTime();
    int sent_channel = active_channel;
    append_channel_message(sent_channel, slopos::mesh::getOwnName(), text, now, true);
    mark_channel_used(sent_channel);
    render_active_messages();
    lv_textarea_set_text(input_field, "");

    lv_obj_t* last = lv_obj_get_child(msg_list, lv_obj_get_child_cnt(msg_list) - 1);
    if (last) lv_obj_scroll_to_view(last, LV_ANIM_ON);
}

// ════════════════════════════════════════════════════
// Input bar
// ════════════════════════════════════════════════════
static void create_input_bar()
{
    int input_y = DISPLAY_H - BOT_BAR_H - INPUT_H - DIVIDER_H;

    input_bar = lv_obj_create(scr);
    lv_obj_set_size(input_bar, LV_PCT(100), INPUT_H);
    lv_obj_align(input_bar, LV_ALIGN_TOP_MID, 0, input_y + DIVIDER_H);
    lv_obj_set_style_bg_color(input_bar, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(input_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(input_bar, 4, 0);
    lv_obj_set_style_border_width(input_bar, 0, 0);
    disable_scroll(input_bar);

    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, input_y);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    input_field = lv_textarea_create(input_bar);
    int field_w = CONTENT_W - 60; // match ribbon usable + send button room
    lv_obj_set_size(input_field, field_w, INPUT_H - 8);
    lv_obj_align(input_field, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(input_field, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(input_field, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(input_field, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(input_field, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_width(input_field, 1, 0);
    lv_obj_set_style_border_color(input_field, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(input_field, 0, 0);
    lv_obj_set_style_pad_all(input_field, 4, 0);
    lv_textarea_set_one_line(input_field, true);
    lv_textarea_set_placeholder_text(input_field, "Message #channel");
    lv_obj_remove_flag(input_field, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_outline_width(input_field, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(input_field, 0, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_EDITED));

    lv_obj_t* send_btn = lv_btn_create(input_bar);
    lv_obj_set_size(send_btn, 52, INPUT_H - 8);
    lv_obj_align(send_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_opa(send_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(send_btn, 0, 0);
    lv_obj_set_style_border_width(send_btn, 0, 0);

    lv_obj_t* send_label = lv_label_create(send_btn);
    lv_label_set_text(send_label, "Send");
    lv_obj_set_style_text_font(send_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(send_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(send_label);

    lv_obj_add_event_cb(send_btn, [](lv_event_t*) { do_send(); },
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(input_field, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_READY) do_send();
    }, LV_EVENT_ALL, nullptr);
}

// ════════════════════════════════════════════════════
// Bottom status bar
// ════════════════════════════════════════════════════
static void create_bottom_bar()
{
    lv_obj_t* bot = lv_obj_create(scr);
    lv_obj_set_size(bot, LV_PCT(100), BOT_BAR_H);
    lv_obj_align(bot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(bot, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bot, 0, 0);
    lv_obj_set_style_border_width(bot, 0, 0);

    lv_obj_t* dev = lv_label_create(bot);
    lv_label_set_text(dev, slopos::mesh::getOwnName());
    lv_obj_set_style_text_color(dev, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dev, &lv_font_montserrat_10, 0);
    lv_obj_align(dev, LV_ALIGN_LEFT_MID, 4, 0);

    int rssi = slopos::mesh::getLastRSSI();
    const char* bars = rssi > -70  ? "▂▄▆█" :
                       rssi > -85  ? "▂▄▆ " :
                       rssi > -100 ? "▂▄  " :
                       rssi > -115 ? "▂   " : "    ";
    lv_obj_t* sig = lv_label_create(bot);
    lv_label_set_text(sig, bars);
    lv_obj_set_style_text_color(sig, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(sig, &lv_font_montserrat_10, 0);
    lv_obj_align(sig, LV_ALIGN_CENTER, -20, 0);

    char batt_buf[8];
    int pct = slopos_battery_pct();
    snprintf(batt_buf, sizeof(batt_buf), "%d%%", pct);
    lv_obj_t* bl = lv_label_create(bot);
    lv_label_set_text(bl, batt_buf);
    lv_obj_set_style_text_color(bl, lv_color_hex(pct > 20 ? ACCENT : ACCENT_RED), 0);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_10, 0);
    lv_obj_align(bl, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, DISPLAY_H - BOT_BAR_H - DIVIDER_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
}

// ════════════════════════════════════════════════════
// Messaging view (opened by tapping a channel)
// ════════════════════════════════════════════════════
static void open_channel_messaging(int idx)
{
    active_channel = idx;

    scr = lv_obj_create(nullptr);
    apply_dark_bg(scr);
    disable_scroll(scr);

    // When the messaging screen is auto-deleted (e.g. user navigates
    // away without pressing back), null all global widget pointers
    // so chat_screen_add_msg() doesn't dereference freed memory.
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        scr = top_bar = channel_ribbon = msg_list = input_bar = input_field = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    create_top_bar();
    create_message_list();
    render_active_messages();
    create_input_bar();
    create_bottom_bar();

    lv_group_t* g = lv_group_get_default();
    if (g && input_field) {
        lv_group_add_obj(g, input_field);
        lv_group_focus_obj(input_field);
    }

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
}

static void refresh_chat_list_view(lv_obj_t* scr) {
    refresh_channels();
    uint32_t n = lv_obj_get_child_cnt(scr);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* c = lv_obj_get_child(scr, i);
        if (lv_obj_get_user_data(c) == (void*)0xCA7C) {
            lv_obj_clean(c);
            populate_channel_rows(c);
            return;
        }
    }
}

static void show_add_channel_options(lv_obj_t* parent) {
    auto dlg_sz = dialog_size(260, 140);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Add # Channel");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* nl = lv_label_create(dlg);
    lv_label_set_text(nl, "Hashtag:");
    lv_obj_set_style_text_color(nl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_align(nl, LV_ALIGN_TOP_LEFT, 4, 28);

    lv_obj_t* ni = lv_textarea_create(dlg);
    lv_obj_set_size(ni, dlg_sz.w - 16, 28);
    lv_obj_align(ni, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_color(ni, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(ni, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(ni, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_width(ni, 0, 0);
    lv_textarea_set_one_line(ni, true);
    lv_textarea_set_placeholder_text(ni, "e.g. #general");

    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, ni);
        lv_group_focus_obj(ni);
    }

    lv_obj_t* fb = lv_label_create(dlg);
    lv_obj_set_style_text_color(fb, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(fb, &lv_font_montserrat_10, 0);
    lv_obj_align(fb, LV_ALIGN_BOTTOM_MID, 0, -32);

    lv_obj_t* add = lv_btn_create(dlg);
    lv_obj_set_size(add, 100, 28);
    lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(add, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(add, 0, 0);
    lv_obj_t* al = lv_label_create(add);
    lv_label_set_text(al, "Add");
    lv_obj_center(al);

    auto submit = [](lv_event_t* e) {
        lv_obj_t* d = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
        lv_obj_t* sc = lv_obj_get_screen(d);
        lv_obj_t* feedback = (lv_obj_t*)lv_event_get_user_data(e);

        lv_obj_t* namei = nullptr;
        for (uint32_t j = 0; j < lv_obj_get_child_cnt(d); j++) {
            lv_obj_t* child = lv_obj_get_child(d, j);
            if (lv_obj_check_type(child, &lv_textarea_class)) {
                namei = child;
                break;
            }
        }

        const char* nm = namei ? lv_textarea_get_text(namei) : "";
        if (!nm[0]) { if (feedback) lv_label_set_text(feedback, "Enter hashtag"); return; }
        if (slopos::mesh::addHashtagChannel(nm)) {
            lv_obj_del_async(d);
            refresh_chat_list_view(sc);
        } else {
            if (feedback) lv_label_set_text(feedback, "Invalid or full");
        }
    };

    lv_obj_add_event_cb(add, submit, LV_EVENT_CLICKED, (void*)fb);
    lv_obj_add_event_cb(ni, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_READY) return;
        lv_obj_t* input = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* d = lv_obj_get_parent(input);
        lv_obj_t* sc = lv_obj_get_screen(d);
        lv_obj_t* feedback = (lv_obj_t*)lv_event_get_user_data(e);
        const char* nm = lv_textarea_get_text(input);
        if (!nm || !nm[0]) { if (feedback) lv_label_set_text(feedback, "Enter hashtag"); return; }
        if (slopos::mesh::addHashtagChannel(nm)) {
            lv_obj_del_async(d);
            refresh_chat_list_view(sc);
        } else {
            if (feedback) lv_label_set_text(feedback, "Invalid or full");
        }
    }, LV_EVENT_ALL, (void*)fb);
}

void chat_screen_open_dm(const char* contact_name)
{
    refresh_channels();

    char dm_name[32];
    snprintf(dm_name, sizeof(dm_name), "DM: %s", contact_name);

    // Check if DM channel already exists
    int idx = -1;
    for (int i = 0; i < dyn_count; i++) {
        if (strcmp(dyn_channels[i], dm_name) == 0) { idx = i; break; }
    }

    // Create DM channel if not found
    if (idx < 0) {
        if (dyn_count >= MAX_CHANNELS) {
            show_channel_list(LV_SCR_LOAD_ANIM_MOVE_LEFT);
            return;
        }
        idx = dyn_count++;
        strncpy(dyn_channels[idx], dm_name, 31);
        dyn_channels[idx][31] = '\0';
        ch_msg_count[idx] = 0;
        ch_meta[idx].preview[0] = '\0';
        ch_meta[idx].timestamp = 0;
        ch_meta[idx].unread = 0;
    }

    open_channel_messaging(idx);
}

void chat_screen_show()
{
    show_channel_list(LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

void chat_screen_add_msg(const char* channel, const char* sender, const char* text, bool is_self)
{
    uint32_t now = slopos::mesh::getCurrentTime();
    int idx = find_channel_idx(channel);
    if (idx < 0 || idx >= MAX_CHANNELS) return;

    append_channel_message(idx, sender, text, now, is_self);

    bool visible = msg_list && idx == active_channel;
    if (!is_self && !visible) ch_meta[idx].unread++;
    if (!visible) return;

    create_bubble(msg_list, sender, text, now, is_self);
    if (lv_obj_get_child_cnt(msg_list) > MAX_MSGS)
        lv_obj_del(lv_obj_get_child(msg_list, 0));

    lv_obj_t* last = lv_obj_get_child(msg_list, lv_obj_get_child_cnt(msg_list) - 1);
    if (last) lv_obj_scroll_to_view(last, LV_ANIM_ON);
}

} // namespace slopos::ui
