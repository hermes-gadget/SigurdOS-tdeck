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
#include "i2c_bus.h"
#include "tdeck_pins.h"
#include "prefs.h"
#include <Arduino.h>
#include <Wire.h>
#include <cstring>
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
//   Key mode: Wire.requestFrom(0x55, 1) → pre-decoded ASCII byte
//   Raw mode: Wire.requestFrom(0x55, 5) → one bitmask per column
//
// Key mode is the primary path. It lets the keyboard MCU own the physical
// matrix mapping, which is required for T-Deck variants with different
// matrices. A short raw sample accompanies each ASCII byte and also runs every
// 20 ms for modifier-only taps. This recovers the host-side Alt/Mic/Sym features
// that the MCU's ASCII protocol cannot express without raw-decoding normal keys.
//
// Keymap (col × row, 5×7 matrix):
//   Col0: q w sym a ALT SPC Mic
//   Col1: e s d   p x   z   LShift
//   Col2: r g t   RShift v c f
//   Col3: u h y   Enter  b n j
//   Col4: o l i   Bksp   $ m k
// ════════════════════════════════════════════════════════

static constexpr uint8_t  KB_I2C_ADDR              = sigurdos::i2c::KEYBOARD_ADDR;
static constexpr uint8_t  CMD_BRIGHTNESS            = 0x01;
static constexpr uint8_t  CMD_DEFAULT_BRIGHTNESS    = 0x02;
static constexpr uint8_t  CMD_MODE_RAW              = 0x03;  // raw bitmask mode
static constexpr uint8_t  CMD_MODE_KEY              = 0x04;  // ASCII key mode
static constexpr uint32_t KB_POLL_INTERVAL_MS       = 5;    // faster polling for responsive typing (was 10)
static constexpr uint32_t KB_RAW_SAMPLE_INTERVAL_MS = 20;
static constexpr uint8_t  KB_RAW_COLS               = 5;
static constexpr uint8_t  KB_RAW_ROWS               = 7;
static constexpr uint8_t  KB_RAW_UNSUPPORTED_LIMIT  = 3;

enum class RawKeyKind : uint8_t {
    Printable,
    Sym,
    Alt,
    Mic,
    Shift,
    Enter,
    Backspace,
};

struct RawKeyDef {
    uint32_t normal;
    uint32_t sym;
    uint32_t sym_shift;
    RawKeyKind kind;
};

static constexpr RawKeyDef RAW_KEYS[KB_RAW_COLS][KB_RAW_ROWS] = {
    {
        {'q', '#', '#', RawKeyKind::Printable},
        {'w', '1', '!', RawKeyKind::Printable},
        {0, 0, 0, RawKeyKind::Sym},
        {'a', '*', '*', RawKeyKind::Printable},
        {0, 0, 0, RawKeyKind::Alt},
        {' ', 0, 0, RawKeyKind::Printable},
        {0, '0', ')', RawKeyKind::Mic},
    },
    {
        {'e', '2', '@', RawKeyKind::Printable},
        {'s', '4', '$', RawKeyKind::Printable},
        {'d', '5', '%', RawKeyKind::Printable},
        {'p', '@', '@', RawKeyKind::Printable},
        {'x', '8', '*', RawKeyKind::Printable},
        {'z', '7', '&', RawKeyKind::Printable},
        {0, 0, 0, RawKeyKind::Shift},
    },
    {
        {'r', '3', '#', RawKeyKind::Printable},
        {'g', '/', '\\', RawKeyKind::Printable},
        {'t', '(', '[', RawKeyKind::Printable},
        {0, 0, 0, RawKeyKind::Shift},
        {'v', '?', '{', RawKeyKind::Printable},
        {'c', '9', '(', RawKeyKind::Printable},
        {'f', '6', '^', RawKeyKind::Printable},
    },
    {
        {'u', '_', '~', RawKeyKind::Printable},
        {'h', ':', '`', RawKeyKind::Printable},
        {'y', ')', ']', RawKeyKind::Printable},
        {0x0D, 0, 0, RawKeyKind::Enter},
        {'b', '!', '|', RawKeyKind::Printable},
        {'n', ',', '<', RawKeyKind::Printable},
        {'j', ';', '}', RawKeyKind::Printable},
    },
    {
        {'o', '+', '=', RawKeyKind::Printable},
        {'l', '"', '"', RawKeyKind::Printable},
        {'i', '-', '_', RawKeyKind::Printable},
        {0x08, 0, 0, RawKeyKind::Backspace},
        {'$', 0, 0, RawKeyKind::Printable},
        {'m', '.', '>', RawKeyKind::Printable},
        {'k', '\'', '?', RawKeyKind::Printable},
    },
};

