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

// Internal keyboard event: request an on-screen character picker for a base key.
// The low byte stores the ASCII base character, e.g. 'c' or 'C'.
static constexpr uint32_t SIGURDOS_KEY_CHAR_PICKER_BASE = 0x01000000u;
static constexpr uint32_t SIGURDOS_KEY_CHAR_PICKER_MASK = 0xFFFFFF00u;

inline uint32_t sigurdos_keyboard_char_picker_key(uint8_t base)
{
    return SIGURDOS_KEY_CHAR_PICKER_BASE | (uint32_t)base;
}

inline bool sigurdos_keyboard_is_char_picker_key(uint32_t key)
{
    return (key & SIGURDOS_KEY_CHAR_PICKER_MASK) == SIGURDOS_KEY_CHAR_PICKER_BASE;
}

inline char sigurdos_keyboard_char_picker_base(uint32_t key)
{
    return (char)(key & 0xFFu);
}

// ── T-Deck Keyboard (ESP32-C3 via I2C) ─────────────────
// The T-Deck keyboard is a separate ESP32-C3 MCU on I2C address 0x55.
// It handles matrix scanning, debouncing, modifier keys, and backlight.
// The main ESP32-S3 reads key codes over I2C.

// Initialize communication with the keyboard MCU
// Must be called after I2C bus is configured (Wire.begin)
// Returns true if keyboard is detected
bool sigurdos_keyboard_init();

// Poll the keyboard for new keypresses (call each frame).
// Uses the C3's model-independent ASCII key mode as the primary path and brief
// raw samples only for host-side Alt/Mic/Sym compatibility features.
void sigurdos_keyboard_scan();

// Get the key code of the last keypress (ASCII/LVGL Unicode codepoint, 0 if none)
int sigurdos_keyboard_get_key();

// Returns true if a new key event is available (one-shot, consumed on read)
/// @brief Check if a new keyboard event is available (non-destructive predicate).
/// @return true if an event is pending (does NOT consume it).
bool sigurdos_keyboard_has_event();

/// @brief Consume a pending event — returns true if one was available and clears the flag.
/// Use this instead of sigurdos_keyboard_has_event() when you intend to immediately
/// call sigurdos_keyboard_consume_key() afterward.
/// @return true if an event was consumed
bool sigurdos_keyboard_consume_event();

// ── Backlight control ──────────────────────────────────
// Set keyboard backlight brightness (0-255, 0=off)
void sigurdos_keyboard_set_brightness(uint8_t duty);

// Set the default brightness used when toggling with Alt+B (30-255)
void sigurdos_keyboard_set_default_brightness(uint8_t duty);

// ── Modifier state (derived from key codes, not I2C) ───
bool sigurdos_keyboard_is_shift();
bool sigurdos_keyboard_is_ctrl();  // retained for tests (DEAD-001: not yet wired)
bool sigurdos_keyboard_is_alt();

struct SigurdOSKeyboardDiag {
    bool initialized;
    bool raw_supported;
    bool raw_unavailable;
    bool raw_valid;
    bool shift;
    bool ctrl;
    bool alt;
    bool sym_down;
    bool mic_down;
    uint8_t layout;
    uint8_t last_key_mode_byte;
    uint8_t raw_matrix[5];
    uint32_t last_output_codepoint;
    uint32_t event_count;
    uint32_t overwrite_count;
    uint32_t last_event_ms;
};

// Snapshot keyboard diagnostic state without consuming the pending key queue.
bool sigurdos_keyboard_get_diag(SigurdOSKeyboardDiag* out);

// Reset internal scan state (poll timer, key buffer, modifier flags).
// Does NOT reset the `initialized` flag or I2C communication.
// Useful for testing and on device wake from deep sleep.
void sigurdos_keyboard_reset_scan_state();

// Reset the static initialized flag. Used by tests to isolate
// warm-handoff retry tests from other keyboard init tests.
// No-op in production builds — the flag is never reset at runtime.
void sigurdos_keyboard_reset_init_for_test();

// Consume/clear the current key event (used by LVGL indev after reporting a press).
// Prevents the same character from being fed again on the next read.
void sigurdos_keyboard_consume_key();

// Inject a simulated keypress (for remote test mode).
// key_code: ASCII character or special code (0x0D=Enter, 0x08=Backspace, 0x1B=Esc).
void sigurdos_keyboard_inject(uint8_t key_code);

// Inject a simulated Unicode codepoint (for tests and future remote input).
// The display input bridge encodes non-ASCII values for LVGL textareas.
void sigurdos_keyboard_inject_codepoint(uint32_t key_code);

// Get the count of keyboard events silently overwritten when the
// ring buffer was full. Useful for diagnostics and input stress testing.
uint32_t sigurdos_keyboard_overwrite_count();
