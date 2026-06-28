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
//   Key mode (0x04): Wire.requestFrom(0x55, 1) → 1 byte
//     Returns pre-decoded ASCII character (0x00 = no key).
//     The C3 resolves its own key matrix including Shift/Sym layers,
//     so this works identically on ALL T-Deck variants regardless of
//     physical key layout differences (T-Deck, T-Deck Plus, etc.).
//   Raw mode (0x03): Wire.requestFrom(0x55, 5) → 5 bytes
//     One bitmask per column. Used periodically to sample modifier
//     keys (Mic, unlabeled Alt) that key mode doesn't expose.
//
// Mode priority (Phase 1 — issue #752):
//   PRIMARY:   Key mode (CMD 0x04) — 1 byte per poll, multi-model safe
//   PERIODIC:  Raw sample every ~200ms for Mic/Alt/Sym modifier keys
//   FALLBACK:  Raw mode if key mode degrades (C3 legacy firmware)
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
static constexpr uint8_t  KB_RAW_COLS               = 5;
static constexpr uint8_t  KB_RAW_ROWS               = 7;
static constexpr uint8_t  KB_RAW_ROW_MASK           = 0x7F;

enum class RawKeyKind : uint8_t {
    Printable,
    Sym,
    Alt,
    Mic,
    Shift,
    Enter,
    Backspace,
    None,
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

static bool     initialized     = false;

void sigurdos_keyboard_reset_init_for_test() { initialized = false; }

static uint32_t last_poll_ms    = 0;
static bool     shift_held      = false;
static bool     ctrl_held       = false;
static bool     alt_held        = false;
static bool     raw_mode_active = false;   // Phase 1: key mode is primary; raw is sampled periodically
static uint8_t  raw_prev[KB_RAW_COLS] = {0};
static bool     sym_one_shot    = false;
static bool     alt_one_shot    = false;
static bool     mic_one_shot    = false;
static bool     sym_combo_used  = false;
static bool     alt_combo_used  = false;
static bool     mic_combo_used  = false;
static uint8_t  keymode_poll_count = 0;      // counter for interleaved raw sampling

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

static bool raw_key_down(const uint8_t matrix[KB_RAW_COLS], uint8_t col, uint8_t row)
{
    if (col >= KB_RAW_COLS || row >= KB_RAW_ROWS) return false;
    return (matrix[col] & (uint8_t)(1u << row)) != 0;
}

static bool is_modifier_kind(RawKeyKind kind)
{
    return kind == RawKeyKind::Sym ||
           kind == RawKeyKind::Alt ||
           kind == RawKeyKind::Mic ||
           kind == RawKeyKind::Shift;
}

static uint32_t shifted_codepoint(uint32_t codepoint)
{
    if (codepoint >= 'a' && codepoint <= 'z') {
        return codepoint - ('a' - 'A');
    }
    return codepoint;
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

static void process_legacy_key(int keyValue)
{
    if (keyValue <= 0 || keyValue == 0xFF) {
        return;
    }
    enqueue_key((uint32_t)keyValue);
    if (keyValue == 0x0C) {
        alt_held = !alt_held;
    }
}

static void process_raw_matrix(const uint8_t matrix[KB_RAW_COLS])
{
    const bool sym_down = raw_key_down(matrix, 0, 2);
    const bool alt_down = raw_key_down(matrix, 0, 4);
    const bool mic_down = raw_key_down(matrix, 0, 6);
    const bool shift_down = raw_key_down(matrix, 1, 6) || raw_key_down(matrix, 2, 3);
    const bool sym_was_down = raw_key_down(raw_prev, 0, 2);
    const bool alt_was_down = raw_key_down(raw_prev, 0, 4);
    const bool mic_was_down = raw_key_down(raw_prev, 0, 6);

    if (sym_down && !sym_was_down) {
        sym_combo_used = false;
    }
    if (alt_down && !alt_was_down) {
        alt_combo_used = false;
    }
    if (mic_down && !mic_was_down) {
        mic_combo_used = false;
    }

    shift_held = shift_down;
    alt_held = alt_down || alt_one_shot || mic_down || mic_one_shot;

    for (uint8_t row = 0; row < KB_RAW_ROWS; row++) {
        for (uint8_t col = 0; col < KB_RAW_COLS; col++) {
            const bool is_down = raw_key_down(matrix, col, row);
            const bool was_down = raw_key_down(raw_prev, col, row);
            if (!is_down || was_down) {
                continue;
            }

            const RawKeyDef& key = RAW_KEYS[col][row];
            const bool sym_layer = sym_down || sym_one_shot;
            if (is_modifier_kind(key.kind) &&
                !(key.kind == RawKeyKind::Mic && sym_layer)) {
                continue;
            }

            const bool alt_layer = alt_down || alt_one_shot;
            const bool mic_layer = mic_down || mic_one_shot;
            uint32_t out = 0;

            if (key.kind == RawKeyKind::Enter || key.kind == RawKeyKind::Backspace) {
                out = key.normal;
            } else if (key.kind == RawKeyKind::Mic && sym_layer) {
                out = (shift_down && key.sym_shift) ? key.sym_shift : key.sym;
            } else if (alt_layer && key.normal == ' ') {
                out = 0x0C; // Channel-menu shortcut, leaving Alt+letter for character options.
            } else if (alt_down && key.normal == 'b') {
                out = 0;    // The keyboard MCU owns Alt+B backlight toggling.
            } else if (alt_layer) {
                out = sigurdos_keyboard_char_picker_key(
                    (uint8_t)(shift_down ? shifted_codepoint(key.normal) : key.normal));
            } else if (mic_layer) {
                out = extended_codepoint(key.normal, shift_down);
                if (out == 0) {
                    out = sym_layer
                        ? (shift_down && key.sym_shift ? key.sym_shift : key.sym)
                        : (shift_down ? shifted_codepoint(key.normal) : key.normal);
                }
            } else if (sym_layer) {
                out = (shift_down && key.sym_shift) ? key.sym_shift : key.sym;
            } else {
                out = shift_down ? shifted_codepoint(key.normal) : key.normal;
            }

            if (sym_down) sym_combo_used = true;
            if (alt_down) alt_combo_used = true;
            if (mic_down) mic_combo_used = true;
            if (sym_one_shot) sym_one_shot = false;
            if (alt_one_shot) alt_one_shot = false;
            if (mic_one_shot) mic_one_shot = false;
            enqueue_key(out);
        }
    }

    if (!sym_down && sym_was_down && !sym_combo_used) {
        sym_one_shot = true;
    }
    if (!alt_down && alt_was_down && !alt_combo_used) {
        alt_one_shot = true;
    }
    if (!mic_down && mic_was_down && !mic_combo_used) {
        mic_one_shot = true;
    }

    for (uint8_t col = 0; col < KB_RAW_COLS; col++) {
        raw_prev[col] = matrix[col] & KB_RAW_ROW_MASK;
    }
}

// ── Test helper: enter raw mode for unit tests ──────────
// Only used by test_keyboard to exercise the raw-matrix path.
void sigurdos_keyboard_enter_raw_mode_for_test()
{
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_MODE_RAW);
    Wire.endTransmission();
    raw_mode_active = true;
}

// ════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════

bool sigurdos_keyboard_init()
{
    if (initialized) return true;

    // I2C bus is already initialized by TDeckBoard::begin() at 400 kHz.
    // Do NOT call Wire.setTimeOut() here — the warm-handoff retry loop
    // below needs the default ~1 s timeout so the C3 keyboard MCU has
    // time to finish its cold boot (~500 ms). The 20 ms poll timeout
    // is applied AFTER init, once we know the C3 is responsive.

    // Warm-handoff probe: after Launcher's ESP.restart(), the C3 keyboard
    // MCU may be slow to respond or in an unexpected mode. Retry with
    // a bounded window and explicitly reset to key mode before probing.
    constexpr int WARM_KBD_RETRIES = 5;       // bumped from 3 → 5: C3 cold boot ~500 ms
    constexpr int WARM_KBD_RETRY_DELAY_MS = 100;

    bool probe_ok = false;
    for (int retry = 0; retry < WARM_KBD_RETRIES; retry++) {
        if (retry > 0) delay(WARM_KBD_RETRY_DELAY_MS);

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

    // Primary: key mode (CMD 0x04). The C3 resolves its own matrix
    // and delivers pre-decoded ASCII — correct on all T-Deck variants.
    // Raw matrix (CMD 0x03) is sampled periodically for modifier keys
    // (Mic, Alt) that key mode doesn't expose. See poll_raw_modifiers_once().
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_MODE_KEY);
    if (Wire.endTransmission() != 0) {
        initialized = false;
        return false;
    }

    raw_mode_active = false;
    initialized = true;

    // Now that the C3 is confirmed responsive, clamp the I2C timeout
    // so future poll read timeouts (key-mode byte requests) don't stall
    // the UI loop. The ESP32 Arduino default of ~1 s would block the
    // entire LVGL render for a second on a transient C3 glitch.
    Wire.setTimeOut(20);

    return true;
}

// ── Key-mode poll (primary) ──────────────────────────────
// Read 1 pre-decoded ASCII byte from the C3 keyboard. The C3 firmware
// resolves its own key matrix, so this works identically on all T-Deck
// variants regardless of physical key layout differences.
// Returns true if a valid key was read and enqueued.
static bool poll_keymode_byte()
{
    Wire.requestFrom(KB_I2C_ADDR, (uint8_t)1);
    if (Wire.available() == 0) return false;
    int keyValue = Wire.read();
    if (keyValue <= 0 || keyValue == 0xFF) return false;

#if defined(SIGURDOS_DEBUG)
    {
        static int last_logged = -1;
        if (keyValue != last_logged) {
            char c = (keyValue >= 0x20 && keyValue < 0x7F) ? (char)keyValue : '.';
            Serial.printf("[kbd] key-mode byte: 0x%02X (%d) '%c'\n",
                          keyValue & 0xFF, keyValue, c);
            last_logged = keyValue;
        }
    }
#endif

    // Track shift state from uppercase letters (the C3 resolves Shift itself)
    if (keyValue >= 'A' && keyValue <= 'Z') {
        shift_held = true;
    } else if (keyValue >= 'a' && keyValue <= 'z') {
        shift_held = false;
    }

    // Alt+C in legacy C3 key-mode firmware sends 0x0C (channel menu)
    if (keyValue == 0x0C) {
        alt_held = !alt_held;
    }

    enqueue_key((uint32_t)keyValue);
    return true;
}

// ── Periodic raw-mode modifier sampler ───────────────────
// Every RAW_SAMPLE_INTERVAL key-mode polls, briefly switch the C3 to raw
// matrix mode (CMD 0x03), read the full 5-column matrix, process any
// modifier-only key combos (Mic+letter, Alt+letter, Sym-only, $ key) that
// the C3's key-mode firmware does NOT encode, then switch back to key mode.
//
// This is the bridge that keeps SigurdOS's extended features (accented
// Latin via Mic, character picker via Alt, dedicated $ key) working while
// the PRIMARY typing path uses the simpler, multi-model-safe key mode.
static constexpr uint8_t RAW_SAMPLE_INTERVAL = 40;   // ~200ms at 5ms poll

static void poll_raw_modifiers_once()
{
    // Switch to raw mode
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_MODE_RAW);
    if (Wire.endTransmission() != 0) return;