enum class RawSamplerSupport : uint8_t {
    Unknown,
    Supported,
    Unavailable,
};

static bool     initialized     = false;
static bool     init_attempted  = false;

void sigurdos_keyboard_reset_init_for_test()
{
    initialized = false;
    init_attempted = false;
}

static uint32_t last_poll_ms    = 0;
static uint32_t last_raw_sample_ms = 0;
static bool     shift_held      = false;
static bool     ctrl_held       = false;
static bool     alt_held        = false;
static bool     key_mode_ready  = true;
static bool     sym_sample_down = false;
static bool     alt_sample_down = false;
static bool     mic_sample_down = false;
static bool     shift_sample_down = false;
static bool     sym_one_shot    = false;
static bool     alt_one_shot    = false;
static bool     mic_one_shot    = false;
static bool     sym_combo_used  = false;
static bool     alt_combo_used  = false;
static bool     mic_combo_used  = false;
static bool     pending_key_valid = false;
static uint8_t  pending_key     = 0;
static RawSamplerSupport raw_sampler_support = RawSamplerSupport::Unknown;
static uint8_t  raw_single_byte_samples = 0;

// ── Ring buffer for key events ─────────────────────────────
// Fixes single-slot latch that dropped fast key presses:
// sigurdos_keyboard_scan() now pushes every valid key into the buffer,
// and the LVGL indev callback dequeues one key at a time.
static constexpr int KEY_BUF_SIZE = 16;
static uint32_t key_buf[KEY_BUF_SIZE] = {0};
static int      key_head  = 0;   // next write position
static int      key_tail  = 0;   // next read position
static int      key_count = 0;   // number of entries in buffer
static uint32_t last_consumed_key = 0; // key returned by most recent consume_event()
static uint32_t key_overwrites = 0;    // count of keys silently overwritten when buffer full
static uint8_t  diag_last_key_mode_byte = 0;
static uint8_t  diag_raw_matrix[KB_RAW_COLS] = {0};
static bool     diag_raw_valid = false;
static uint32_t diag_last_output_codepoint = 0;
static uint32_t diag_event_count = 0;
static uint32_t diag_last_event_ms = 0;

static bool raw_key_down(const uint8_t matrix[KB_RAW_COLS], uint8_t col, uint8_t row)
{
    if (col >= KB_RAW_COLS || row >= KB_RAW_ROWS) return false;
    return (matrix[col] & (uint8_t)(1u << row)) != 0;
}

static uint32_t extended_codepoint(uint32_t base, bool shift)
{
    switch (base) {
    case 'a': return shift ? 0x00C4 : 0x00E4; // A/a diaeresis
    case 'q': return shift ? 0x00C0 : 0x00E0; // A/a grave
    case 'w': return shift ? 0x00C1 : 0x00E1; // A/a acute
    case 'x': return shift ? 0x00C2 : 0x00E2; // A/a circumflex
    case 'e': return shift ? 0x00C9 : 0x00E9; // E/e acute
    case 'r': return shift ? 0x00C8 : 0x00E8; // E/e grave
    case 't': return shift ? 0x00CA : 0x00EA; // E/e circumflex
    case 'd': return shift ? 0x00CB : 0x00EB; // E/e diaeresis
    case 'i': return shift ? 0x00CE : 0x00EE; // I/i circumflex
    case 'o': return shift ? 0x00D6 : 0x00F6; // O/o diaeresis
    case 'u': return shift ? 0x00DC : 0x00FC; // U/u diaeresis
    case 'c': return shift ? 0x00C7 : 0x00E7; // C/c cedilla
    case 'n': return shift ? 0x00D1 : 0x00F1; // N/n tilde
    case 's': return 0x00DF;                  // sharp s
    case 'l': return shift ? 0x00D8 : 0x00F8; // O/o stroke
    case 'y': return shift ? 0x00C6 : 0x00E6; // AE/ae
    case 'h': return shift ? 0x00C5 : 0x00E5; // A/a ring
    case 'j': return shift ? 0x0152 : 0x0153; // OE/oe
    case '$': return 0x20AC;                  // euro sign
    default:  return 0;
    }
}

