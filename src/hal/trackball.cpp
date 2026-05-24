// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "trackball.h"
#include "tdeck_pins.h"
#include <Arduino.h>

static constexpr uint32_t CLICK_DEBOUNCE_MS = 20;
static constexpr uint32_t DIRECTION_DEADTIME_MS = 150;
static constexpr uint32_t LEFT_DEADTIME_MS = 80;
static constexpr uint32_t DIRECTION_SETTLE_MS = 250;
static constexpr uint8_t EVENT_QUEUE_SIZE = 8;

#if defined(SLOPOS_TRACKBALL_DEBUG)
static constexpr uint32_t DEBUG_STATUS_MS = 250;
#endif

struct ButtonState {
    uint8_t pin;
    SlopOSTrackballEvent event;
    bool direction;
    int last_raw;
    bool raw_active;
    bool stable_active;
    uint32_t raw_changed_at;
    uint32_t last_event_at;
    uint32_t deadtime_ms;
};

static ButtonState buttons[] = {
    {PIN_TRACKBALL_UP,    SlopOSTrackballEvent::Up,    true,  HIGH, false, false, 0, 0, DIRECTION_DEADTIME_MS},
    {PIN_TRACKBALL_DOWN,  SlopOSTrackballEvent::Down,  true,  HIGH, false, false, 0, 0, DIRECTION_DEADTIME_MS},
    {PIN_TRACKBALL_LEFT,  SlopOSTrackballEvent::Left,  true,  HIGH, false, false, 0, 0, LEFT_DEADTIME_MS},
    {PIN_TRACKBALL_RIGHT, SlopOSTrackballEvent::Right, true,  HIGH, false, false, 0, 0, DIRECTION_DEADTIME_MS},
    {PIN_TRACKBALL_BTN,   SlopOSTrackballEvent::Click, false, HIGH, false, false, 0, 0, CLICK_DEBOUNCE_MS},
};

static bool initialized = false;
static uint32_t initialized_at = 0;
static SlopOSTrackballEvent event_queue[EVENT_QUEUE_SIZE];
static uint8_t queue_head = 0;
static uint8_t queue_tail = 0;
static uint8_t queue_count = 0;
#if defined(SLOPOS_TRACKBALL_DEBUG)
static uint32_t last_debug_status_at = 0;
#endif

static bool raw_pin_active(const ButtonState& btn)
{
    const int raw = digitalRead(btn.pin);
    return btn.direction ? raw != btn.last_raw : raw == LOW;
}

#if defined(SLOPOS_TRACKBALL_DEBUG)
static const char* event_name(SlopOSTrackballEvent event)
{
    switch (event) {
    case SlopOSTrackballEvent::Up: return "UP";
    case SlopOSTrackballEvent::Down: return "DOWN";
    case SlopOSTrackballEvent::Left: return "LEFT";
    case SlopOSTrackballEvent::Right: return "RIGHT";
    case SlopOSTrackballEvent::Click: return "CLICK";
    case SlopOSTrackballEvent::None:
    default: return "NONE";
    }
}
#endif

static void queue_event(SlopOSTrackballEvent event)
{
#if defined(SLOPOS_TRACKBALL_DEBUG)
    Serial.printf("[trackball] event=%s shadow=%d queue=%u raw(U,D,L,R,C)=%d,%d,%d,%d,%d active=%d,%d,%d,%d,%d ms=%lu\n",
                  event_name(event),
#if defined(SLOPOS_TRACKBALL_DEBUG_SHADOW)
                  1,
#else
                  0,
#endif
                  queue_count,
                  digitalRead(PIN_TRACKBALL_UP),
                  digitalRead(PIN_TRACKBALL_DOWN),
                  digitalRead(PIN_TRACKBALL_LEFT),
                  digitalRead(PIN_TRACKBALL_RIGHT),
                  digitalRead(PIN_TRACKBALL_BTN),
                  raw_pin_active(buttons[0]),
                  raw_pin_active(buttons[1]),
                  raw_pin_active(buttons[2]),
                  raw_pin_active(buttons[3]),
                  raw_pin_active(buttons[4]),
                  (unsigned long)millis());
#if defined(SLOPOS_TRACKBALL_DEBUG_SHADOW)
    return;
#endif
#endif

    if (queue_count >= EVENT_QUEUE_SIZE) return;
    event_queue[queue_head] = event;
    queue_head = (uint8_t)((queue_head + 1) % EVENT_QUEUE_SIZE);
    queue_count++;
}

