// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Telemetry engine — structured heartbeat, diff-based updates, crash capture,
// and agent query interface for SigurdOS T-Deck.
//
// Gated behind SIGURDOS_TELEMETRY. When 0, all functions compile to inline
// no-ops (zero DRAM cost).

#ifndef SIGURDOS_TELEMETRY_H
#define SIGURDOS_TELEMETRY_H

#include "debug_cfg.h"
#include <cstdint>

namespace sigurdos {
namespace telemetry {

// ── Initialisation / Loop ─────────────────────────────

// Call once in setup() after mesh and LVGL are ready.
void init();

// Call every loop() iteration. Non-blocking.
void loop();

// ── Agent Commands (called from test_controller) ──────

// "telemetry on|off|diff on|off|level N|hb N|full" — control telemetry
void cmd_telemetry(const char* arg);

// "query state|heap|lvgl|mesh|crash|drift|hb-ring|full" — on-demand queries
void cmd_query(const char* arg);

// "crash report|clear|test" — crash log operations
void cmd_crash(const char* arg);

// "drift" — show all drift detectors
void cmd_drift(const char* arg);

// ── State Queries ─────────────────────────────────────

bool is_enabled();
uint32_t tick_count();
uint32_t uptime_s();

// ── Phase 1 Telemetry Hooks ──────────────────────────

// Report a screen transition. Called from navigation.cpp after dispatch.
void report_screen_transition(uint8_t from, uint8_t to, uint32_t now_ms);

// Report loop timing in microseconds. Called from main.cpp after telemetry::loop().
void report_loop_timing(uint32_t elapsed_us);

// Report display wake event.
void report_display_wake();

// Report display sleep event.
void report_display_sleep();

// Report a render flush (called from LVGL flush callback).
void report_render_flush();

// Phase 2+3+4 hooks
void report_key_event(uint8_t keycode);
void report_touch_event(uint16_t x, uint16_t y);
void report_trackball_event(uint8_t direction);

// Push a mesh packet into the packet content log (ring buffer).
void push_packet_log(const char* sender, const char* channel,
                     const char* text, int rssi);

}  // namespace telemetry
}  // namespace sigurdos

// ── Inline stubs when disabled ────────────────────────
#if !SIGURDOS_TELEMETRY

namespace sigurdos {
namespace telemetry {

inline void init() {}
inline void loop() {}
inline void cmd_telemetry(const char*) {}
inline void cmd_query(const char*) {}
inline void cmd_crash(const char*) {}
inline void cmd_drift(const char*) {}
inline bool is_enabled() { return false; }
inline uint32_t tick_count() { return 0; }
inline uint32_t uptime_s() { return 0; }

// Phase 1 stubs (disabled)
inline void report_screen_transition(uint8_t, uint8_t, uint32_t) {}
inline void report_loop_timing(uint32_t) {}
inline void report_display_wake() {}
inline void report_display_sleep() {}
inline void report_render_flush() {}

// Phase 2+3+4 stubs
inline void report_key_event(uint8_t) {}
inline void report_touch_event(uint16_t, uint16_t) {}
inline void report_trackball_event(uint8_t) {}
inline void push_packet_log(const char*, const char*, const char*, int) {}

}  // namespace telemetry
}  // namespace sigurdos

#endif // !SIGURDOS_TELEMETRY

#endif // SIGURDOS_TELEMETRY_H
