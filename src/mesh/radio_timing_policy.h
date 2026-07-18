#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <cstdint>

namespace sigurdos::mesh::radio_timing_policy {

inline int rxDelay(float base, float score, uint32_t airtime_ms) {
    if (base <= 0.0f) return 0;
    return static_cast<int>(
        (std::pow(base, 0.85f - score) - 1.0f) * airtime_ms);
}

inline uint32_t retransmitUpperBound(uint32_t estimated_airtime_ms,
                                     float delay_factor,
                                     float airtime_fraction) {
    if (delay_factor <= 0.0f || airtime_fraction <= 0.0f) return 0;
    const uint32_t slot = static_cast<uint32_t>(
        estimated_airtime_ms * airtime_fraction * delay_factor);
    return slot == 0 ? 0 : 5 * slot + 1;
}

}  // namespace sigurdos::mesh::radio_timing_policy
