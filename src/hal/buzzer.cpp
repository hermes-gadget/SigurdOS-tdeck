// SPDX-License-Identifier: GPL-3.0-or-later

#include "buzzer.h"
#include "tdeck_pins.h"
#include <Arduino.h>

#ifndef PLATFORMIO_UNIT_TESTING

namespace sigurdos {
namespace hal {

void buzzer_init() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
}

void buzzer_beep_short() {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(80);
    digitalWrite(PIN_BUZZER, LOW);
}

void buzzer_beep_double() {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(60);
    digitalWrite(PIN_BUZZER, LOW);
    delay(60);
    digitalWrite(PIN_BUZZER, HIGH);
    delay(60);
    digitalWrite(PIN_BUZZER, LOW);
}

} // namespace hal
} // namespace sigurdos

#else  // PLATFORMIO_UNIT_TESTING — no-op stubs

namespace sigurdos {
namespace hal {
void buzzer_init() {}
void buzzer_beep_short() {}
void buzzer_beep_double() {}
} // namespace hal
} // namespace sigurdos

#endif
