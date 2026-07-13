#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Comprehensive device-wide debug diagnostics for SigurdOS.
// Enabled by building with -D SIGURDOS_DEBUG=1 (see platformio.ini debug env).
// Per-feature flags in debug_cfg.h allow independent feature debug control.
//
// Debug levels (SIGURDOS_DEBUG_LEVEL):
//   1 = Quiet   — test controller output only, no periodic stats/flushes/pins
//   2 = Normal  — periodic [stat] + [pins] every 5s, [flush] on each frame
//   3 = Verbose — all of level 2 plus on-demand heavy dumps
//
// Runtime level can be changed via test controller: debug <1|2|3>
// Runtime per-feature toggles: debug display 1|0, debug mesh 1|0, etc.

#include <cstdint>

#ifndef SIGURDOS_DEBUG_LEVEL
#define SIGURDOS_DEBUG_LEVEL 2
#endif

// ── Crash ring buffer ───────────────────────────────────
// Captures the last N debug lines in checksummed RTC no-init memory and
// auto-dumps them after a panic or watchdog reset. RTC retention is not
// guaranteed across power loss or brownout.
#ifndef SIGURDOS_CRASH_RING
#define SIGURDOS_CRASH_RING 0
#endif

#define DEBUG_RING_SIZE 48

struct DebugRingEntry {
    uint32_t timestamp_ms;
    char     line[64];
};

namespace sigurdos {
namespace debug {

static constexpr uint8_t SIGURDOS_DEBUG_MIN_LEVEL = 1;
static constexpr uint8_t SIGURDOS_DEBUG_MAX_LEVEL = 3;

inline uint8_t clamp_level(int level) {
    if (level < SIGURDOS_DEBUG_MIN_LEVEL) return SIGURDOS_DEBUG_MIN_LEVEL;
    if (level > SIGURDOS_DEBUG_MAX_LEVEL) return SIGURDOS_DEBUG_MAX_LEVEL;
    return static_cast<uint8_t>(level);
}

} // namespace debug
} // namespace sigurdos

#if defined(SIGURDOS_DEBUG) && (SIGURDOS_DEBUG)

namespace sigurdos {
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

// ── Crash ring buffer (SIGURDOS_CRASH_RING) ─────────────
void ring_log(const char* line);
void ring_dump();
void ring_clear();
bool has_crash_record();

} // namespace debug
} // namespace sigurdos

#else

// Empty inline stubs for non-debug builds — safe to call from any code
// without linker errors, compiled to zero instructions.
namespace sigurdos {
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

inline void ring_log(const char*) {}
inline void ring_dump() {}
inline void ring_clear() {}
inline bool has_crash_record() { return false; }

} // namespace debug
} // namespace sigurdos

#endif
