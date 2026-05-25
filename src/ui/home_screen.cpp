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


#include "home_screen.h"
#include "screens.h"
#include "navigation.h"
#include "theme.h"
#include "responsive.h"
#include "../hal/tdeck_pins.h"
#include "../mesh/mesh_wrapper.h"
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
#include "../diagnostics/debug.h"
#include <Arduino.h>
#endif
#include <lvgl.h>
#include <cstdio>
#include <cstring>

namespace slopos::ui {

using namespace theme;

static lv_obj_t* scr           = nullptr;
static lv_obj_t* top_bar       = nullptr;
static lv_obj_t* bottom_bar    = nullptr;
static lv_obj_t* grid          = nullptr;
static lv_obj_t* time_label    = nullptr;
static lv_obj_t* batt_label    = nullptr;
static lv_obj_t* signal_label  = nullptr;
static lv_obj_t* hashtag_label = nullptr;

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
    {"CONTACTS",  LV_SYMBOL_CALL,       false, Screen::Contacts},
    {"REPEATERS", LV_SYMBOL_WIFI,       false, Screen::Heard},
    {"FINDER",    LV_SYMBOL_EYE_OPEN,   false, Screen::Network},
    {"PACKETS",   LV_SYMBOL_LIST,       false, Screen::Heard},
    {"MAP",       LV_SYMBOL_GPS,        false, Screen::Map},
    {"ADVERTISE", LV_SYMBOL_AUDIO,      false, Screen::Advertise},
    {"SETTINGS",  LV_SYMBOL_SETTINGS,   false, Screen::Settings},
    {"TRACE",     LV_SYMBOL_SHUFFLE,    false, Screen::Trace},
    {"TERMINAL",  LV_SYMBOL_KEYBOARD,   false, Screen::Terminal},
    {"SETUP",     LV_SYMBOL_SETTINGS,   false, Screen::Onboarding},
    {"SIGNAL",    LV_SYMBOL_BARS,       false, Screen::Signal},
};

static constexpr int ICON_COUNT = sizeof(icons) / sizeof(icons[0]);
static lv_obj_t* icon_tiles[ICON_COUNT] = {};
static int tile_x[ICON_COUNT] = {};
static int tile_y[ICON_COUNT] = {};
static int tile_w[ICON_COUNT] = {};
static int tile_h[ICON_COUNT] = {};
static int selected_icon = 0;
static int active_cols = 1;
static int active_rows = 1;

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

#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
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
#endif
}

static void on_icon_click(lv_event_t* e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < ICON_COUNT)
        navigate_to(icons[idx].target);
}

