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

#include <cstdint>

// ── Display lifecycle ───────────────────────────────────
bool sigurdos_display_init();
void sigurdos_display_init_inputs();
void sigurdos_display_loop();
void sigurdos_display_render_now();
uint32_t sigurdos_display_millis();

// ── Auto-off (power saving) ─────────────────────────────
// After AUTO_OFF_MS of no user input, turns off backlight.
// Touch or keyboard input automatically wakes the display.
// Call sigurdos_display_wake() from input handlers to reset timer.
void sigurdos_display_wake();
bool sigurdos_display_is_on();
void sigurdos_display_set_brightness(uint8_t brightness);

// Re-reads auto_off_timeout from prefs and resets the auto-off timer.
// Call after changing the timeout in Settings.
void sigurdos_display_reset_auto_off();

// Return the current screen buffer for screenshot capture.
// Only available when full-screen buffer mode is active (PSRAM present).
void* sigurdos_display_get_buffer();
uint32_t sigurdos_display_get_width();
uint32_t sigurdos_display_get_height();

// Convert a Unicode codepoint into LVGL's little-endian UTF-8 keypad payload.
// ASCII is returned unchanged; invalid Unicode returns '?'.
inline uint32_t sigurdos_display_encode_text_key(uint32_t codepoint)
{
    if (codepoint < 0x80) return codepoint;
    if ((codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint >= 0x110000) {
        return '?';
    }

    uint8_t bytes[4] = {0, 0, 0, 0};
    if (codepoint < 0x0800) {
        bytes[0] = (uint8_t)(((codepoint >> 6) & 0x1F) | 0xC0);
        bytes[1] = (uint8_t)((codepoint & 0x3F) | 0x80);
    } else if (codepoint < 0x010000) {
        bytes[0] = (uint8_t)(((codepoint >> 12) & 0x0F) | 0xE0);
        bytes[1] = (uint8_t)(((codepoint >> 6) & 0x3F) | 0x80);
        bytes[2] = (uint8_t)((codepoint & 0x3F) | 0x80);
    } else {
        bytes[0] = (uint8_t)(((codepoint >> 18) & 0x07) | 0xF0);
        bytes[1] = (uint8_t)(((codepoint >> 12) & 0x3F) | 0x80);
        bytes[2] = (uint8_t)(((codepoint >> 6) & 0x3F) | 0x80);
        bytes[3] = (uint8_t)((codepoint & 0x3F) | 0x80);
    }

    return ((uint32_t)bytes[0]) |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

// Capture the current screen and dump it over Serial as hex-encoded RGB565.
// Uses lv_snapshot_take_to_draw_buf for a clean capture.
void sigurdos_display_capture_framebuffer();

#if defined(SIGURDOS_REMOTE_TEST)
void sigurdos_test_set_touch(int x, int y);
#endif
