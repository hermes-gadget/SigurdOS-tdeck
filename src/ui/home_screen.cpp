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


#include "home_screen.h"
#include "screens.h"
#include "screens_common.h"
#include "screen_lifetime.h"
#include "chat_screen.h"
#include "navigation.h"
#include "theme.h"
#include "responsive.h"
#include "../hal/tdeck_pins.h"
#include "../hal/prefs.h"
#include "../hal/battery.h"
#include "../mesh/mesh_wrapper.h"
#include "../diagnostics/debug_cfg.h"
#if SIGURDOS_DEBUG_UI
#include "../diagnostics/debug.h"
#endif
#include "../fonts/emoji_font.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>
#include <cstring>

namespace sigurdos::ui {

using namespace theme;

static lv_obj_t* scr           = nullptr;
static lv_obj_t* top_bar       = nullptr;
static lv_obj_t* bottom_bar    = nullptr;
static lv_obj_t* grid          = nullptr;
static lv_obj_t* time_label    = nullptr;
static lv_obj_t* batt_label    = nullptr;
static lv_obj_t* hashtag_label = nullptr;
static lv_obj_t* badge_obj     = nullptr;  // CHATS unread badge container

using namespace responsive;
static constexpr int GRID_PAD   = 3;
// GRID_COLS, GRID_ROWS, TOP_BAR_H, BOT_BAR_H, DIVIDER_H — now from responsive.h

struct IconDef {
    const char* label;
    const char* symbol;
    bool        badge;
    Screen      target;
};

static const IconDef icons[] = {
    {"CHATS",     LV_SYMBOL_ENVELOPE,   true,  Screen::Chat},
    {"DMs",       LV_SYMBOL_FILE,       false, Screen::Chat},
    {"ROOMS",     LV_SYMBOL_DIRECTORY,  false, Screen::Contacts},
    {"CONTACTS",  LV_SYMBOL_CALL,       false, Screen::Contacts},
    {"REPEATERS", LV_SYMBOL_WIFI,       false, Screen::Repeaters},
    {"ADVERTISE", LV_SYMBOL_BELL,       false, Screen::Advertise},
    {"MAP",       LV_SYMBOL_GPS,        false, Screen::Map},
    {"TERMINAL",  LV_SYMBOL_KEYBOARD,   false, Screen::Terminal},
    {"PACKETS",   LV_SYMBOL_LIST,       false, Screen::Heard},
    {"SETTINGS",  LV_SYMBOL_SETTINGS,   false, Screen::Settings},
    {"SETUP",     LV_SYMBOL_HOME,       false, Screen::Onboarding},
    {"SIGNAL",    LV_SYMBOL_BARS,       false, Screen::Signal},
};

static constexpr int ICON_COUNT = sizeof(icons) / sizeof(icons[0]);
static lv_obj_t* icon_tiles[ICON_COUNT] = {};
static ScreenLifetime g_home_lifetime;
static int tile_x[ICON_COUNT] = {};
static int tile_y[ICON_COUNT] = {};
static int tile_w[ICON_COUNT] = {};
static int tile_h[ICON_COUNT] = {};
static int selected_icon = 0;
static int active_cols = 1;
static int active_rows = 1;

static void format_time_str(char* out, size_t sz) {
    uint32_t epoch = sigurdos::mesh::getCurrentTime();
    if (epoch == 0) {
        snprintf(out, sz, "--:--");
    } else {
        uint32_t sec = epoch % 86400;
        snprintf(out, sz, "%02d:%02d", (sec/3600)%24, (sec/60)%60);
    }
}

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

static void force_full_tile_redraw(int idx)
{
    if (idx < 0 || idx >= ICON_COUNT || !icon_tiles[idx]) return;

    // Always safe per-object invalidate
    lv_obj_invalidate(icon_tiles[idx]);

    // Expanded dirty rect (in tile-local coords) so the partial renderer
    // is forced to redraw the entire icon + label + border in one go.
    lv_area_t a;
    a.x1 = -4;
    a.y1 = -4;
    a.x2 = tile_w[idx] + 4;
    a.y2 = tile_h[idx] + 4;
    lv_obj_invalidate_area(icon_tiles[idx], &a);
}

static void apply_selection(int old_idx = -1)
{
    if (old_idx >= 0 && old_idx < ICON_COUNT && icon_tiles[old_idx]) {
        lv_obj_set_style_border_color(icon_tiles[old_idx], lv_color_hex(BG_PRIMARY), 0);
        force_full_tile_redraw(old_idx);
    }
    if (selected_icon >= 0 && selected_icon < ICON_COUNT && icon_tiles[selected_icon]) {
        lv_obj_set_style_border_color(icon_tiles[selected_icon], lv_color_hex(ACCENT), 0);
        force_full_tile_redraw(selected_icon);
    }

#if SIGURDOS_DEBUG_UI
    SIGURDOS_RUNTIME_FEAT(ui) {
    Serial.printf("[home] apply_selection old=%d new=%d", old_idx, selected_icon);
    if (selected_icon >= 0 && selected_icon < ICON_COUNT) {
        lv_area_t coords;
        lv_obj_get_coords(icon_tiles[selected_icon], &coords);
        Serial.printf(" tile_coords=(%ld,%ld,%ld,%ld) w=%ld h=%ld",
                      (long)coords.x1, (long)coords.y1, (long)coords.x2, (long)coords.y2,
                      (long)lv_obj_get_width(icon_tiles[selected_icon]),
                      (long)lv_obj_get_height(icon_tiles[selected_icon]));
        lv_coord_t bw = lv_obj_get_style_border_width(icon_tiles[selected_icon], 0);
        lv_opa_t bg_o = lv_obj_get_style_bg_opa(icon_tiles[selected_icon], 0);
        lv_coord_t pl = lv_obj_get_style_pad_left(icon_tiles[selected_icon], 0);
        lv_coord_t pr = lv_obj_get_style_pad_right(icon_tiles[selected_icon], 0);
        lv_coord_t pt = lv_obj_get_style_pad_top(icon_tiles[selected_icon], 0);
        lv_coord_t pb = lv_obj_get_style_pad_bottom(icon_tiles[selected_icon], 0);
        Serial.printf(" bw=%d bg_opa=%d pad=(%d,%d,%d,%d)",
                      (int)bw, (int)bg_o, (int)pl, (int)pr, (int)pt, (int)pb);

        lv_obj_t* parent = lv_obj_get_parent(icon_tiles[selected_icon]);
        if (parent) {
            lv_area_t gc;
            lv_obj_get_coords(parent, &gc);
            Serial.printf(" grid_coords=(%ld,%ld,%ld,%ld) grid_w=%ld grid_h=%ld",
                          (long)gc.x1, (long)gc.y1, (long)gc.x2, (long)gc.y2,
                          (long)lv_obj_get_width(parent), (long)lv_obj_get_height(parent));
        }
    }
    Serial.println();
    }
#endif
}

static void on_icon_click(lv_event_t* e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < ICON_COUNT) {
        // Reset filters to defaults
        chat_screen_set_filter(0);
        contacts_screen_set_filter(-1);

        // Apply filter based on which icon was clicked
        if (strcmp(icons[idx].label, "DMs") == 0) {
            chat_screen_set_filter(2);       // DMs only
        } else if (strcmp(icons[idx].label, "CHATS") == 0) {
            chat_screen_set_filter(1);       // channels only
        } else if (strcmp(icons[idx].label, "ROOMS") == 0) {
            contacts_screen_set_filter(ADV_TYPE_ROOM);  // room servers only
        }
        // CONTACTS: default filter (CHAT + ROOM) — start DM from here
        navigate_to(icons[idx].target);
    }
}

