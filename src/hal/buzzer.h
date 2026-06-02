#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

namespace sigurdos {
namespace hal {

// Initialize buzzer GPIO
void buzzer_init();

// Short beep (~100ms) — for DM arrival
void buzzer_beep_short();

// Double beep — for channel message arrival
void buzzer_beep_double();

} // namespace hal
} // namespace sigurdos
