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
#include "../utils/utf8_util.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

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

// "query build|state|heap|lvgl|mesh|crash|drift|hb-ring|full" — on-demand queries
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

// Push a canonical mesh RX/TX observation into the packet ring. RX entries use
// the measured RSSI; TX entries use 0 because no receive signal exists.
void push_packet_log(const char* direction, const char* sender,
                     const char* channel, const char* text, int rssi);

inline const char* packet_log_text_by_policy(const char* value) {
#if SIGURDOS_TELEMETRY_INCLUDE_MESSAGE_TEXT
    return value ? value : "";
#else
    return (value && value[0]) ? "<redacted>" : "";
#endif
}

inline const char* packet_log_field_or_empty(const char* value) {
    return value ? value : "";
}

inline size_t packet_log_copy_field(char* out, size_t out_size,
                                    const char* value) {
    if (!out || out_size == 0) return 0;
    const char* source = packet_log_field_or_empty(value);
    const size_t bytes = sigurdos::utf8_truncate_bytes(source, out_size - 1);
    memcpy(out, source, bytes);
    out[bytes] = '\0';
    return bytes;
}

inline bool is_supported_query(const char* arg) {
    if (!arg || !arg[0]) return false;
    static const char* const commands[] = {
        "build", "state", "heap", "lvgl", "mesh", "crash", "crash clear",
        "crash test", "drift", "screen", "wifi", "gps", "radio", "sd",
        "nvs", "temp", "task", "hb-ring", "pktlog", "full"
    };
    for (const char* command : commands) {
        if (strcmp(arg, command) == 0) return true;
    }
    return false;
}

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
inline void push_packet_log(const char*, const char*, const char*, const char*, int) {}

}  // namespace telemetry
}  // namespace sigurdos

#endif // !SIGURDOS_TELEMETRY

#endif // SIGURDOS_TELEMETRY_H
