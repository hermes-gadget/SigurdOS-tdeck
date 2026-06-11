// SPDX-License-Identifier: GPL-3.0-or-later

#include "buzzer.h"
#include "tdeck_pins.h"
#include <Arduino.h>

#ifndef PLATFORMIO_UNIT_TESTING

namespace sigurdos {
namespace hal {

namespace {

// Non-blocking playback state — buzzer_loop() advances through the active
// pattern. Starting a beep while one is playing replaces it (restart
// semantics); no call site can trigger overlap today: the only caller is
// the message-arrival path in ui.cpp.
const BuzzerPatternStep* s_pattern = nullptr;
std::size_t s_count = 0;
std::size_t s_idx = 0;
uint32_t s_step_started_ms = 0;
bool s_active = false;

void buzzer_apply_step() {
    digitalWrite(PIN_BUZZER, s_pattern[s_idx].level_high ? HIGH : LOW);
    s_step_started_ms = millis();
    if (s_pattern[s_idx].duration_ms == 0) {
        // Terminal marker: apply its level, then idle LOW.
        digitalWrite(PIN_BUZZER, LOW);
        s_active = false;
    }
}

void buzzer_start_pattern(BuzzerPatternKind kind) {
    s_pattern = sigurdos_buzzer_pattern(kind, &s_count);
    s_idx = 0;
    s_active = true;
    buzzer_apply_step();
}

} // namespace

void buzzer_init() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
}

void buzzer_loop() {
    if (!s_active) return;
    if (millis() - s_step_started_ms < s_pattern[s_idx].duration_ms) return;
    s_idx++;
    if (s_idx >= s_count) {
        digitalWrite(PIN_BUZZER, LOW);
        s_active = false;
        return;
    }
    buzzer_apply_step();
}

void buzzer_beep_short() {
    buzzer_start_pattern(BuzzerPatternKind::Short);
}

void buzzer_beep_double() {
    buzzer_start_pattern(BuzzerPatternKind::Double);
}

} // namespace hal
} // namespace sigurdos

#else  // PLATFORMIO_UNIT_TESTING - no-op stubs

namespace sigurdos {
namespace hal {
void buzzer_init() {}
void buzzer_loop() {}
void buzzer_beep_short() {}
void buzzer_beep_double() {}
} // namespace hal
} // namespace sigurdos

#endif
