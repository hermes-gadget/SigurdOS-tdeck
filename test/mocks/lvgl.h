#pragma once

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

// Mock lvgl.h for native PlatformIO testing
// Provides stubs for all LVGL v9 API functions used by SigurdOS UI code

#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
#else
#include <stdint.h>
#include <stddef.h>
#endif

// ── LVGL version ────────────────────────────────────────
#define LVGL_VERSION_MAJOR 9
#define LVGL_VERSION_MINOR 3

// ── Basic types ─────────────────────────────────────────
typedef uint32_t lv_color_t;
typedef int      lv_coord_t;
typedef uint32_t lv_opa_t;

// ── Color helpers ────────────────────────────────────────
inline lv_color_t lv_color_hex(uint32_t hex) { return hex; }
inline lv_color_t lv_color_make(uint8_t r, uint8_t g, uint8_t b) { return (r << 16) | (g << 8) | b; }

// ── Object type (complete, not opaque — needed for static locals in stubs) ──
struct _lv_obj_t {
    int dummy;
};
typedef _lv_obj_t lv_obj_t;

struct _lv_display_t {
    int dummy;
};
typedef _lv_display_t lv_display_t;

struct _lv_indev_t {
    int dummy;
};
typedef _lv_indev_t lv_indev_t;

struct _lv_timer_t {
    int delete_count;
    void* user_data;
};
typedef _lv_timer_t lv_timer_t;

// ── Event ────────────────────────────────────────────────
typedef struct _lv_event_t {
    lv_obj_t* target;
    lv_obj_t* current_target;
    void* user_data;
    void* param;
} lv_event_t;

typedef void (*lv_event_cb_t)(lv_event_t* e);

// ── Display ──────────────────────────────────────────────
#define LV_DISPLAY_RENDER_MODE_PARTIAL 0
#define LV_SCR_LOAD_ANIM_NONE     0
#define LV_SCR_LOAD_ANIM_FADE_ON  1
#define LV_SCR_LOAD_ANIM_MOVE_LEFT  2
#define LV_SCR_LOAD_ANIM_MOVE_RIGHT 3

// ── Alignment ────────────────────────────────────────────
#define LV_ALIGN_DEFAULT        0
#define LV_ALIGN_TOP_LEFT       1
#define LV_ALIGN_TOP_MID        2
#define LV_ALIGN_TOP_RIGHT      3
#define LV_ALIGN_BOTTOM_LEFT    4
#define LV_ALIGN_BOTTOM_MID     5
#define LV_ALIGN_BOTTOM_RIGHT   6
#define LV_ALIGN_LEFT_MID       7
#define LV_ALIGN_RIGHT_MID      8
#define LV_ALIGN_CENTER         9
#define LV_ALIGN_OUT_TOP_LEFT  10
#define LV_ALIGN_OUT_TOP_MID   11
#define LV_ALIGN_OUT_BOTTOM_MID 12
#define LV_ALIGN_FLEX_START    13
#define LV_ALIGN_FLEX_END      14

// ── Object flags ─────────────────────────────────────────
#define LV_OBJ_FLAG_CLICKABLE        (1 << 6)
#define LV_OBJ_FLAG_CHECKABLE        (1 << 7)
#define LV_OBJ_FLAG_SCROLLABLE       (1 << 8)
#define LV_OBJ_FLAG_SCROLL_ELASTIC   (1 << 9)
#define LV_OBJ_FLAG_SCROLL_MOMENTUM  (1 << 10)
#define LV_OBJ_FLAG_SCROLL_CHAIN_HOR (1 << 11)
#define LV_OBJ_FLAG_SCROLL_CHAIN_VER (1 << 12)
#define LV_OBJ_FLAG_SCROLL_CHAIN     (LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER)
#define LV_OBJ_FLAG_SCROLL_ON_FOCUS  (1 << 13)
#define LV_OBJ_FLAG_SCROLL_WITH_ARROW (1 << 14)

// ── Object states ───────────────────────────────────────
#define LV_STATE_DEFAULT  (0)
#define LV_STATE_FOCUSED (1 << 1)
#define LV_STATE_FOCUS_KEY (1 << 2)
#define LV_STATE_PRESSED  (1 << 3)

// ── Events ───────────────────────────────────────────────
#define LV_EVENT_CLICKED    0x07
#define LV_EVENT_PRESSED    0x01
#define LV_EVENT_DELETE     0x21

// ── Input device types ───────────────────────────────────
#define LV_INDEV_TYPE_POINTER  0
#define LV_INDEV_TYPE_KEYPAD   1
#define LV_INDEV_TYPE_ENCODER  2
#define LV_INDEV_TYPE_BUTTON   3

// ── Input states ─────────────────────────────────────────
#define LV_INDEV_STATE_RELEASED 0
#define LV_INDEV_STATE_PRESSED  1