static void enqueue_key(uint32_t key_code)
{
    if (key_code == 0 || key_code == 0xFF) {
        return;
    }

#if defined(SIGURDOS_DEBUG)
    {
        char c = (key_code >= 0x20 && key_code < 0x7F) ? (char)key_code : '.';
        Serial.printf("[kbd] key: U+%04lX (%lu) '%c'\n",
                      (unsigned long)key_code,
                      (unsigned long)key_code,
                      c);
    }
#endif

    key_buf[key_head] = key_code;
    key_head = (key_head + 1) % KEY_BUF_SIZE;
    if (key_count < KEY_BUF_SIZE) {
        key_count++;
    } else {
        key_tail = (key_tail + 1) % KEY_BUF_SIZE;
        key_overwrites++;
    }
    diag_last_output_codepoint = key_code;
    diag_event_count++;
    diag_last_event_ms = millis();

#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_key_event(
        (uint8_t)(key_code <= 0xFF ? key_code : 0x1A));
#endif

    if (key_code >= 'A' && key_code <= 'Z') {
        shift_held = true;
    } else if (key_code >= 'a' && key_code <= 'z') {
        shift_held = false;
    }
}

static bool set_keyboard_mode(uint8_t command)
{
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(command);
    return Wire.endTransmission() == 0;
}

static const RawKeyDef* active_raw_key(const uint8_t matrix[KB_RAW_COLS])
{
    for (uint8_t row = 0; row < KB_RAW_ROWS; row++) {
        for (uint8_t col = 0; col < KB_RAW_COLS; col++) {
            if (!raw_key_down(matrix, col, row)) continue;
            const RawKeyDef& key = RAW_KEYS[col][row];
            if (key.kind == RawKeyKind::Printable || key.kind == RawKeyKind::Mic) {
                return &key;
            }
        }
    }
    return nullptr;
}

static uint32_t one_shot_symbol(uint8_t key_code, bool shifted)
{
    uint32_t base = key_code;
    if (base >= 'A' && base <= 'Z') base += ('a' - 'A');

    for (uint8_t row = 0; row < KB_RAW_ROWS; row++) {
        for (uint8_t col = 0; col < KB_RAW_COLS; col++) {
            const RawKeyDef& key = RAW_KEYS[col][row];
            if (key.normal != base) continue;
            const uint32_t mapped = shifted && key.sym_shift ? key.sym_shift : key.sym;
            return mapped ? mapped : key_code;
        }
    }
    return key_code;
}

static void update_modifier_sample(const uint8_t matrix[KB_RAW_COLS])
{
    const bool sym_down = raw_key_down(matrix, 0, 2);
    const bool alt_down = raw_key_down(matrix, 0, 4);
    const bool mic_down = raw_key_down(matrix, 0, 6);
    const bool shift_down = raw_key_down(matrix, 1, 6) || raw_key_down(matrix, 2, 3);

    if (sym_down && !sym_sample_down) sym_combo_used = false;
    if (alt_down && !alt_sample_down) alt_combo_used = false;
    if (mic_down && !mic_sample_down) mic_combo_used = false;

    // Alt+B is consumed entirely by the C3 as a backlight toggle, so no ASCII
    // byte arrives for resolve_pending_key() to mark the chord as used.
    if (alt_down && raw_key_down(matrix, 3, 4)) alt_combo_used = true;

    if (!sym_down && sym_sample_down && !sym_combo_used) sym_one_shot = true;
    if (!alt_down && alt_sample_down && !alt_combo_used) alt_one_shot = true;
    if (!mic_down && mic_sample_down && !mic_combo_used) mic_one_shot = true;

    sym_sample_down = sym_down;
    alt_sample_down = alt_down;
    mic_sample_down = mic_down;
    shift_sample_down = shift_down;
    shift_held = shift_down;
    alt_held = alt_down || alt_one_shot || mic_down || mic_one_shot;
}

