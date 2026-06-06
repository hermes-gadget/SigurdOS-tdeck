// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Latin-extended font (Montserrat Regular) providing Latin-1 Supplement +
// Latin Extended-A glyphs for Western/Central European accented characters.
// Used as a fallback between the ASCII-only Montserrat fonts and the emoji font.
// Size: 16px, Bpp: 4 (grayscale anti-aliasing)
extern const lv_font_t latin_ext_font;

// Register latin_ext_font as an intermediate fallback.
// Call during UI initialization, after emoji_font_register_fallback().
void latin_ext_font_register();

#ifdef __cplusplus
}
#endif
