#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstring>
#include "../hal/trackball.h"

namespace sigurdos::ui {

static constexpr uint8_t PIN_MAX_FAILURES = 3;
static constexpr uint32_t PIN_LOCKOUT_MS = 30000;

class PinAttemptState {
public:
    bool can_attempt(uint32_t now_ms) {
        if (failures_ < PIN_MAX_FAILURES) return true;
        if (static_cast<uint32_t>(now_ms - locked_at_ms_) < PIN_LOCKOUT_MS) return false;
        reset();
        return true;
    }

    void record_failure(uint32_t now_ms) {
        if (failures_ < PIN_MAX_FAILURES) ++failures_;
        if (failures_ == PIN_MAX_FAILURES) locked_at_ms_ = now_ms;
    }

    uint8_t remaining() const {
        return failures_ >= PIN_MAX_FAILURES ? 0 : PIN_MAX_FAILURES - failures_;
    }

    void reset() { failures_ = 0; locked_at_ms_ = 0; }

private:
    uint8_t failures_ = 0;
    uint32_t locked_at_ms_ = 0;
};

inline bool pin_value_valid(const char* value) {
    if (!value || std::strlen(value) != 4 || std::strcmp(value, "0000") == 0) return false;
    for (int i = 0; i < 4; ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
    }
    return true;
}

inline bool pin_confirmation_valid(const char* value, const char* confirmation) {
    return pin_value_valid(value) && confirmation && std::strcmp(value, confirmation) == 0;
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