#if defined(SLOPOS_TRACKBALL_DEBUG)
static void debug_status(uint32_t now)
{
    if (now - last_debug_status_at < DEBUG_STATUS_MS) return;
    last_debug_status_at = now;

    Serial.printf("[trackball] status queue=%u raw(U,D,L,R,C)=%d,%d,%d,%d,%d active=%d,%d,%d,%d,%d ms=%lu\n",
                  queue_count,
                  digitalRead(PIN_TRACKBALL_UP),
                  digitalRead(PIN_TRACKBALL_DOWN),
                  digitalRead(PIN_TRACKBALL_LEFT),
                  digitalRead(PIN_TRACKBALL_RIGHT),
                  digitalRead(PIN_TRACKBALL_BTN),
                  raw_pin_active(buttons[0]),
                  raw_pin_active(buttons[1]),
                  raw_pin_active(buttons[2]),
                  raw_pin_active(buttons[3]),
                  raw_pin_active(buttons[4]),
                  (unsigned long)now);
}

static void debug_change(const ButtonState& btn, bool active, uint32_t now)
{
    Serial.printf("[trackball] change event=%s pin=%u active=%d raw=%d ms=%lu\n",
                  event_name(btn.event),
                  btn.pin,
                  active,
                  digitalRead(btn.pin),
                  (unsigned long)now);
}
#endif

static void reset_button(ButtonState& btn, uint32_t now)
{
    btn.last_raw = digitalRead(btn.pin);
    btn.raw_active = btn.direction ? false : raw_pin_active(btn);
    btn.stable_active = btn.raw_active;
    btn.raw_changed_at = now;
    btn.last_event_at = 0;
}

static void scan_direction(ButtonState& btn, uint32_t now)
{
    const int raw = digitalRead(btn.pin);
    if (raw == btn.last_raw) return;

    btn.last_raw = raw;
    btn.raw_changed_at = now;
#if defined(SLOPOS_TRACKBALL_DEBUG)
    debug_change(btn, true, now);
#endif

    if (now - initialized_at < DIRECTION_SETTLE_MS) return;

    // Fire only on falling edge (HIGH→LOW) — one event per physical detent
    // LEFT is an exception: fires on both edges for better responsiveness
    if (btn.event != SlopOSTrackballEvent::Left && raw == HIGH) return;

    if (btn.last_event_at != 0 && now - btn.last_event_at < btn.deadtime_ms) return;

    queue_event(btn.event);
    btn.last_event_at = now;
}

static void scan_click(ButtonState& btn, uint32_t now)
{
    const bool raw = raw_pin_active(btn);

    if (raw != btn.raw_active) {
        btn.raw_active = raw;
        btn.raw_changed_at = now;
#if defined(SLOPOS_TRACKBALL_DEBUG)
        debug_change(btn, raw, now);
#endif
        return;
    }

    if (raw != btn.stable_active && now - btn.raw_changed_at >= CLICK_DEBOUNCE_MS) {
        btn.stable_active = raw;
        if (btn.stable_active) {
            queue_event(btn.event);
        }
        return;
    }
}

static void scan_button(ButtonState& btn, uint32_t now)
{
    if (btn.direction) {
        scan_direction(btn, now);
    } else {
        scan_click(btn, now);
    }
}

bool slopos_trackball_init()
{
    for (ButtonState& btn : buttons) {
        pinMode(btn.pin, INPUT_PULLUP);
    }

    initialized = true;
    initialized_at = millis();
    slopos_trackball_reset_scan_state();
#if defined(SLOPOS_TRACKBALL_DEBUG)
    Serial.printf("[trackball] debug enabled shadow=%d pins U=%u D=%u L=%u R=%u C=%u\n",
#if defined(SLOPOS_TRACKBALL_DEBUG_SHADOW)
                  1,
#else
                  0,
#endif
                  PIN_TRACKBALL_UP,
                  PIN_TRACKBALL_DOWN,
                  PIN_TRACKBALL_LEFT,
                  PIN_TRACKBALL_RIGHT,
                  PIN_TRACKBALL_BTN);
    Serial.printf("[trackball] initial raw U,D,L,R,C=%d,%d,%d,%d,%d settle_ms=%lu\n",
                  buttons[0].last_raw,
                  buttons[1].last_raw,
                  buttons[2].last_raw,
                  buttons[3].last_raw,
                  buttons[4].last_raw,
                  (unsigned long)DIRECTION_SETTLE_MS);
#endif
    return true;
}

void slopos_trackball_scan()
{
    if (!initialized) return;

    const uint32_t now = millis();
#if defined(SLOPOS_TRACKBALL_DEBUG)
    debug_status(now);
#endif
    for (ButtonState& btn : buttons) {
        scan_button(btn, now);
    }
}

bool slopos_trackball_next_event(SlopOSTrackballEvent* out)
{
    if (!out || queue_count == 0) return false;

    *out = event_queue[queue_tail];
    queue_tail = (uint8_t)((queue_tail + 1) % EVENT_QUEUE_SIZE);
    queue_count--;
    return true;
}

void slopos_trackball_reset_scan_state()
{
    const uint32_t now = millis();
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
#if defined(SLOPOS_TRACKBALL_DEBUG)
    last_debug_status_at = 0;
#endif

    for (ButtonState& btn : buttons) {
        reset_button(btn, now);
    }
}