// ── Top bar ─────────────────────────────────────────────
static void create_top_bar()
{
    top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, LV_PCT(100), TOP_BAR_H);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);

    // ≡ hamburger using LVGL symbol font (reliable on all builds)
    lv_obj_t* menu_icon = lv_label_create(top_bar);
    lv_label_set_text(menu_icon, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(menu_icon, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_align(menu_icon, LV_ALIGN_LEFT_MID, 4, 0);

    // Radio status / setup warning (replaces old channel hashtags)
    const bool configured = sigurdos::prefs_get().configured;
    hashtag_label = lv_label_create(top_bar);
    lv_label_set_text(hashtag_label, configured ? "" : "Do setup for radio");
    lv_label_set_long_mode(hashtag_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hashtag_label, HASHTAG_LABEL_W());
    lv_obj_set_style_text_color(hashtag_label,
        lv_color_hex(configured ? CHANNEL_HASH : ACCENT_RED), 0);
    lv_obj_set_style_text_font(hashtag_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(hashtag_label, LV_ALIGN_LEFT_MID, 26, 0);

    // Time (far right)
    time_label = lv_label_create(top_bar);
    char tbuf[8];
    format_time_str(tbuf, sizeof(tbuf));
    lv_label_set_text(time_label, tbuf);
    lv_obj_set_style_text_color(time_label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(time_label, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(time_label, LV_ALIGN_RIGHT_MID, -4, 0);

    // Signal dots (iOS-style, left of time)
    {
        lv_obj_t* sig = create_signal_dots(top_bar, sigurdos::mesh::getLastRSSI());
        lv_obj_align(sig, LV_ALIGN_RIGHT_MID, -54, 0);
    }

    // Divider
    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
}

// ── Bottom bar ──────────────────────────────────────────
static void create_bottom_bar()
{
    bottom_bar = lv_obj_create(scr);
    lv_obj_set_size(bottom_bar, LV_PCT(100), BOT_BAR_H);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bottom_bar, 0, 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);

    lv_obj_t* dev = lv_label_create(bottom_bar);
    lv_label_set_text(dev, sigurdos::mesh::getOwnName());
    lv_obj_set_style_text_color(dev, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dev, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(dev, LV_ALIGN_LEFT_MID, 4, 0);

    batt_label = lv_label_create(bottom_bar);
    lv_label_set_text(batt_label, "--%");
    lv_obj_set_style_text_color(batt_label, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(batt_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(batt_label, LV_ALIGN_RIGHT_MID, -4, 0);

    // Divider
    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, DISPLAY_H - BOT_BAR_H - DIVIDER_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
}

// ── Icon tile ────────────────────────────────────────────
static lv_obj_t* create_icon_tile(lv_obj_t* parent, const IconDef& icon, int idx)
{
    lv_obj_t* tile = lv_obj_create(parent);
    lv_obj_set_size(tile, tile_w[idx], tile_h[idx]);
    lv_obj_set_pos(tile, tile_x[idx], tile_y[idx]);
    lv_obj_set_style_bg_color(tile, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, 0, 0);
    lv_obj_set_style_border_width(tile, PIXEL_BORDER, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_style_pad_all(tile, 4, 0);
    disable_scroll(tile);

    lv_obj_set_style_bg_color(tile, lv_color_hex(0x2a2a2a), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_STATE_PRESSED);

    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, on_icon_click, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    lv_obj_t* icon_label = lv_label_create(tile);
    lv_label_set_text(icon_label, icon.symbol);
    lv_obj_set_style_text_font(icon_label, emoji_wrapped_montserrat_14, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(ACCENT), 0);
    lv_obj_align(icon_label, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t* label = lv_label_create(tile);
    lv_label_set_text(label, icon.label);
    lv_obj_set_style_text_color(label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 12);

    if (icon.badge) {
        // Badge: container with count label, hidden by default, shown when
        // sigurdos::mesh::pendingMessageCount() > 0 via home_screen_update_badges()
        badge_obj = lv_obj_create(tile);
        lv_obj_set_size(badge_obj, 18, 12);
        lv_obj_set_style_bg_color(badge_obj, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_bg_opa(badge_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(badge_obj, 0, 0);
        lv_obj_set_style_border_width(badge_obj, 0, 0);
        lv_obj_align(badge_obj, LV_ALIGN_TOP_RIGHT, -2, 4);
        lv_obj_clear_flag(badge_obj, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_add_flag(badge_obj, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* cnt_lbl = lv_label_create(badge_obj);
        lv_label_set_text(cnt_lbl, "0");
        lv_obj_set_style_text_color(cnt_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(cnt_lbl, emoji_wrapped_montserrat_10, 0);
        lv_obj_center(cnt_lbl);
    }

    return tile;
}

// ── Adaptive icon grid — LVGL grid layout fills the full content area ──────
static void create_icon_grid()
{
    GridLayout gl = compute_grid(GRID_PAD);
    active_cols = gl.cols;
    active_rows = (ICON_COUNT + active_cols - 1) / active_cols;

    const int usable_w = CONTENT_W - (GRID_PAD * 2) - (GRID_PAD * (active_cols - 1));
    const int usable_h = CONTENT_H - (GRID_PAD * 2) - (GRID_PAD * (active_rows - 1));
    const int base_w = usable_w / active_cols;
    const int base_h = usable_h / active_rows;
    const int extra_h = usable_h - (base_h * active_rows);

    for (int i = 0; i < ICON_COUNT; i++) {
        const int col = i % active_cols;
        const int row = i / active_cols;
        // Use the same width for every column so that when the selection border
        // is active the inner content area is identical on all tiles.
        // This eliminates the visual "warping" that only appeared on the left
        // column (the one that used to receive the extra pixel).
        tile_w[i] = base_w;
        tile_h[i] = base_h + (row < extra_h ? 1 : 0);
        tile_x[i] = GRID_PAD + col * (base_w + GRID_PAD);
        tile_y[i] = GRID_PAD + row * (base_h + GRID_PAD) + (row < extra_h ? 1 : 0);
    }

    grid = lv_obj_create(scr);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_size(grid, LV_PCT(100), CONTENT_H);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    disable_scroll(grid);

    for (int i = 0; i < ICON_COUNT; i++) {
        icon_tiles[i] = create_icon_tile(grid, icons[i], i);
    }

    selected_icon = 0;
    apply_selection();
}

// ── Shared build helper ──────────────────────────────────
static void build_home_screen(lv_scr_load_anim_t anim, uint32_t duration)
{
    // Reset dangling pointers before rebuilding
    hashtag_label = nullptr;
    badge_obj     = nullptr;
    screens_clear_back_btn();
    screens_clear_wifi_icon();
    time_label    = nullptr;
    batt_label    = nullptr;
    for (int i = 0; i < ICON_COUNT; i++) icon_tiles[i] = nullptr;

    scr = lv_obj_create(nullptr);
    apply_dark_bg(scr);
    disable_scroll(scr);

    // When any other screen replaces Home, LVGL frees all children. Track
    // only Home-owned pointers; shared screen pointers may already belong to
    // the replacement screen when this delayed delete callback runs.
    g_home_lifetime.bind(scr);
    g_home_lifetime.track(&scr);
    g_home_lifetime.track(&top_bar);
    g_home_lifetime.track(&bottom_bar);
    g_home_lifetime.track(&grid);
    g_home_lifetime.track(&time_label);
    g_home_lifetime.track(&batt_label);
    g_home_lifetime.track(&hashtag_label);
    g_home_lifetime.track(&badge_obj);
    for (int i = 0; i < ICON_COUNT; i++) g_home_lifetime.track(&icon_tiles[i]);

    create_top_bar();
    create_bottom_bar();
    create_icon_grid();
    // Always use show_screen (auto_del=false). The animated FADE_ON path used
    // auto_del=true and reintroduced the LVGL screen-load deadlock after ~4-5
    // navigation cycles (screens.cpp / PR #840). Drop animation rather than
    // mix auto_del policies.
    (void)anim;
    (void)duration;
    show_screen(scr);
}

// ── Public API ───────────────────────────────────────────
void home_screen_create()
{
    build_home_screen(LV_SCR_LOAD_ANIM_NONE, 0);
    home_screen_update_battery(sigurdos_battery_pct());
    home_screen_update_badges();
}

void home_screen_show()
{
    build_home_screen(LV_SCR_LOAD_ANIM_NONE, 0);
    home_screen_update_battery(sigurdos_battery_pct());
    home_screen_update_badges();
}

void home_screen_handle_trackball(SigurdOSTrackballEvent event)
{
    if (!grid) return;

    switch (event) {
    case SigurdOSTrackballEvent::Left: {
        const int old = selected_icon;
        const int row = selected_icon / active_cols;
        const int first = row * active_cols;
        const int last = (first + active_cols - 1 < ICON_COUNT) ? first + active_cols - 1 : ICON_COUNT - 1;
        selected_icon = (selected_icon == first) ? last : selected_icon - 1;
        apply_selection(old);
        break;
    }
    case SigurdOSTrackballEvent::Right: {
        const int old = selected_icon;
        const int row = selected_icon / active_cols;
        const int first = row * active_cols;
        const int last = (first + active_cols - 1 < ICON_COUNT) ? first + active_cols - 1 : ICON_COUNT - 1;
        selected_icon = (selected_icon == last) ? first : selected_icon + 1;
        apply_selection(old);
        break;
    }
    case SigurdOSTrackballEvent::Up: {
        const int old = selected_icon;
        const int col = selected_icon % active_cols;
        int row = selected_icon / active_cols;
        do {
            row = (row == 0) ? active_rows - 1 : row - 1;
            selected_icon = row * active_cols + col;
        } while (selected_icon >= ICON_COUNT);
        apply_selection(old);
        break;
    }
    case SigurdOSTrackballEvent::Down: {
        const int old = selected_icon;
        const int col = selected_icon % active_cols;
        int row = selected_icon / active_cols;
        do {
            row = (row + 1) % active_rows;
            selected_icon = row * active_cols + col;
        } while (selected_icon >= ICON_COUNT);
        apply_selection(old);
        break;
    }
    case SigurdOSTrackballEvent::Click:
        if (selected_icon >= 0 && selected_icon < ICON_COUNT) {
            navigate_to(icons[selected_icon].target);
        }
        break;
    case SigurdOSTrackballEvent::None:
    default:
        break;
    }
}

void home_screen_update_battery(int pct)
{
    if (!batt_label) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(batt_label, buf);
    lv_obj_set_style_text_color(batt_label,
        pct > 20 ? lv_color_hex(ACCENT) : lv_color_hex(ACCENT_RED), 0);
}

void home_screen_update_time(const char* time_str)
{
    if (!time_label) return;
    lv_label_set_text(time_label, time_str);
}

void home_screen_update_channels()
{
    if (!hashtag_label) return;
    const bool configured = sigurdos::prefs_get().configured;
    lv_label_set_text(hashtag_label, configured ? "" : "Do setup for radio");
    lv_obj_set_style_text_color(hashtag_label,
        lv_color_hex(configured ? CHANNEL_HASH : ACCENT_RED), 0);
}

void home_screen_update_badges()
{
    if (!badge_obj) return;
    int n = sigurdos::mesh::getUnreadMessageCount();
    if (n > 0) {
        lv_obj_clear_flag(badge_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t* lbl = lv_obj_get_child(badge_obj, 0);
        if (lbl) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", n > 99 ? 99 : n);
            lv_label_set_text(lbl, buf);
        }
    } else {
        lv_obj_add_flag(badge_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace sigurdos::ui
