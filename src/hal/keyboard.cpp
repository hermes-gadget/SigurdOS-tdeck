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


#include "keyboard.h"
#include "tdeck_pins.h"
#include "prefs.h"
#include <Arduino.h>
#include <Wire.h>
#if SIGURDOS_TELEMETRY
#include "../diagnostics/telemetry.h"
#endif

// ════════════════════════════════════════════════════════
// T-Deck Keyboard Protocol (ESP32-C3 I2C slave at 0x55)
//
// Architecture: The T-Deck has a dedicated ESP32-C3 that scans the
// physical keyboard matrix. The main ESP32-S3 communicates with it
// over I2C to read key codes and control backlight.
//
// Protocol based on the LilyGo T-Deck Keyboard_ESP32C3 firmware:
//   https://github.com/Xinyuan-LilyGO/T-Deck
//   License: MIT — Copyright (c) 2023 Shenzhen Xin Yuan Electronic Technology Co., Ltd
//
// I2C write commands (master → slave):
//   0x01 <duty>   Set backlight brightness (0-255)
//   0x02 <duty>   Set default brightness for Alt+B (minimum: 30)
//   0x03          Switch to raw mode (returns bitmask per column)
//   0x04          Switch to key mode (returns ASCII characters)
//
// I2C read (master ← slave):
//   Wire.requestFrom(0x55, 1) → 1 byte
//     In key mode: 0x00 = no key, otherwise ASCII char of pressed key
//                  (Enter=0x0D, Backspace=0x08, Alt+C=0x0C)
//     In raw mode: 5 bytes, one bitmask per column
//
// Keymap (col × row, 5×7 matrix):
//   Col0: q w sym a ALT SPC Mic
//   Col1: e s d   p x   z   LShift
//   Col2: r g t   RShift v c f
//   Col3: u h y   Enter  b n j
//   Col4: o l i   Bksp   $ m k
// ════════════════════════════════════════════════════════

static constexpr uint8_t  KB_I2C_ADDR              = 0x55;
static constexpr uint8_t  CMD_BRIGHTNESS            = 0x01;
static constexpr uint8_t  CMD_DEFAULT_BRIGHTNESS    = 0x02;
static constexpr uint8_t  CMD_MODE_RAW              = 0x03;  // raw bitmask mode
static constexpr uint8_t  CMD_MODE_KEY              = 0x04;  // ASCII key mode
static constexpr uint32_t KB_POLL_INTERVAL_MS       = 5;    // faster polling for responsive typing (was 10)
static constexpr uint8_t  KB_BACKLIGHT_DEFAULT      = 127;  // mid brightness

static bool     initialized     = false;
static uint32_t last_poll_ms    = 0;
static bool     shift_held      = false;
static bool     ctrl_held       = false;
static bool     alt_held        = false;

// ── Ring buffer for key events ─────────────────────────────
// Fixes single-slot latch that dropped fast key presses:
// sigurdos_keyboard_scan() now pushes every valid key into the buffer,
// and the LVGL indev callback dequeues one key at a time.
static constexpr int KEY_BUF_SIZE = 16;
static uint8_t  key_buf[KEY_BUF_SIZE] = {0};
static int      key_head  = 0;   // next write position
static int      key_tail  = 0;   // next read position
static int      key_count = 0;   // number of entries in buffer
static int      last_consumed_key = 0; // key returned by most recent consume_event()

// ════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════

bool sigurdos_keyboard_init()
{
    if (initialized) return true;

    // I2C bus must already be initialized (TDeckBoard::begin does this)
    Wire.setClock(100000);  // keyboard MCU uses 100kHz

    // Probe the keyboard MCU — request 1 byte, should ACK
    Wire.requestFrom(KB_I2C_ADDR, (uint8_t)1);
    if (Wire.available() == 0 || Wire.read() == -1) {
        // Keyboard MCU not responding — may need firmware flash or
        // peripheral power not enabled
        return false;
    }

    // Set initial backlight from stored preferences
    uint8_t brightness = sigurdos::prefs_get().kbd_backlight;
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_BRIGHTNESS);
    Wire.write(brightness);
    if (Wire.endTransmission() != 0) {
        initialized = false;
        return false;
    }
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_DEFAULT_BRIGHTNESS);
    Wire.write(brightness < 30 ? 30 : brightness);
    if (Wire.endTransmission() != 0) {
        initialized = false;
        return false;
    }

    // Switch to key mode (ASCII chars). The keyboard MCU defaults to key
    // mode on power-up, but this guards against it being in raw mode from
    // a prior session that didn't power-cycle the keyboard MCU.
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_MODE_KEY);
    if (Wire.endTransmission() != 0) {
        initialized = false;
        return false;
    }

    initialized = true;
    return true;
}