    delayMicroseconds(800);   // let C3 stabilise in raw mode

    // Read 5-byte matrix
    uint8_t matrix[KB_RAW_COLS] = {0};
    Wire.requestFrom(KB_I2C_ADDR, (uint8_t)KB_RAW_COLS);
    uint8_t got = 0;
    while (Wire.available() > 0 && got < KB_RAW_COLS) {
        int v = Wire.read();
        if (v < 0) break;
        matrix[got++] = (uint8_t)v;
    }

    // Switch back to key mode immediately
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(CMD_MODE_KEY);
    Wire.endTransmission();   // fire-and-forget; next poll verifies

    if (got == KB_RAW_COLS) {
        // Only process raw matrix if it carries modifier keys that
        // key mode can't express: Mic+letter, Alt+letter, Sym-only,
        // or the dedicated $ key. Normal alpha/numeric keys are
        // already handled by key mode — processing them again here
        // would produce duplicate characters.
        process_raw_matrix(matrix);
    }
}

void sigurdos_keyboard_scan()
{
    if (!initialized) return;

    uint32_t now = millis();
    if (now - last_poll_ms < KB_POLL_INTERVAL_MS) return;
    last_poll_ms = now;

    // I2C clock is set once at init (200kHz compromise for shared bus)

    if (raw_mode_active) {
        // Raw-mode fallback (C3 firmware didn't support key mode, or
        // key mode degraded). Unchanged from the original driver.
        uint8_t matrix[KB_RAW_COLS] = {0};
        Wire.requestFrom(KB_I2C_ADDR, (uint8_t)KB_RAW_COLS);
        uint8_t got = 0;
        while (Wire.available() > 0 && got < KB_RAW_COLS) {
            int v = Wire.read();
            if (v < 0) break;
            matrix[got++] = (uint8_t)v;
        }

        if (got == KB_RAW_COLS) {
            process_raw_matrix(matrix);
            return;
        }

        raw_mode_active = false;
        if (got > 0) {
            process_legacy_key(matrix[0]);
        }
        return;
    }

    // ── Primary path: key mode (CMD 0x04) ─────────────
    // Read 1 ASCII byte from the C3 per poll. The C3 resolves its own
    // key matrix — correct on all T-Deck variants.
    poll_keymode_byte();

    // Periodic raw-mode sample for modifier keys the C3 doesn't encode
    // in key mode (Mic, unlabeled Alt, dedicated $).
    keymode_poll_count++;
    if (keymode_poll_count >= RAW_SAMPLE_INTERVAL) {
        keymode_poll_count = 0;
        poll_raw_modifiers_once();
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
    key_head  = 0;
    key_tail  = 0;
    key_count = 0;
    last_consumed_key = 0;
    shift_held      = false;
    ctrl_held       = false;
    alt_held        = false;
    raw_mode_active = false;   // key mode is primary\n    sym_one_shot    = false;
    alt_one_shot    = false;
    mic_one_shot    = false;
    sym_combo_used  = false;
    alt_combo_used  = false;
    mic_combo_used  = false;
    keymode_poll_count = 0;
    memset(raw_prev, 0, sizeof(raw_prev));
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
