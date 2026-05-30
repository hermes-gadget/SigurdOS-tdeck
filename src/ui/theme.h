#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// SlopOS Pixel Theme — Discord-inspired palette with blocky pixel styling
// Theme colors are runtime variables (inline) so they can be changed by the
// theme system and persist across reboots via NVS.

#include <lvgl.h>
#include <cstdint>

namespace slopos::theme {

// ── Themeable backgrounds ───────────────────────────────
inline uint32_t BG_PRIMARY   = 0x0f0f0f;  // deep black
inline uint32_t BG_SECONDARY = 0x181818;  // status bars
inline uint32_t BG_TERTIARY  = 0x1e1e1e;  // card/icon tile background
inline uint32_t BG_INPUT     = 0x252525;  // input field

// ── Themeable accents ───────────────────────────────────
inline uint32_t ACCENT       = 0x00bfff;  // bright cyan
inline uint32_t ACCENT_HOVER = 0x00a5e0;

// ── Semantic accents (always fixed — status indicators) ──
constexpr uint32_t ACCENT_GREEN = 0x3ba55d;
constexpr uint32_t ACCENT_RED   = 0xed4245;
constexpr uint32_t ACCENT_ORANGE= 0xfaa61a;
constexpr uint32_t ACCENT_YELLOW= 0xfee75c;

// ── Message bubbles ──────────────────────────────────────
constexpr uint32_t MSG_INCOMING = 0x3a4560;

// ── Text (always fixed for readability) ──────────────────
constexpr uint32_t TEXT_PRIMARY   = 0xf2f3f5;
constexpr uint32_t TEXT_SECONDARY = 0x949ba4;
constexpr uint32_t TEXT_MUTED     = 0x6b7078;
constexpr uint32_t TEXT_LINK      = 0x00aff4;

// ── Themeable channel colors ─────────────────────────────
inline uint32_t CHANNEL_HASH   = 0x00bfff;
constexpr uint32_t CHANNEL_ACTIVE = 0xffffff;

// ── Structural ───────────────────────────────────────────
constexpr uint32_t DIVIDER        = 0x2a2a2a;

// ── Pixel border width ───────────────────────────────────
constexpr int32_t PIXEL_BORDER    = 2;

// ── Theme preset definitions ─────────────────────────────
struct ThemeDef {
    const char* name;       // display name
    uint32_t bg_primary;
    uint32_t bg_secondary;
    uint32_t bg_tertiary;
    uint32_t bg_input;
    uint32_t accent;
    uint32_t accent_hover;
    uint32_t channel_hash;
};

constexpr ThemeDef THEMES[] = {
    // 0: Default Cyan
    {"Default Cyan",   0x0f0f0f, 0x181818, 0x1e1e1e, 0x252525, 0x00bfff, 0x00a5e0, 0x00bfff},
    // 1: Midnight Blue
    {"Midnight Blue",  0x050510, 0x0d0d20, 0x12122a, 0x181838, 0x4488ff, 0x3377ee, 0x4488ff},
    // 2: Forest Green
    {"Forest Green",   0x0a0f0a, 0x121a12, 0x182218, 0x1e2a1e, 0x3ba55d, 0x2d8a4a, 0x3ba55d},
    // 3: Sunset Orange
    {"Sunset Orange",  0x0f0a06, 0x1a1410, 0x221c18, 0x2a2420, 0xfaa61a, 0xe09510, 0xfaa61a},
    // 4: Royal Purple
    {"Royal Purple",   0x0c0a12, 0x14121e, 0x1a1828, 0x222036, 0x9b59b6, 0x8a44a5, 0x9b59b6},
    // 5: Amber Glow
    {"Amber Glow",     0x0f0c06, 0x181610, 0x1e1c18, 0x282420, 0xffa500, 0xe89400, 0xffa500},
};

constexpr int NUM_THEMES = 6;

// Apply a theme preset by index (clamped to 0..NUM_THEMES-1)
inline void theme_apply(uint8_t id)
{
    if (id >= NUM_THEMES) id = 0;
    const auto& t = THEMES[id];
    BG_PRIMARY   = t.bg_primary;
    BG_SECONDARY = t.bg_secondary;
    BG_TERTIARY  = t.bg_tertiary;
    BG_INPUT     = t.bg_input;
    ACCENT       = t.accent;
    ACCENT_HOVER = t.accent_hover;
    CHANNEL_HASH = t.channel_hash;
}

// ── Signal dot constants ──────────────────────────────────
constexpr int SIGNAL_DOT_COUNT = 5;
constexpr int SIGNAL_DOT_DIAM  = 11;    // diameter of each dot (px)
constexpr int SIGNAL_DOT_GAP   = 3;     // gap between dots (px)
constexpr int SIGNAL_DOT_H     = 22;    // container height (px)

// Convert RSSI dBm to number of active dots (1-5)
inline int rssi_to_dots(int rssi)
{
    if (rssi > -70)  return 5;
    if (rssi > -85)  return 4;
    if (rssi > -95)  return 3;
    if (rssi > -105) return 2;
    return 1;
}

// Create a row of iOS-style signal strength dots.
// Active dots are filled ACCENT, inactive are unfilled (border only, TEXT_MUTED).
inline lv_obj_t* create_signal_dots(lv_obj_t* parent, int rssi)
{
    int total_w = SIGNAL_DOT_COUNT * SIGNAL_DOT_DIAM
                + (SIGNAL_DOT_COUNT - 1) * SIGNAL_DOT_GAP;

    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_set_size(cont, total_w, SIGNAL_DOT_H);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);

