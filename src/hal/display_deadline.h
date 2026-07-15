// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace sigurdos {
namespace hal {

struct DisplayAutoOffDeadline {
    bool enabled;
    uint32_t at;

    DisplayAutoOffDeadline() : enabled(false), at(0) {}

    void arm(uint32_t now, uint32_t timeout_ms) {
        enabled = timeout_ms != 0;
        at = enabled ? now + timeout_ms : 0;
    }

    bool reached(uint32_t now) const {
        return enabled && static_cast<int32_t>(now - at) >= 0;
    }
};

}  // namespace hal
}  // namespace sigurdos
