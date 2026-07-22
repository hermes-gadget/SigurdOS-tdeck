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

#include "tdeck_pins.h"
#include <cstddef>
#include <cstdint>

static constexpr size_t SIGURDOS_TOUCH_GT911_MAX_POINTS = 5;
static constexpr size_t SIGURDOS_TOUCH_GT911_POINT_SIZE = 8;
static constexpr uint8_t SIGURDOS_TOUCH_IDLE_THRESHOLD = 5;

inline uint8_t sigurdos_touch_idle_increment(uint8_t count)
{
    return count < SIGURDOS_TOUCH_IDLE_THRESHOLD ? (uint8_t)(count + 1) : count;
}

inline bool sigurdos_touch_raw_point_valid(uint16_t raw_x, uint16_t raw_y)
{
    if (raw_x == 0xFFFFu || raw_y == 0xFFFFu) return false;
    if (raw_x == 0 && raw_y == 0) return false;
    if (raw_x >= (uint16_t)TOUCH_SENSOR_X || raw_y >= (uint16_t)TOUCH_SENSOR_Y) return false;
    return true;
}

inline bool sigurdos_touch_parse_point_raw(const uint8_t* points,
                                           size_t points_len,
                                           size_t point_index,
                                           uint16_t* out_x,
                                           uint16_t* out_y)
{
    if (!points || !out_x || !out_y) return false;
    if (point_index >= SIGURDOS_TOUCH_GT911_MAX_POINTS) return false;

    const size_t offset = point_index * SIGURDOS_TOUCH_GT911_POINT_SIZE;
    if (points_len < offset + SIGURDOS_TOUCH_GT911_POINT_SIZE) return false;

    const uint8_t* p = points + offset;
    const uint16_t raw_x = p[1] | ((uint16_t)p[2] << 8);
    const uint16_t raw_y = p[3] | ((uint16_t)p[4] << 8);
    if (!sigurdos_touch_raw_point_valid(raw_x, raw_y)) return false;

    *out_x = raw_x;
    *out_y = raw_y;
    return true;
}

// Initialize the GT911 touch controller over I2C
// Must be called after Wire.begin() and before LVGL init
// Returns true on successful initialization
bool sigurdos_touch_init();

// Reset one-shot initialization state for native test isolation.
// Production code never calls this.
void sigurdos_touch_reset_init_for_test();

// Call this each frame to poll for new touch data
// (called from sigurdos_display_loop)
void sigurdos_touch_loop();

// Get the current touch state
// Returns true if a touch is active, and fills x/y with position
// Coordinates are already mapped to display space (0-319, 0-239)
bool sigurdos_touch_get(int* out_x, int* out_y, bool* out_pressed);

// Is the touch controller initialized and responding?
bool sigurdos_touch_ready();

struct SigurdOSTouchDiag {
    bool initialized;
    bool init_attempted;
    bool pressed;
    bool edge_release_pending;
    uint8_t i2c_addr;
    int x;
    int y;
    int consecutive_i2c_errors;
    uint32_t press_count;
    uint32_t release_count;
    uint32_t move_count;
    uint32_t last_event_ms;
    uint32_t reinit_count;   // successful touch re-inits (RELI-001)
};

// Snapshot touch diagnostic state without changing LVGL input state.
bool sigurdos_touch_get_diag(SigurdOSTouchDiag* out);
