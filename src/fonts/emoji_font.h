// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Emoji font with commonly used emoji subset (Noto Emoji, SIL OFL)
// Automatically generated — see scripts/generate_emoji_font.sh
// Size: 16px, Bpp: 4 (grayscale anti-aliasing)
extern const lv_font_t emoji_font;

// Latin-extended font (Montserrat Regular) providing Latin-1 Supplement +
// Latin Extended-A glyphs (äöüéèàç, etc.) for Western/Central European
// accented characters. Used as an intermediate fallback in the chain:
// Montserrat → latin_ext_font → emoji_font.
// Generated — see scripts/generate_latin_ext_font.sh
// Size: 16px, Bpp: 4
extern const lv_font_t latin_ext_font;

// Writable wrappers around Montserrat fonts with emoji fallback set.
// Use these instead of lv_font_montserrat_XX where emoji support is needed.
extern const lv_font_t* emoji_wrapped_montserrat_10;
extern const lv_font_t* emoji_wrapped_montserrat_12;
extern const lv_font_t* emoji_wrapped_montserrat_14;
extern const lv_font_t* emoji_wrapped_montserrat_16;
extern const lv_font_t* emoji_wrapped_montserrat_18;
extern const lv_font_t* emoji_wrapped_montserrat_20;
extern const lv_font_t* emoji_wrapped_montserrat_24;
extern const lv_font_t* emoji_wrapped_montserrat_28;

// Register emoji font as fallback for all Montserrat sizes used in the UI.
// Call once during UI initialization.
void emoji_font_register_fallback();

// Get the total number of emoji codepoints in the font.
// Useful for the emoji-list terminal command and test verification.
int emoji_font_get_count();

// Get emoji UTF-8 string by index. Returns nullptr if index is out of range.
const char* emoji_font_get_by_index(int index);

#ifdef __cplusplus
}
#endif
