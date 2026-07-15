// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Input event capture for structured telemetry — keyboard,
// touch, trackball events are reported as @pins records.
//
// Gated behind SIGURDOS_TELEMETRY — all functions become
// inline no-ops when disabled.

#ifndef SIGURDOS_TELEMETRY_INPUT_H
#define SIGURDOS_TELEMETRY_INPUT_H

#include <cstdint>

namespace sigurdos {
namespace telemetry {
namespace input {

// ── Input event reporters ──────────────────────────────

// Report a keyboard key event (keycode is the ASCII value).
void report_key_event(uint8_t keycode);

// Report a touch event at (x, y).
void report_touch_event(uint16_t x, uint16_t y);

// Report a trackball event (direction as uint8_t).
// Direction values: 0=None, 1=Up, 2=Down, 3=Left, 4=Right, 5=Click.
void report_trackball_event(uint8_t direction);

// ── LVGL rendering counters ────────────────────────────

// Get the total LVGL render count (incremented on each flush).
uint32_t get_lvgl_render_count();

// Increment the render count (called from display flush callback).
void increment_render_count();

}  // namespace input
}  // namespace telemetry
}  // namespace sigurdos

// ── Inline stubs when disabled ────────────────────────
#if !SIGURDOS_TELEMETRY

namespace sigurdos {
namespace telemetry {
namespace input {

inline void report_key_event(uint8_t) {}
inline void report_touch_event(uint16_t, uint16_t) {}
inline void report_trackball_event(uint8_t) {}
inline uint32_t get_lvgl_render_count() { return 0; }
inline void increment_render_count() {}

}  // namespace input
}  // namespace telemetry
}  // namespace sigurdos

#endif // !SIGURDOS_TELEMETRY

#endif // SIGURDOS_TELEMETRY_INPUT_H
