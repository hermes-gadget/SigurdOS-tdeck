// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Input event capture — reports keyboard, touch, and trackball
// events as @pins telemetry records.

#include "telemetry_input.h"
#include "telemetry_protocol.h"
#include "debug_cfg.h"

#if SIGURDOS_TELEMETRY

#include <Arduino.h>

namespace sigurdos {
namespace telemetry {
namespace input {

// ── Static counters ────────────────────────────────────
static uint16_t    s_lvgl_evq_depth      = 0;
static uint32_t    s_lvgl_render_count   = 0;
static uint8_t     s_last_key_code       = 0;
static uint32_t    s_last_key_ms         = 0;
static uint32_t    s_key_event_count     = 0;
static uint32_t    s_touch_count         = 0;
static uint32_t    s_trackball_count     = 0;

// ── Emit helper: optionally sample frequent events ─────

void report_key_event(uint8_t keycode) {
    s_last_key_code = keycode;
    s_last_key_ms = millis();
    s_key_event_count++;

    // Emit every 5th key event, or all control codes
    bool emit = (keycode < 0x20 || keycode > 0x7E) || (s_key_event_count % 5 == 0);
    if (!emit) return;

    emit_tag(tag::PINS);
    emit_sep();
    emit_kv_s(key::TYPE, "key");
    emit_sep();
    emit_kv_u("code", keycode);
    emit_sep();
    emit_kv_u("kcnt", s_key_event_count);
    emit_end();
}

void report_touch_event(uint16_t x, uint16_t y) {
    s_touch_count++;

    // Only emit on press, sample every 10th event to reduce noise
    // (callee should pass pressed==true to reduce spam)
    if (s_touch_count % 10 != 0) return;

    emit_tag(tag::PINS);
    emit_sep();
    emit_kv_s(key::TYPE, "touch");
    emit_sep();
    emit_kv("x", (int32_t)x);
    emit_sep();
    emit_kv("y", (int32_t)y);
    emit_sep();
    emit_kv_u("tcnt", s_touch_count);
    emit_end();
}

void report_trackball_event(uint8_t direction) {
    if (direction == 0) return;  // None

    s_trackball_count++;

    // Sample every 5th event, or always on Click
    bool is_click = (direction == 5);
    if (!is_click && s_trackball_count % 5 != 0) return;

    emit_tag(tag::PINS);
    emit_sep();
    emit_kv_s(key::TYPE, "track");
    emit_sep();
    emit_kv_u("dir", direction);
    emit_sep();
    emit_kv_u("tb_cnt", s_trackball_count);
    emit_end();
}

// ── LVGL counters ──────────────────────────────────────

uint16_t get_lvgl_evq_depth() {
    return s_lvgl_evq_depth;
}

uint32_t get_lvgl_render_count() {
    return s_lvgl_render_count;
}

void increment_render_count() {
    s_lvgl_render_count++;
}

}  // namespace input
}  // namespace telemetry
}  // namespace sigurdos

#endif // SIGURDOS_TELEMETRY