static void resolve_pending_key(const uint8_t* matrix)
{
    if (!pending_key_valid) return;

    const uint8_t key_code = pending_key;
    const bool shifted = shift_sample_down || (key_code >= 'A' && key_code <= 'Z');
    const bool alt_layer = alt_sample_down || alt_one_shot;
    const bool mic_layer = mic_sample_down || mic_one_shot;
    uint32_t out = key_code;

    if (alt_layer) {
        if (key_code == 0x0C) {
            out = sigurdos_keyboard_char_picker_key(shifted ? 'C' : 'c');
        } else if (key_code == ' ') {
            out = 0x0C; // Keep the matrix-mode Alt+Space channel shortcut.
        } else if (key_code >= 0x20 && key_code <= 0x7E) {
            out = sigurdos_keyboard_char_picker_key(key_code);
        }
    } else if (mic_layer) {
        const RawKeyDef* raw_key = matrix ? active_raw_key(matrix) : nullptr;
        if (sym_sample_down && raw_key && raw_key->kind == RawKeyKind::Mic) {
            // Mic is also the physical Sym+0 key. Preserve that key before
            // treating Mic as the host-side accented-character modifier.
            out = shifted && raw_key->sym_shift ? raw_key->sym_shift : raw_key->sym;
        } else {
            uint32_t base = key_code;
            if (base >= 'A' && base <= 'Z') base += ('a' - 'A');
            const uint32_t extended = extended_codepoint(base, shifted);
            if (extended) out = extended;
        }
    } else if (sym_sample_down) {
        // The C3 handles the ordinary Sym layer. Its published firmware applies
        // an ASCII "-32" operation to Shift+Sym, so reconstruct that legacy
        // matrix feature only when a raw sample identifies the physical key.
        if (shifted && matrix) {
            const RawKeyDef* raw_key = active_raw_key(matrix);
            if (raw_key && raw_key->sym_shift) out = raw_key->sym_shift;
        }
    } else if (sym_one_shot) {
        out = one_shot_symbol(key_code, shifted);
    }

    if (sym_sample_down) sym_combo_used = true;
    if (alt_sample_down) alt_combo_used = true;
    if (mic_sample_down) mic_combo_used = true;

    // A one-shot modifier is consumed by the next key even when the key itself
    // has no transformed representation, matching the previous raw driver.
    sym_one_shot = false;
    alt_one_shot = false;
    mic_one_shot = false;
    alt_held = alt_sample_down || mic_sample_down;
    pending_key_valid = false;
    pending_key = 0;
    enqueue_key(out);
}

static void hold_keymode_byte(int key_value)
{
    if (key_value <= 0 || key_value == 0xFF) return;

    // The C3 has a single-byte latch, so a second byte here is already a newer
    // physical event. Preserve ordering rather than silently replacing it.
    if (pending_key_valid) resolve_pending_key(nullptr);
    pending_key = (uint8_t)key_value;
    pending_key_valid = true;

    if (raw_sampler_support == RawSamplerSupport::Unavailable) {
        resolve_pending_key(nullptr);
    }
}

static bool poll_key_mode()
{
    if (!key_mode_ready) {
        key_mode_ready = set_keyboard_mode(CMD_MODE_KEY);
        if (!key_mode_ready) return false;
    }

    const uint8_t received = Wire.requestFrom(KB_I2C_ADDR, (uint8_t)1);
    if (received != 1 || Wire.available() == 0) {
#if defined(SIGURDOS_DEBUG)
        Serial.printf("[kbd] key-mode read: %u/1 bytes\n", received);
#endif
        return false;
    }
    const int key_value = Wire.read();
    if (key_value > 0 && key_value != 0xFF) {
        diag_last_key_mode_byte = (uint8_t)key_value;
    }

#if defined(SIGURDOS_DEBUG)
    if (key_value > 0 && key_value != 0xFF) {
        const char c = (key_value >= 0x20 && key_value < 0x7F) ? (char)key_value : '.';
        Serial.printf("[kbd] key-mode byte: 0x%02X '%c'\n", key_value & 0xFF, c);
    }
#endif

    hold_keymode_byte(key_value);
    return key_value > 0 && key_value != 0xFF;
}

