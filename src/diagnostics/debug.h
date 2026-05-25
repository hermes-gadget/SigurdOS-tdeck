#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Comprehensive device-wide debug diagnostics for SlopOS-TDeck.
// Enabled by building with -D SLOPOS_DEBUG=1 (see platformio.ini debug env).
//
// Debug levels (SLOPOS_DEBUG_LEVEL):
//   1 = Quiet   — test controller output only, no periodic stats/flushes/pins
//   2 = Normal  — periodic [stat] + [pins] every 5s, [flush] on each frame
//   3 = Verbose — all of level 2 plus on-demand heavy dumps
//
// Runtime level can be changed via test controller: debug <1|2|3>

#include <cstdint>

#ifndef SLOPOS_DEBUG_LEVEL
#define SLOPOS_DEBUG_LEVEL 2
#endif

namespace slopos {
namespace debug {

void init();
void loop();

// Runtime debug level control
void set_level(uint8_t level);
uint8_t get_level();

void dump_system();
void dump_lvgl_rendering();
void dump_trackball_state();
void dump_home_screen_layout();
void dump_memory();
void dump_display_config();
void dump_mesh_state();

} // namespace debug
} // namespace slopos