void sigurdos_keyboard_scan()
{
    if (!initialized) return;

    uint32_t now = millis();
    if (now - last_poll_ms < KB_POLL_INTERVAL_MS) return;
    last_poll_ms = now;

    // Ensure I2C clock is 100kHz for keyboard MCU (touch may have set it to 400kHz)
    Wire.setClock(100000);

    // Read 1 byte from keyboard MCU
    Wire.requestFrom(KB_I2C_ADDR, (uint8_t)1);
    int keyValue = 0;
    if (Wire.available() > 0) {
        keyValue = Wire.read();
    }

    if (keyValue <= 0 || keyValue == 0xFF) {
        // MCU returned 0 or invalid — nothing to enqueue.
        return;
    }

    // Push key into ring buffer (if full, overwrite oldest)
    key_buf[key_head] = (uint8_t)keyValue;
    key_head = (key_head + 1) % KEY_BUF_SIZE;
    if (key_count < KEY_BUF_SIZE) {
        key_count++;
    } else {
        // Buffer full — advance tail to discard oldest entry
        key_tail = (key_tail + 1) % KEY_BUF_SIZE;
    }

#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_key_event((uint8_t)keyValue);
#endif

    // Track modifier state based on key codes where possible.
    // Note: The T-Deck keyboard MCU sends pre-processed ASCII key codes,
    // not raw modifier+scancode, so modifier tracking is limited.
    // Shift state is best-effort — uppercase ASCII implies shift.
    int k = keyValue;
    if (k >= 'A' && k <= 'Z') {
        shift_held = true;
    } else if (k >= 'a' && k <= 'z') {
        shift_held = false;
    }
    // Alt and Ctrl are harder to detect from ASCII output alone.
    // Track via special key codes the MCU may send.
    switch (k) {
    case 0x0C: alt_held = !alt_held; break;   // Alt+C toggle sent by MCU
    case 0x0D: break;  // Enter — no modifier change
    case 0x08: break;  // Backspace — no modifier change
    default:   break;
    }
}

int sigurdos_keyboard_get_key()
{
    if (key_count > 0) {
        return key_buf[key_tail];
    }
    return last_consumed_key;
}

bool sigurdos_keyboard_is_shift()
{
    return shift_held;
}

bool sigurdos_keyboard_is_ctrl()
{
    return ctrl_held;
}

bool sigurdos_keyboard_is_alt()
{
    return alt_held;
}

bool sigurdos_keyboard_has_event()
{
    return key_count > 0;
}

bool sigurdos_keyboard_consume_event()
{
    if (key_count > 0) {
        last_consumed_key = key_buf[key_tail];
        key_tail = (key_tail + 1) % KEY_BUF_SIZE;
        key_count--;
        return true;
    }
    return false;
}

void sigurdos_keyboard_set_brightness(uint8_t duty)
{
    Wire.setClock(100000);
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_BRIGHTNESS);
    Wire.write(duty);
    if (Wire.endTransmission() != 0) {
        // Non-critical: backlight brightness update failed, device still usable
    }
}

void sigurdos_keyboard_set_default_brightness(uint8_t duty)
{
    if (duty < 30) duty = 30;  // minimum for Alt+B toggle
    Wire.setClock(100000);
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_DEFAULT_BRIGHTNESS);
    Wire.write(duty);
    if (Wire.endTransmission() != 0) {
        // Non-critical: default brightness update failed, device still usable
    }
}

void sigurdos_keyboard_reset_scan_state()
{
    last_poll_ms    = 0;
    key_head  = 0;
    key_tail  = 0;
    key_count = 0;
    last_consumed_key = 0;
    shift_held      = false;
    ctrl_held       = false;
    alt_held        = false;
}

void sigurdos_keyboard_consume_key()
{
    // consume_event() already dequeued the key from the ring buffer.
    // We just need to clear the latched value so subsequent get_key() calls
    // without a new event return 0 (no key pending).
    last_consumed_key = 0;
}

void sigurdos_keyboard_inject(uint8_t key_code)
{
    // Push into ring buffer (if full, overwrite oldest)
    key_buf[key_head] = key_code;
    key_head = (key_head + 1) % KEY_BUF_SIZE;
    if (key_count < KEY_BUF_SIZE) {
        key_count++;
    } else {
        key_tail = (key_tail + 1) % KEY_BUF_SIZE;
    }

    // Track modifier state based on key code
    if (key_code >= 'A' && key_code <= 'Z') {
        shift_held = true;
    } else if (key_code >= 'a' && key_code <= 'z') {
        shift_held = false;
    }
    switch (key_code) {
    case 0x0C: alt_held = !alt_held; break;
    default:   break;
    }
}
