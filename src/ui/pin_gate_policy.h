#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../hal/trackball.h"

namespace sigurdos::ui {

struct PinGateState {
    uint8_t failures = 0;
    uint32_t failure_at_ms = 0;
    uint32_t lockout_ms = 0;
    uint32_t last_unlock_ms = 0;
};

inline uint32_t pinFailureDelay(uint8_t failures) {
    static constexpr uint32_t DELAYS[] = {
        0, 1000, 5000, 30000, 60000, 300000
    };
    if (failures == 0) return 0;
    const size_t requested = static_cast<size_t>(failures - 1);
    const size_t count = sizeof(DELAYS) / sizeof(DELAYS[0]);
    const size_t index = requested < count ? requested : count - 1;
    return DELAYS[index];
}

inline bool pinGateLocked(const PinGateState& state, uint32_t now) {
    return state.lockout_ms != 0 &&
        static_cast<uint32_t>(now - state.failure_at_ms) < state.lockout_ms;
}

inline uint32_t pinGateRemaining(const PinGateState& state, uint32_t now) {
    if (!pinGateLocked(state, now)) return 0;
    return state.lockout_ms -
        static_cast<uint32_t>(now - state.failure_at_ms);
}

inline void pinGateRecordFailure(PinGateState& state, uint32_t now) {
    if (state.failures < UINT8_MAX) ++state.failures;
    state.failure_at_ms = now;
    state.lockout_ms = pinFailureDelay(state.failures);
}

inline void pinGateRecordSuccess(PinGateState& state, uint32_t now) {
    state.failures = 0;
    state.failure_at_ms = 0;
    state.lockout_ms = 0;
    state.last_unlock_ms = now == 0 ? 1 : now;
}

inline bool pinGateGraceActive(const PinGateState& state, uint32_t now,
                               uint32_t grace_ms) {
    return state.last_unlock_ms != 0 &&
        static_cast<uint32_t>(now - state.last_unlock_ms) < grace_ms;
}

inline void pinGateClearGrace(PinGateState& state) {
    state.last_unlock_ms = 0;
}

inline bool pin_value_valid(const char* value) {
    if (!value || value[0] == '0') return false;
    const size_t length = std::strlen(value);
    if (length < 4 || length > 6) return false;
    for (size_t i = 0; i < length; ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
    }
    return true;
}

inline bool pin_confirmation_valid(const char* value,
                                   const char* confirmation) {
    return pin_value_valid(value) && confirmation &&
        std::strcmp(value, confirmation) == 0;
}

enum class PinModalAction : uint8_t { None, FocusPrevious, FocusNext, Activate };

inline PinModalAction pin_modal_action(SigurdOSTrackballEvent event) {
    switch (event) {
    case SigurdOSTrackballEvent::Up:
    case SigurdOSTrackballEvent::Left:
        return PinModalAction::FocusPrevious;
    case SigurdOSTrackballEvent::Down:
    case SigurdOSTrackballEvent::Right:
        return PinModalAction::FocusNext;
    case SigurdOSTrackballEvent::Click:
        return PinModalAction::Activate;
    default:
        return PinModalAction::None;
    }
}

} // namespace sigurdos::ui