    int active = rssi_to_dots(rssi);

    for (int i = 0; i < SIGNAL_DOT_COUNT; i++) {
        lv_obj_t* dot = lv_obj_create(cont);
        lv_obj_set_size(dot, SIGNAL_DOT_DIAM, SIGNAL_DOT_DIAM);
        if (i < active) {
            lv_obj_set_style_bg_color(dot, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
        } else {
            lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(dot, 2, 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(TEXT_MUTED), 0);
        }
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID,
                     i * (SIGNAL_DOT_DIAM + SIGNAL_DOT_GAP), 0);
    }

    return cont;
}

// ── Apply dark background to an object ──────────────────
inline void apply_dark_bg(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

// ── Shared keyboard/trackball focus treatment ───────────
inline void apply_focus_style(lv_obj_t* obj) {
    lv_obj_set_style_border_color(obj, lv_color_hex(ACCENT), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(obj, PIXEL_BORDER, LV_STATE_FOCUS_KEY);
}

// ── Pixel card style (0-radius, dark bg, 2px border) ────
inline void apply_pixel_card(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, PIXEL_BORDER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_pad_all(obj, 6, 0);
    apply_focus_style(obj);
}

// ── Pixel card with accent border ───────────────────────
inline void apply_pixel_card_accent(lv_obj_t* obj) {
    apply_pixel_card(obj);
    lv_obj_set_style_border_color(obj, lv_color_hex(ACCENT), 0);
}

// ── Pixel button (filled) ───────────────────────────────
inline void apply_pixel_btn(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, PIXEL_BORDER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(ACCENT_HOVER), 0);
    lv_obj_set_style_pad_all(obj, 6, 0);
    apply_focus_style(obj);
}

// ── Pixel button (outline) ──────────────────────────────
inline void apply_pixel_btn_outline(lv_obj_t* obj) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, PIXEL_BORDER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_pad_all(obj, 6, 0);
    apply_focus_style(obj);
}

// ── Top-bar icon button ─────────────────────────────────
inline void apply_topbar_icon_btn(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(DIVIDER), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    apply_focus_style(obj);
}

// ── Pixel input field ───────────────────────────────────
inline void apply_pixel_input(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, PIXEL_BORDER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_pad_all(obj, 6, 0);
    apply_focus_style(obj);
}

// ── Pixel badge (small accent label) ────────────────────
inline void apply_pixel_badge(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_30, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_pad_all(obj, 2, 0);
}

// ── Legacy card style (kept for compatibility) ──────────
inline void apply_card_style(lv_obj_t* obj) {
    apply_pixel_card(obj);
}

} // namespace slopos::theme
