#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Comprehensive device-wide debug diagnostics for SlopOS-TDeck.
// Enabled by building with -D SLOPOS_DEBUG=1 (see platformio.ini debug env).
// Per-feature flags in debug_cfg.h allow independent feature debug control.
//
// Debug levels (SLOPOS_DEBUG_LEVEL):
//   1 = Quiet   — test controller output only, no periodic stats/flushes/pins
//   2 = Normal  — periodic [stat] + [pins] every 5s, [flush] on each frame
//   3 = Verbose — all of level 2 plus on-demand heavy dumps
//
// Runtime level can be changed via test controller: debug <1|2|3>
// Runtime per-feature toggles: debug display 1|0, debug mesh 1|0, etc.

#include <cstdint>

#ifndef SLOPOS_DEBUG_LEVEL
#define SLOPOS_DEBUG_LEVEL 2
#endif

#if defined(SLOPOS_DEBUG) && (SLOPOS_DEBUG)

namespace slopos {
namespace debug {

void init();
void loop();

// Runtime debug level control
void set_level(uint8_t level);
uint8_t get_level();

// Per-feature runtime toggles
void   feat_set_all_mask(bool on);
void   feat_from_mask(uint8_t mask);
uint8_t feat_to_mask();
void   feat_set_display(bool on);
bool   feat_get_display();
void   feat_set_mesh(bool on);
bool   feat_get_mesh();
void   feat_set_ui(bool on);
bool   feat_get_ui();
void   feat_set_map(bool on);
bool   feat_get_map();
void   feat_set_diag(bool on);
bool   feat_get_diag();

// On-demand dump functions (respect runtime per-feature state)
void dump_system();
void dump_lvgl_rendering();
void dump_trackball_state();
void dump_home_screen_layout();
void dump_memory();
void dump_display_config();
void dump_mesh_state();

} // namespace debug
} // namespace slopos

#else

// Empty inline stubs for non-debug builds — safe to call from any code
// without linker errors, compiled to zero instructions.
namespace slopos {
namespace debug {

inline void init() {}
inline void loop() {}
inline void set_level(uint8_t) {}
inline uint8_t get_level() { return 0; }

inline void feat_set_all_mask(bool) {}
inline void feat_from_mask(uint8_t) {}
inline uint8_t feat_to_mask() { return 0; }
inline void feat_set_display(bool) {}
inline bool feat_get_display() { return false; }
inline void feat_set_mesh(bool) {}
inline bool feat_get_mesh() { return false; }
inline void feat_set_ui(bool) {}
inline bool feat_get_ui() { return false; }
inline void feat_set_map(bool) {}
inline bool feat_get_map() { return false; }
inline void feat_set_diag(bool) {}
inline bool feat_get_diag() { return false; }

inline void dump_system() {}
inline void dump_lvgl_rendering() {}
inline void dump_trackball_state() {}
inline void dump_home_screen_layout() {}
inline void dump_memory() {}
inline void dump_display_config() {}
inline void dump_mesh_state() {}

} // namespace debug
} // namespace slopos

#endif
