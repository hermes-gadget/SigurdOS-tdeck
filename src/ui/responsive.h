#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Responsive layout helpers — adapt UI to any display size/orientation.
// All functions are constexpr where possible for zero runtime overhead.

#include "hal/tdeck_pins.h"
#include <lvgl.h>
#include <algorithm>

namespace sigurdos::responsive {

// ── Display metadata ────────────────────────────────────
constexpr int DISPLAY_W = TFT_WIDTH;
constexpr int DISPLAY_H = TFT_HEIGHT;
constexpr bool IS_PORTRAIT  = (DISPLAY_H > DISPLAY_W);
constexpr bool IS_LANDSCAPE = (DISPLAY_W > DISPLAY_H);
constexpr bool IS_SQUARE    = (DISPLAY_W == DISPLAY_H);

// ── Safe-area insets (tune for round/cutout displays) ────
constexpr int SAFE_LEFT   = 0;
constexpr int SAFE_RIGHT  = 0;
constexpr int SAFE_TOP    = 0;
constexpr int SAFE_BOTTOM = 0;

// ── Bar heights — proportional, with minimums ────────────
// Scaled to ~9% of display height, clamped for readability.
constexpr int bar_height() {
    return (DISPLAY_H / 11 < 12) ? 12 : (DISPLAY_H / 11 > 28) ? 28 : DISPLAY_H / 11;
}

constexpr int TOP_BAR_H  = bar_height();
constexpr int BOT_BAR_H  = bar_height() - 2;  // slightly smaller
constexpr int DIVIDER_H  = 1;

// ── Content area ─────────────────────────────────────────
constexpr int CONTENT_Y  = TOP_BAR_H + DIVIDER_H + SAFE_TOP;
constexpr int CONTENT_H  = DISPLAY_H - CONTENT_Y - DIVIDER_H - BOT_BAR_H - SAFE_BOTTOM;
constexpr int CONTENT_X  = SAFE_LEFT;
constexpr int CONTENT_W  = DISPLAY_W - SAFE_LEFT - SAFE_RIGHT;

struct DisplayGeometry {
    int width;
    int height;
    int top_bar_h;
    int bottom_bar_h;
    int content_x;
    int content_y;
    int content_w;
    int content_h;
    bool portrait;
};

inline DisplayGeometry geometry_for(int width, int height)
{
    const int raw_bar = height / 11;
    const int top = raw_bar < 12 ? 12 : (raw_bar > 28 ? 28 : raw_bar);
    const int bottom = top - 2;
    return {width, height, top, bottom, SAFE_LEFT, top + DIVIDER_H + SAFE_TOP,
            width - SAFE_LEFT - SAFE_RIGHT,
            height - (top + DIVIDER_H + SAFE_TOP) - DIVIDER_H - bottom - SAFE_BOTTOM,
            height > width};
}

inline DisplayGeometry runtime_geometry()
{
    lv_display_t* display = lv_display_get_default();
    if (!display) return geometry_for(DISPLAY_W, DISPLAY_H);
    const int width = lv_display_get_horizontal_resolution(display);
    const int height = lv_display_get_vertical_resolution(display);
    return geometry_for(width > 0 ? width : DISPLAY_W, height > 0 ? height : DISPLAY_H);
}

// ── Fixed bottom-row offsets ─────────────────────────────
// LVGL bottom alignment uses offsets from the screen bottom. These helpers
// keep stacked fixed rows above the bottom bar from covering each other or the
// scrollable content area.
constexpr int bottom_stack_offset(int row_index, int prev_row_h = 30, int gap = 4) {
    return row_index * (prev_row_h + gap);
}

constexpr int bottom_aligned_top(int row_h, int offset_from_content_bottom) {
    return DISPLAY_H - (BOT_BAR_H + DIVIDER_H + offset_from_content_bottom) - row_h;
}

constexpr int contact_acl_offset(bool is_room_type, bool room_logged_in) {
    return room_logged_in ? 132 : (is_room_type ? 98 : 128);
}

constexpr int contact_qr_offset(bool is_room_type, bool room_logged_in) {
    return room_logged_in ? 102 : (is_room_type ? 68 : 98);
}

constexpr int contact_bottom_reserved(bool is_room_type, bool room_logged_in) {
    return CONTENT_H - ((bottom_aligned_top(22, contact_acl_offset(is_room_type, room_logged_in)) - 2) - CONTENT_Y);
}

// ── Hashtag label width — available space in top bar ─────
// Leaves room for hamburger icon (left) and time label (right).
inline int HASHTAG_LABEL_W() {
    return runtime_geometry().width - 60;  // hamburger(20) + time(32) + margins(8)
}

// ── Grid layout — adaptive columns ───────────────────────
// tile_w/tile_h omitted — caller uses LV_GRID_FR(1) to fill space exactly.
struct GridLayout { int cols; };

inline GridLayout compute_grid(int /*grid_pad*/ = 3) {
    GridLayout g{};
    const int content_w = runtime_geometry().content_w;
    // Choose column count based on available width per tile.
    if (content_w >= 300)      g.cols = 4;
    else if (content_w >= 230) g.cols = 3;
    else if (content_w >= 170) g.cols = 2;
    else                       g.cols = 1;
    return g;
}

// ── Proportional column widths ───────────────────────────
// Distributes 'total_w' among 'n' columns using given weights.
// Returns each column's x-offset in out_offsets (must be size n).
template<int N>
inline void column_offsets(const int (&weights)[N], int total_w,
                               int (&out_x)[N], int start_x = 0) {
    int sum = 0;
    for (int i = 0; i < N; i++) {
        if (weights[i] > 0) sum += weights[i];
    }

    if (total_w <= 0 || sum <= 0) {
        for (int i = 0; i < N; i++) out_x[i] = start_x;
        return;
    }

    int x = start_x;
    for (int i = 0; i < N; i++) {
        out_x[i] = x;
        const int weight = weights[i] > 0 ? weights[i] : 0;
        x += (total_w * weight) / sum;
    }
}

// ── Dialog size — capped to display ──────────────────────
struct DialogSize { int w; int h; DialogSize() : w(0), h(0) {} DialogSize(int w_, int h_) : w(w_), h(h_) {} };

inline DialogSize dialog_size(int desired_w, int desired_h,
                              int margin = 16) {
    const DisplayGeometry geometry = runtime_geometry();
    return DialogSize(
        std::min(desired_w, geometry.width - margin * 2),
        std::min(desired_h, geometry.height - margin * 2));
}

// ── Widget width — capped as percentage of display ────────
inline int capped_width(int desired_w, int max_pct = 90) {
    int cap = (runtime_geometry().width * max_pct) / 100;
    return std::min(desired_w, cap);
}

// ── Bar-height widget — scaled proportionally ────────────
inline int bar_widget_h(int pct_of_content = 33) {
    return (runtime_geometry().content_h * pct_of_content) / 100;
}

} // namespace sigurdos::responsive