typedef struct {
    lv_coord_t x, y;
    int16_t enc_diff;
    uint32_t key;
    uint8_t state;
} lv_indev_data_t;

// ── Style ────────────────────────────────────────────────
#define LV_OPA_TRANSP   0
#define LV_OPA_COVER    255
#define LV_OPA_10       25
#define LV_OPA_20       51
#define LV_OPA_30       76
#define LV_OPA_40      102
#define LV_OPA_50      127
#define LV_OPA_60      153
#define LV_OPA_70      178
#define LV_OPA_80      204
#define LV_OPA_90      229

#define LV_PCT(n) (n)  // simplified

#define LV_SIZE_CONTENT 0xFFFF

#define LV_RADIUS_CIRCLE 0x7FFF

#define LV_DIR_VER 2
#define LV_DIR_HOR 1
#define LV_DIR_ALL 3

#define LV_ANIM_OFF 0
#define LV_ANIM_ON  1

#define LV_TEXT_ALIGN_LEFT    0
#define LV_TEXT_ALIGN_CENTER  1
#define LV_TEXT_ALIGN_RIGHT   2

#define LV_LABEL_LONG_WRAP    1
#define LV_LABEL_LONG_DOT     2
#define LV_LABEL_LONG_SCROLL  3

#define LV_SCROLLBAR_MODE_OFF 0

#define LV_FLEX_FLOW_ROW_WRAP 0
#define LV_FLEX_FLOW_COLUMN   1
#define LV_FLEX_FLOW_ROW      2

#define LV_FLEX_ALIGN_SPACE_EVENLY 0
#define LV_FLEX_ALIGN_CENTER       1
#define LV_FLEX_ALIGN_START        2

#define LV_SYMBOL_LEFT      "\xEF\x81\x93"
#define LV_SYMBOL_RIGHT     "\xEF\x81\x94"
#define LV_SYMBOL_UPLOAD    "\xEF\x82\x93"
#define LV_SYMBOL_USER      "\xEF\x80\x87"
#define LV_SYMBOL_SETTINGS  "\xEF\x80\x93"
#define LV_SYMBOL_WIFI      "\xEF\x87\xAB"
#define LV_SYMBOL_SHUFFLE   "\xEF\x81\xB4"
#define LV_SYMBOL_BATTERY_FULL  "\xEF\x89\x80"
#define LV_SYMBOL_SD_CARD   "\xEF\x9F\x82"
#define LV_SYMBOL_GPS       "\xEF\x84\xA4"
#define LV_SYMBOL_HOME      "\xEF\x80\x95"
#define LV_SYMBOL_LOOP      "\xEF\x81\xB9"

// ── Global init ──────────────────────────────────────────
inline void lv_init() {}

// ── Screen ───────────────────────────────────────────────
inline lv_obj_t* lv_scr_act() {
    static lv_obj_t s;
    return &s;
}
inline void lv_scr_load(lv_obj_t*) {}
inline void lv_scr_load_anim(lv_obj_t*, int, int, int, bool) {}

// ── Object creation ──────────────────────────────────────
inline lv_obj_t* lv_obj_create(lv_obj_t* parent) {
    static lv_obj_t pool[256];
    static int next = 0;
    return &pool[(next++) % 256];
}

inline void lv_obj_del(lv_obj_t*) {}
inline void lv_obj_del_async(lv_obj_t*) {}

// ── Object properties ────────────────────────────────────
inline void lv_obj_set_size(lv_obj_t*, lv_coord_t, lv_coord_t) {}
inline void lv_obj_set_width(lv_obj_t*, lv_coord_t) {}
inline void lv_obj_set_height(lv_obj_t*, lv_coord_t) {}
inline void lv_obj_set_pos(lv_obj_t*, lv_coord_t, lv_coord_t) {}
inline void lv_obj_align(lv_obj_t*, int, lv_coord_t, lv_coord_t) {}
inline void lv_obj_center(lv_obj_t*) {}
inline void lv_obj_add_flag(lv_obj_t*, uint32_t) {}
inline void lv_obj_remove_flag(lv_obj_t*, uint32_t) {}
inline void lv_obj_scroll_to_view(lv_obj_t*, int) {}
inline void lv_obj_invalidate(lv_obj_t*) {}
inline void lv_obj_set_scrollbar_mode(lv_obj_t*, int) {}
inline lv_coord_t lv_obj_get_width(lv_obj_t*) { return 100; }
inline lv_coord_t lv_obj_get_height(lv_obj_t*) { return 100; }
inline lv_obj_t* lv_obj_get_child(lv_obj_t*, int) { return nullptr; }
inline int lv_obj_get_child_cnt(lv_obj_t*) { return 0; }

