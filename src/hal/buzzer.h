#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace hal {

enum class BuzzerPatternKind : uint8_t {
    Short,
    Double,
};

struct BuzzerPatternStep {
    bool level_high;
    uint16_t duration_ms;
};

static constexpr uint16_t SIGURDOS_BUZZER_SHORT_ON_MS = 80;
static constexpr uint16_t SIGURDOS_BUZZER_DOUBLE_ON_MS = 60;
static constexpr uint16_t SIGURDOS_BUZZER_DOUBLE_GAP_MS = 60;

inline const BuzzerPatternStep* sigurdos_buzzer_pattern(BuzzerPatternKind kind,
                                                        std::size_t* count) {
    static constexpr BuzzerPatternStep short_pattern[] = {
        {true, SIGURDOS_BUZZER_SHORT_ON_MS},
        {false, 0},
    };
    static constexpr BuzzerPatternStep double_pattern[] = {
        {true, SIGURDOS_BUZZER_DOUBLE_ON_MS},
        {false, SIGURDOS_BUZZER_DOUBLE_GAP_MS},
        {true, SIGURDOS_BUZZER_DOUBLE_ON_MS},
        {false, 0},
    };

    const BuzzerPatternStep* pattern = short_pattern;
    std::size_t pattern_count = sizeof(short_pattern) / sizeof(short_pattern[0]);

    if (kind == BuzzerPatternKind::Double) {
        pattern = double_pattern;
        pattern_count = sizeof(double_pattern) / sizeof(double_pattern[0]);
    }

    if (count) {
        *count = pattern_count;
    }
    return pattern;
}

// Initialize buzzer GPIO
void buzzer_init();

// Advance non-blocking pattern playback — call once per main loop iteration
void buzzer_loop();

// Short beep (~100ms) - for DM arrival
void buzzer_beep_short();

// Double beep - for channel message arrival
void buzzer_beep_double();

} // namespace hal
} // namespace sigurdos