static void sample_raw_modifiers()
{
    if (!set_keyboard_mode(CMD_MODE_RAW)) {
        key_mode_ready = set_keyboard_mode(CMD_MODE_KEY);
        resolve_pending_key(nullptr);
        return;
    }

    uint8_t matrix[KB_RAW_COLS] = {0};
    const uint8_t requested = Wire.requestFrom(KB_I2C_ADDR, (uint8_t)KB_RAW_COLS);
    if (requested != KB_RAW_COLS) {
#if defined(SIGURDOS_DEBUG)
        Serial.printf("[kbd] raw-mode read: %u/%u bytes\n", requested, KB_RAW_COLS);
#endif
    }
    uint8_t got = 0;
    while (Wire.available() > 0 && got < KB_RAW_COLS) {
        const int value = Wire.read();
        if (value < 0) break;
        matrix[got++] = (uint8_t)value;
    }

    // Always restore key mode before interpreting the sample. If this write
    // fails, the next regular poll retries it before reading a byte.
    key_mode_ready = set_keyboard_mode(CMD_MODE_KEY);

    if (got == KB_RAW_COLS) {
#if defined(SIGURDOS_DEBUG)
        if (raw_sampler_support != RawSamplerSupport::Supported) {
            Serial.println("[kbd] raw modifier sampling active");
        }
#endif
        raw_sampler_support = RawSamplerSupport::Supported;
        raw_single_byte_samples = 0;
        memcpy(diag_raw_matrix, matrix, sizeof(diag_raw_matrix));
        diag_raw_valid = true;
        update_modifier_sample(matrix);
        resolve_pending_key(matrix);
        return;
    }

    // Older C3 firmware ignores CMD_MODE_RAW and still returns one ASCII byte.
    // Preserve that byte, then stop mode sampling after the behavior repeats.
    if (got == 1) {
        hold_keymode_byte(matrix[0]);
        if (raw_sampler_support == RawSamplerSupport::Unknown &&
            ++raw_single_byte_samples >= KB_RAW_UNSUPPORTED_LIMIT) {
            raw_sampler_support = RawSamplerSupport::Unavailable;
#if defined(SIGURDOS_DEBUG)
            Serial.println("[kbd] raw modifier sampling unavailable; using key mode only");
#endif
        }
    }
    resolve_pending_key(nullptr);
}

// ════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════