// ── Style ────────────────────────────────────────────────
inline void lv_obj_set_style_bg_color(lv_obj_t*, lv_color_t, int) {}
inline void lv_obj_set_style_bg_opa(lv_obj_t*, lv_opa_t, int) {}
inline void lv_obj_set_style_text_color(lv_obj_t*, lv_color_t, int) {}
inline void lv_obj_set_style_text_font(lv_obj_t*, const void*, int) {}
inline void lv_obj_set_style_radius(lv_obj_t*, lv_coord_t, int) {}
inline void lv_obj_set_style_border_width(lv_obj_t*, lv_coord_t, int) {}
inline void lv_obj_set_style_border_color(lv_obj_t*, lv_color_t, int) {}
inline void lv_obj_set_style_pad_all(lv_obj_t*, lv_coord_t, int) {}
inline void lv_obj_set_style_pad_gap(lv_obj_t*, lv_coord_t, int) {}
inline void lv_obj_set_style_arc_color(lv_obj_t*, lv_color_t, int) {}

// ── Flex / Grid ──────────────────────────────────────────
inline void lv_obj_set_flex_flow(lv_obj_t*, int) {}
inline void lv_obj_set_flex_align(lv_obj_t*, int, int, int) {}
inline void lv_obj_set_scroll_dir(lv_obj_t*, int) {}

// ── Event ────────────────────────────────────────────────
inline void lv_obj_add_event_cb(lv_obj_t*, lv_event_cb_t, int, void*) {}
inline void* lv_event_get_user_data(lv_event_t* e) { return e ? e->user_data : nullptr; }
inline lv_obj_t* lv_event_get_target(lv_event_t* e) { return e ? e->target : nullptr; }

// ── Label ────────────────────────────────────────────────
inline lv_obj_t* lv_label_create(lv_obj_t* parent) { return lv_obj_create(parent); }
inline void lv_label_set_text(lv_obj_t*, const char*) {}
inline void lv_label_set_text_static(lv_obj_t*, const char*) {}
inline void lv_label_set_long_mode(lv_obj_t*, int) {}
inline void lv_label_set_align(lv_obj_t*, int) {}

// ── Button ───────────────────────────────────────────────
inline lv_obj_t* lv_btn_create(lv_obj_t* parent) { return lv_obj_create(parent); }

// ── Textarea ─────────────────────────────────────────────
inline lv_obj_t* lv_textarea_create(lv_obj_t* parent) { return lv_obj_create(parent); }
inline void lv_textarea_set_text(lv_obj_t*, const char*) {}
inline void lv_textarea_set_one_line(lv_obj_t*, bool) {}
inline void lv_textarea_set_placeholder_text(lv_obj_t*, const char*) {}

// ── List ─────────────────────────────────────────────────
inline lv_obj_t* lv_list_create(lv_obj_t* parent) { return lv_obj_create(parent); }
inline lv_obj_t* lv_list_add_btn(lv_obj_t*, const void*, const char*) { return lv_obj_create(nullptr); }
inline lv_obj_t* lv_list_add_text(lv_obj_t*, const char*) { return lv_obj_create(nullptr); }

// ── Spinner ──────────────────────────────────────────────
inline lv_obj_t* lv_spinner_create(lv_obj_t* parent, int, int) { return lv_obj_create(parent); }

// ── Display ──────────────────────────────────────────────
inline lv_display_t* lv_display_create(lv_coord_t, lv_coord_t) { return nullptr; }
inline void lv_display_set_flush_cb(lv_display_t*, void(*)(lv_display_t*, const void*, uint8_t*)) {}
inline void lv_display_set_buffers(lv_display_t*, void*, void*, size_t, int) {}
inline void lv_display_flush_ready(lv_display_t*) {}

// ── Indev ────────────────────────────────────────────────
inline lv_indev_t* lv_indev_create() { return nullptr; }
inline void lv_indev_set_type(lv_indev_t*, int) {}
inline void lv_indev_set_read_cb(lv_indev_t*, void(*)(lv_indev_t*, lv_indev_data_t*)) {}

// ── Timer ────────────────────────────────────────────────
inline uint32_t lv_timer_handler() { return 5; }
inline void lv_timer_del(lv_timer_t* timer) {
    if (timer) timer->delete_count++;
}

// ── Fonts (forward-declared as extern) ───────────────────
#ifdef __cplusplus
extern "C" {
#endif
struct lv_font_t {
    const void* fallback;
};
extern const lv_font_t lv_font_montserrat_10;
extern const lv_font_t lv_font_montserrat_12;
extern const lv_font_t lv_font_montserrat_14;
extern const lv_font_t lv_font_montserrat_16;
extern const lv_font_t lv_font_montserrat_18;
extern const lv_font_t lv_font_montserrat_20;
extern const lv_font_t lv_font_montserrat_24;
extern const lv_font_t lv_font_montserrat_28;
extern const lv_font_t emoji_font;
#ifdef __cplusplus
}
#endif
