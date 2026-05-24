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


#include "keyboard.h"
#include "tdeck_pins.h"
#include "prefs.h"
#include <Arduino.h>
#include <Wire.h>

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
static bool     has_new_event   = false;
static int      current_key     = 0;     // int: Wire.read() returns int, -1 = error
static int      latched_key     = 0;     // key currently considered "active" until MCU reports 0
static uint32_t last_poll_ms    = 0;
static bool     shift_held      = false;
static bool     ctrl_held       = false;
static bool     alt_held        = false;

// ════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════

bool slopos_keyboard_init()
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
    uint8_t brightness = slopos::prefs_get().kbd_backlight;
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

void slopos_keyboard_scan()
{
    if (!initialized) return;

    uint32_t now = millis();
    if (now - last_poll_ms < KB_POLL_INTERVAL_MS) return;
    last_poll_ms = now;

    // Read 1 byte from keyboard MCU
    Wire.requestFrom(KB_I2C_ADDR, (uint8_t)1);
    int keyValue = 0;
    if (Wire.available() > 0) {
        keyValue = Wire.read();
    }

    if (keyValue <= 0 || keyValue == 0xFF) {
        // MCU returned 0 — this single-shot MCU only reports each key once, then returns 0.
        // Do NOT clear latched_key or has_new_event here; they persist until
        // slopos_keyboard_consume_key() is called by the LVGL indev callback.
        // Clearing here was the race: display loop could wipe the key before LVGL ever saw it.
        return;
    }

    // Store the key code (ASCII value) and latch it until we see a 0 from the MCU
    latched_key = keyValue;
    has_new_event = true;
    current_key = keyValue;

    // Track modifier state based on key codes where possible.
    // Note: The T-Deck keyboard MCU sends pre-processed ASCII key codes,
    // not raw modifier+scancode, so modifier tracking is limited.
    // Shift state is best-effort — uppercase ASCII implies shift.
    if (current_key >= 'A' && current_key <= 'Z') {
        shift_held = true;
    } else if (current_key >= 'a' && current_key <= 'z') {
        shift_held = false;
    }
    // Alt and Ctrl are harder to detect from ASCII output alone.
    // Track via special key codes the MCU may send.
    switch (current_key) {
    case 0x0C: alt_held = !alt_held; break;   // Alt+C toggle sent by MCU
    case 0x0D: break;  // Enter — no modifier change
    case 0x08: break;  // Backspace — no modifier change
    default:   break;
    }
}

int slopos_keyboard_get_key()
{
    return latched_key ? latched_key : current_key;
}

bool slopos_keyboard_is_shift()
{
    return shift_held;
}

bool slopos_keyboard_is_ctrl()
{
    return ctrl_held;
}

bool slopos_keyboard_is_alt()
{
    return alt_held;
}

bool slopos_keyboard_has_new_event()
{
    if (has_new_event) {
        has_new_event = false;  // consume the event
        return true;
    }
    return false;
}

void slopos_keyboard_set_brightness(uint8_t duty)
{
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_BRIGHTNESS);
    Wire.write(duty);
    if (Wire.endTransmission() != 0) {
        // Non-critical: backlight brightness update failed, device still usable
    }
}

void slopos_keyboard_set_default_brightness(uint8_t duty)
{
    if (duty < 30) duty = 30;  // minimum for Alt+B toggle
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_DEFAULT_BRIGHTNESS);
    Wire.write(duty);
    if (Wire.endTransmission() != 0) {
        // Non-critical: default brightness update failed, device still usable
    }
}

void slopos_keyboard_reset_scan_state()
{
    last_poll_ms    = 0;
    current_key     = 0;
    latched_key     = 0;
    has_new_event   = false;
    shift_held      = false;
    ctrl_held       = false;
    alt_held        = false;
}

void slopos_keyboard_consume_key()
{
    current_key   = 0;
    latched_key   = 0;
    has_new_event = false;
}
