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


#include "touch.h"
#include "tdeck_pins.h"
#include <Wire.h>
#include <Arduino.h>

// ════════════════════════════════════════════════════════
// GT911 Register Constants
// ════════════════════════════════════════════════════════
static constexpr uint8_t  GT911_ADDR1          = 0x5D;   // primary I2C addr
static constexpr uint8_t  GT911_ADDR2          = 0x14;   // alternate
static constexpr uint16_t GT911_REG_CONFIG     = 0x8047;
static constexpr uint16_t GT911_REG_STATUS     = 0x814E;
static constexpr int      GT911_MAX_POINTS     = 5;
static constexpr int      GT911_POINT_SIZE     = 8;
static constexpr uint32_t GT911_INIT_RETRY_MS  = 50;
static constexpr uint32_t GT911_POLL_INTERVAL  = 10;     // ms between full scans

// ── State ────────────────────────────────────────────────
static uint8_t  i2c_addr       = GT911_ADDR1;
static bool     initialized    = false;
static int      last_x         = -1;
static int      last_y         = -1;
static bool     pressed        = false;
static uint32_t last_poll      = 0;
static bool     was_pressed     = false;   // for edge detection

// ── Helpers ───────────────────────────────────────────────
static bool i2c_write_reg(uint16_t reg, uint8_t val)
{
    Wire.beginTransmission(i2c_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool i2c_write_bytes(uint16_t reg, const uint8_t* data, size_t len)
{
    Wire.beginTransmission(i2c_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(data, len);
    return Wire.endTransmission() == 0;
}

static bool i2c_read_bytes(uint16_t reg, uint8_t* out, size_t len)
{
    Wire.beginTransmission(i2c_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;

    Wire.requestFrom(i2c_addr, len);
    if (Wire.available() < (int)len) return false;

    for (size_t i = 0; i < len; i++) {
        out[i] = Wire.read();
    }
    return true;
}

// ── Probe I2C bus ─────────────────────────────────────────
static bool probe_i2c(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// ── Reset GT911 via INT pin ───────────────────────────────
static void gt911_reset()
{
    pinMode(PIN_TOUCH_INT, OUTPUT);
    digitalWrite(PIN_TOUCH_INT, LOW);
    delay(1);
    digitalWrite(PIN_TOUCH_INT, HIGH);
    delay(10);
    pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
}

// ════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════

bool slopos_touch_init()
{
    if (initialized) return true;

    // I2C bus is already initialized by TDeckBoard::begin()
    // with correct pins and 400kHz clock
    Wire.setClock(400000);  // GT911 supports 400 kHz

    // Configure INT pin
    pinMode(PIN_TOUCH_INT, INPUT_PULLUP);

    // Try to reset the controller
    gt911_reset();
    delay(50);

    // Probe both addresses
    if (probe_i2c(GT911_ADDR1)) {
        i2c_addr = GT911_ADDR1;
    } else if (probe_i2c(GT911_ADDR2)) {
        i2c_addr = GT911_ADDR2;
    } else {
        // GT911 not found on I2C bus
        return false;
    }

    // Read existing config from chip (186 bytes).
    // Chunk into 32-byte pieces to avoid Arduino Wire buffer overflow.
    static constexpr size_t CFG_SZ = 186;
    static constexpr size_t CHUNK_SZ = 32;
    uint8_t config[CFG_SZ];
    bool config_ok = true;
    for (size_t offset = 0; offset < CFG_SZ; offset += CHUNK_SZ) {
        size_t chunk = (offset + CHUNK_SZ <= CFG_SZ) ? CHUNK_SZ : (CFG_SZ - offset);
        if (!i2c_read_bytes(GT911_REG_CONFIG + offset, config + offset, chunk)) {
            config_ok = false;
            break;
        }
    }
    if (config_ok) {
        // Write back in chunks
        for (size_t offset = 0; offset < CFG_SZ; offset += CHUNK_SZ) {
            size_t chunk = (offset + CHUNK_SZ <= CFG_SZ) ? CHUNK_SZ : (CFG_SZ - offset);
            i2c_write_bytes(GT911_REG_CONFIG + offset, config + offset, chunk);
        }
    }

    // Clear any stale touch data
    i2c_write_reg(GT911_REG_STATUS, 0);

    initialized = true;
    last_x = -1;
    last_y = -1;
    pressed = false;
    was_pressed = false;

    return true;
}

void slopos_touch_loop()
{
    if (!initialized) return;

    uint32_t now = millis();
    if (now - last_poll < GT911_POLL_INTERVAL) return;
    last_poll = now;

    // Ensure I2C clock is 400kHz for GT911 touch controller
    // (keyboard scan may have set it to 100kHz)
    Wire.setClock(400000);

    // Check INT pin — GT911 pulls it LOW when new data is ready.
    // When HIGH, there may be no new data, but the GT911 can buffer
    // multiple touch samples. Read the status register to confirm
    // before deciding to release — otherwise rapid taps that arrive
    // between poll cycles are silently dropped.
    if (digitalRead(PIN_TOUCH_INT) == HIGH) {
        // Read status register to confirm no buffered data
        uint8_t status = 0;
        if (i2c_read_bytes(GT911_REG_STATUS, &status, 1) && (status & 0x80)) {
            // Buffered data waiting — fall through to process it
            // (INT de-asserted between taps but GT911 still has data)
        } else {
            // Genuinely no new data — maintain existing state
            if (pressed) {
                pressed = false;
                was_pressed = true;
            }
            return;
        }
    }

    // Read status register
    uint8_t status = 0;
    if (!i2c_read_bytes(GT911_REG_STATUS, &status, 1)) {
        return;
    }

    // Bit 7 = buffer status (1 = ready, 0 = no data)
    if (!(status & 0x80)) return;

    int num_points = status & 0x0F;
    if (num_points == 0 || num_points > GT911_MAX_POINTS) {
        // Buffer ready but no points — clear and treat as release
        i2c_write_reg(GT911_REG_STATUS, 0);
        if (pressed) {
            pressed = false;
            was_pressed = true;
        }
        return;
    }

    // Read touch point data (all 5 points, 8 bytes each = 40 bytes)
    static uint8_t point_data[GT911_MAX_POINTS * GT911_POINT_SIZE];  // static avoids repeated stack alloc
    if (!i2c_read_bytes(GT911_REG_STATUS + 1, point_data, sizeof(point_data))) {
        return;
    }

    // Parse first valid touch point (we're single-touch)
    int found_x = -1, found_y = -1;
    for (int i = 0; i < num_points; i++) {
        const uint8_t* p = point_data + (i * GT911_POINT_SIZE);
        uint16_t raw_x = p[1] | ((uint16_t)p[2] << 8);
        uint16_t raw_y = p[3] | ((uint16_t)p[4] << 8);

        // Skip invalid points
        if (raw_x == 0xFFFF || raw_y == 0xFFFF) continue;
        if (raw_x == 0 && raw_y == 0) continue;

        // Bounds check against GT911 native (pre-swap) portrait resolution
        if (raw_x >= (uint16_t)TOUCH_SENSOR_X || raw_y >= (uint16_t)TOUCH_SENSOR_Y) continue;

        // ── Coordinate transformation ──────────────────
        int sx = raw_x, sy = raw_y;

        // Swap XY (T-Deck touch panel is rotated relative to display)
        if (TOUCH_SWAP_XY) {
            int tmp = sx; sx = sy; sy = tmp;
        }

        // Scale to display dimensions
        sx = (sx * TFT_WIDTH)  / TOUCH_MAX_X;
        sy = (sy * TFT_HEIGHT) / TOUCH_MAX_Y;

        // Mirror if configured
        if (TOUCH_MIRROR_X) sx = TFT_WIDTH  - 1 - sx;
        if (TOUCH_MIRROR_Y) sy = TFT_HEIGHT - 1 - sy;

        // Clamp to display bounds
        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        if (sx >= TFT_WIDTH)  sx = TFT_WIDTH  - 1;
        if (sy >= TFT_HEIGHT) sy = TFT_HEIGHT - 1;

        found_x = sx;
        found_y = sy;
        break;
    }

    // Clear status register to acknowledge (GT911 won't update until cleared)
    i2c_write_reg(GT911_REG_STATUS, 0);

    if (found_x >= 0 && found_y >= 0) {
        // Touch detected
        last_x = found_x;
        last_y = found_y;
        pressed = true;
        was_pressed = false;
    } else {
        // Buffer ready but no valid point — release
        if (pressed) {
            pressed = false;
            was_pressed = true;
        }
    }
}

bool slopos_touch_get(int* out_x, int* out_y, bool* out_pressed)
{
    if (!initialized) {
        if (out_x) *out_x = 0;
        if (out_y) *out_y = 0;
        if (out_pressed) *out_pressed = false;
        return false;
    }

    if (out_x) *out_x = (last_x >= 0) ? last_x : 0;
    if (out_y) *out_y = (last_y >= 0) ? last_y : 0;
    if (out_pressed) *out_pressed = pressed;
    return initialized;
}

bool slopos_touch_ready()
{
    return initialized;
}