bool sigurdos_keyboard_init()
{
    if (initialized) return true;
    if (init_attempted) return false;
    init_attempted = true;

    // TDeckBoard::begin() normally applies this once. Reassert it here so the
    // driver contract remains safe in isolation and after Launcher handoff.
    sigurdos::i2c::configure_runtime();

    // Warm-handoff probe: after Launcher's ESP.restart(), the C3 keyboard
    // MCU may be slow to respond or in an unexpected mode. The 8-attempt
    // window gives a cold C3 at least 700 ms to boot while every individual
    // I2C operation remains bounded to 20 ms.
    constexpr int WARM_KBD_RETRIES = 8;
    constexpr int WARM_KBD_RETRY_DELAY_MS = 100;

    bool probe_ok = false;
    for (int retry = 0; retry < WARM_KBD_RETRIES; retry++) {
        if (retry > 0) delay(WARM_KBD_RETRY_DELAY_MS);

        if (!sigurdos::i2c::probe_target(KB_I2C_ADDR)) continue;

        // Push C3 into key mode (the default after C3 cold boot) to
        // establish a known state regardless of what Launcher left behind.
        Wire.beginTransmission(KB_I2C_ADDR);
        Wire.write(CMD_MODE_KEY);
        Wire.endTransmission();  // ignore NACK — C3 may not be ready yet

        Wire.requestFrom(KB_I2C_ADDR, (uint8_t)1);
        if (Wire.available() > 0 && Wire.read() >= 0) {
            probe_ok = true;
            break;
        }
    }

    if (!probe_ok) {
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

    // Key mode is the permanent primary path. Raw mode is entered only for a
    // bounded modifier sample and is restored immediately by scan().
    if (!set_keyboard_mode(CMD_MODE_KEY)) {
        initialized = false;
        return false;
    }

    key_mode_ready = true;
    raw_sampler_support = RawSamplerSupport::Unknown;
    raw_single_byte_samples = 0;
    last_raw_sample_ms = millis();
    initialized = true;
#if defined(SIGURDOS_DEBUG)
    Serial.println("[kbd] initialized in key mode");
#endif
    return true;
}

void sigurdos_keyboard_scan()
{
    if (!initialized) return;

    uint32_t now = millis();
    if (now - last_poll_ms < KB_POLL_INTERVAL_MS) return;
    last_poll_ms = now;

    // I2C clock is set once in TDeckBoard::begin().
    if (raw_sampler_support != RawSamplerSupport::Unavailable &&
        now - last_raw_sample_ms >= KB_RAW_SAMPLE_INTERVAL_MS) {
        last_raw_sample_ms = now;
        sample_raw_modifiers();
        return;
    }

    if (poll_key_mode() && raw_sampler_support != RawSamplerSupport::Unavailable) {
        // Correlate the ASCII byte with the matrix state while the physical
        // chord is still held. Periodic samples remain necessary for a tapped
        // modifier that produces no ASCII byte of its own.
        last_raw_sample_ms = now;
        sample_raw_modifiers();
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
    last_raw_sample_ms = 0;
    key_head  = 0;
    key_tail  = 0;
    key_count = 0;
    last_consumed_key = 0;
    shift_held      = false;
    ctrl_held       = false;
    alt_held        = false;
    key_mode_ready  = true;
    sym_sample_down = false;
    alt_sample_down = false;
    mic_sample_down = false;
    shift_sample_down = false;
    sym_one_shot    = false;
    alt_one_shot    = false;
    mic_one_shot    = false;
    sym_combo_used  = false;
    alt_combo_used  = false;
    mic_combo_used  = false;
    pending_key_valid = false;
    pending_key = 0;
    raw_sampler_support = RawSamplerSupport::Unknown;
    raw_single_byte_samples = 0;
    diag_last_key_mode_byte = 0;
    memset(diag_raw_matrix, 0, sizeof(diag_raw_matrix));
    diag_raw_valid = false;
    diag_last_output_codepoint = 0;
    diag_event_count = 0;
    diag_last_event_ms = 0;
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
    enqueue_key(key_code);
}

void sigurdos_keyboard_inject_codepoint(uint32_t key_code)
{
    enqueue_key(key_code);
}

uint32_t sigurdos_keyboard_overwrite_count()
{
    return key_overwrites;
}

bool sigurdos_keyboard_get_diag(SigurdOSKeyboardDiag* out)
{
    if (!out) return false;
    out->initialized = initialized;
    out->raw_supported = raw_sampler_support == RawSamplerSupport::Supported;
    out->raw_unavailable = raw_sampler_support == RawSamplerSupport::Unavailable;
    out->raw_valid = diag_raw_valid;
    out->shift = shift_held;
    out->ctrl = ctrl_held;
    out->alt = alt_held;
    out->sym_down = sym_sample_down;
    out->mic_down = mic_sample_down;
    out->layout = sigurdos::prefs_get().kbd_layout;
    out->last_key_mode_byte = diag_last_key_mode_byte;
    memcpy(out->raw_matrix, diag_raw_matrix, sizeof(out->raw_matrix));
    out->last_output_codepoint = diag_last_output_codepoint;
    out->event_count = diag_event_count;
    out->overwrite_count = key_overwrites;
    out->last_event_ms = diag_last_event_ms;
    return initialized;
}
