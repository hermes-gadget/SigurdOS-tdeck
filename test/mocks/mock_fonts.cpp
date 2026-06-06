// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Mock font symbols for native testing.
// Provides stub definitions for fonts declared extern in lvgl.h
// so that emoji_font_setup.cpp can link without the real font data.
// The real fonts (emoji_font.c, Montserrat) require full LVGL internals
// which are unavailable in the mock environment.

#include "fonts/emoji_font.h"

// Minimal lv_font_t stub — the .fallback pointer is the only member
// the mock needs. Other fields are zero-initialized for C compatibility.
const lv_font_t lv_font_montserrat_10   = {nullptr};
const lv_font_t lv_font_montserrat_12   = {nullptr};
const lv_font_t lv_font_montserrat_14   = {nullptr};
const lv_font_t lv_font_montserrat_16   = {nullptr};
const lv_font_t lv_font_montserrat_18   = {nullptr};
const lv_font_t lv_font_montserrat_20   = {nullptr};
const lv_font_t lv_font_montserrat_24   = {nullptr};
const lv_font_t lv_font_montserrat_28   = {nullptr};

// The emoji font — empty struct, not backed by actual glyph data.
// Only used for pointer equality checks in fallback registration.
// The real emoji_font data lives in src/fonts/emoji_font.c.
const lv_font_t emoji_font = {nullptr};

// Latin-extended font — empty struct for native test link resolution.
// Only used for pointer equality checks in fallback registration.
// The real latin_ext_font data lives in src/fonts/latin_ext_font.c.
const lv_font_t latin_ext_font = {nullptr};
