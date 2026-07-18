#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <cstdint>

namespace sigurdos::mesh::airtime_policy {

// C-01: MeshCore represents an airtime budget as factor=(100-p)/p.
// A configured 0% means the product's explicit "unlimited" policy, represented
// by factor 0 (the Dispatcher then permits a 100% duty fraction).
inline float dutyCyclePercentToFactor(uint8_t percent) {
    if (percent == 0 || percent >= 100) return 0.0f;
    return (100.0f - static_cast<float>(percent)) / static_cast<float>(percent);
}

inline uint8_t factorToDutyCyclePercent(float factor) {
    if (!std::isfinite(factor) || factor <= 0.0f) return 0;
    const float percent = 100.0f / (1.0f + factor);
    if (percent <= 1.0f) return 1;
    if (percent >= 100.0f) return 100;
    return static_cast<uint8_t>(percent + 0.5f);
}

inline void setDutyCyclePercent(float& airtime_factor, uint8_t& duty_cycle,
                                uint8_t percent) {
    duty_cycle = percent > 100 ? 100 : percent;
    airtime_factor = dutyCyclePercentToFactor(duty_cycle);
}

}  // namespace sigurdos::mesh::airtime_policy