// ── Build dynamic channel hashtag string ────────────────
static void build_channel_string(char* buf, size_t sz)
{
    char names[8][32];
    int n = slopos::mesh::exportChannels(names, 8);
    if (sz == 0) return;
    if (n == 0) {
        snprintf(buf, sz, "no channels");
        return;
    }

    int pos = 0;
    for (int i = 0; i < n && pos < (int)sz - 20; i++) {
        const char* nm = names[i];
        int written = snprintf(
            buf + pos,
            sz - pos,
            "%s%s*  ",
            (nm && nm[0] == '#') ? "" : "#",
            nm ? nm : "");

        if (written < 0) break;
        if ((size_t)written >= sz - (size_t)pos) {
            pos = (int)sz - 1;
            break;
        }
        pos += written;
    }
    while (pos > 0 && buf[pos-1] == ' ') buf[--pos] = '\0';
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

    // Dynamic channel hashtags
    char ch_buf[120];
    build_channel_string(ch_buf, sizeof(ch_buf));
    hashtag_label = lv_label_create(top_bar);
    lv_label_set_text(hashtag_label, ch_buf);
    lv_label_set_long_mode(hashtag_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hashtag_label, HASHTAG_LABEL_W());
    lv_obj_set_style_text_color(hashtag_label, lv_color_hex(CHANNEL_HASH), 0);
    lv_obj_set_style_text_font(hashtag_label, &lv_font_montserrat_10, 0);
    lv_obj_align(hashtag_label, LV_ALIGN_LEFT_MID, 26, 0);

    // Time (far right)
    time_label = lv_label_create(top_bar);
    lv_label_set_text(time_label, "--:--");
    lv_obj_set_style_text_color(time_label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_12, 0);
    lv_obj_align(time_label, LV_ALIGN_RIGHT_MID, -4, 0);

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
    lv_label_set_text(dev, slopos::mesh::getOwnName());
    lv_obj_set_style_text_color(dev, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dev, &lv_font_montserrat_10, 0);
    lv_obj_align(dev, LV_ALIGN_LEFT_MID, 4, 0);

    signal_label = create_signal_bars(bottom_bar, slopos::mesh::getLastRSSI());
    lv_obj_align(signal_label, LV_ALIGN_CENTER, -20, 0);

    batt_label = lv_label_create(bottom_bar);
    lv_label_set_text(batt_label, "--%");
    lv_obj_set_style_text_color(batt_label, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(batt_label, &lv_font_montserrat_10, 0);
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
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(ACCENT), 0);
    lv_obj_align(icon_label, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t* label = lv_label_create(tile);
    lv_label_set_text(label, icon.label);
    lv_obj_set_style_text_color(label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 12);

    if (icon.badge) {
        lv_obj_t* badge = lv_obj_create(tile);
        lv_obj_set_size(badge, 10, 10);
        lv_obj_set_style_bg_color(badge, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(badge, 0, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -4, 4);
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
    const int extra_w = usable_w - (base_w * active_cols);
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
    screens_clear_back_btn();
    time_label    = nullptr;
    batt_label    = nullptr;
    signal_label  = nullptr;
    for (int i = 0; i < ICON_COUNT; i++) icon_tiles[i] = nullptr;

    scr = lv_obj_create(nullptr);
    apply_dark_bg(scr);
    disable_scroll(scr);

    // When any other screen replaces Home (auto_del=true), LVGL frees all
    // children — null the static pointers so periodic update functions
    // in ui::loop() don't dereference freed objects.
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        scr = top_bar = bottom_bar = grid = nullptr;
        time_label = batt_label = signal_label = hashtag_label = nullptr;
        for (int i = 0; i < ICON_COUNT; i++) icon_tiles[i] = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    create_top_bar();
    create_bottom_bar();
    create_icon_grid();
    lv_scr_load_anim(scr, anim, duration, 0, true);
}

// ── Public API ───────────────────────────────────────────
void home_screen_create()
{
    build_home_screen(LV_SCR_LOAD_ANIM_FADE_ON, 300);
}

void home_screen_show()
{
    build_home_screen(LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200);
}

void home_screen_handle_trackball(SlopOSTrackballEvent event)
{
    if (!grid) return;

    switch (event) {
    case SlopOSTrackballEvent::Left: {
        const int old = selected_icon;
        const int row = selected_icon / active_cols;
        const int first = row * active_cols;
        const int last = (first + active_cols - 1 < ICON_COUNT) ? first + active_cols - 1 : ICON_COUNT - 1;
        selected_icon = (selected_icon == first) ? last : selected_icon - 1;
        apply_selection(old);
        break;
    }
    case SlopOSTrackballEvent::Right: {
        const int old = selected_icon;
        const int row = selected_icon / active_cols;
        const int first = row * active_cols;
        const int last = (first + active_cols - 1 < ICON_COUNT) ? first + active_cols - 1 : ICON_COUNT - 1;
        selected_icon = (selected_icon == last) ? first : selected_icon + 1;
        apply_selection(old);
        break;
    }
    case SlopOSTrackballEvent::Up: {
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
    case SlopOSTrackballEvent::Down: {
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
    case SlopOSTrackballEvent::Click:
        if (selected_icon >= 0 && selected_icon < ICON_COUNT) {
            navigate_to(icons[selected_icon].target);
        }
        break;
    case SlopOSTrackballEvent::None:
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

void home_screen_update_signal(int rssi)
{
    if (!signal_label) return;
    int active = rssi_to_bars(rssi);
    uint32_t n = lv_obj_get_child_cnt(signal_label);
    for (uint32_t i = 0; i < n && i < (uint32_t)SIGNAL_BAR_COUNT; i++) {
        lv_obj_t* bar = lv_obj_get_child(signal_label, i);
        lv_obj_set_style_bg_color(bar,
            lv_color_hex((int)i < active ? ACCENT : BG_INPUT), 0);
    }
}

void home_screen_update_channels()
{
    if (!hashtag_label) return;
    char buf[120];
    build_channel_string(buf, sizeof(buf));
    lv_label_set_text(hashtag_label, buf);
}

} // namespace slopos::ui
