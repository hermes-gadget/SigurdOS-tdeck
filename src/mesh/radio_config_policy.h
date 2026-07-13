#pragma once

#include <cstdint>

namespace sigurdos {
namespace mesh {

struct RadioConfig {
    float frequency_mhz;
    float bandwidth_khz;
    int spreading_factor;
    int coding_rate;
    int tx_power_dbm;
    bool rx_boosted_gain;
};

inline bool sx1262BandwidthSupportedHz(uint32_t bandwidth_hz)
{
    switch (bandwidth_hz) {
        case 7800:
        case 10400:
        case 15600:
        case 20800:
        case 31250:
        case 41700:
        case 62500:
        case 125000:
        case 250000:
        case 500000:
            return true;
        default:
            return false;
    }
}

inline bool sx1262BandwidthSupportedKHz(float bandwidth_khz)
{
    static constexpr float supported[] = {
        7.8f, 10.4f, 15.6f, 20.8f, 31.25f,
        41.7f, 62.5f, 125.0f, 250.0f, 500.0f,
    };
    for (float candidate : supported) {
        float delta = bandwidth_khz - candidate;
        if (delta < 0.0f) delta = -delta;
        if (delta < 0.001f) return true;
    }
    return false;
}

inline bool sx1262RadioConfigSupported(const RadioConfig& config)
{
    return config.frequency_mhz >= 150.0f &&
           config.frequency_mhz <= 960.0f &&
           sx1262BandwidthSupportedKHz(config.bandwidth_khz) &&
           config.spreading_factor >= 5 && config.spreading_factor <= 12 &&
           config.coding_rate >= 5 && config.coding_rate <= 8 &&
           config.tx_power_dbm >= -9 && config.tx_power_dbm <= 22;
}

template <typename ApplyFn>
inline bool applyRadioConfigTransaction(const RadioConfig& requested,
                                        const RadioConfig& previous,
                                        ApplyFn apply,
                                        bool* rollback_succeeded = nullptr)
{
    if (apply(requested)) {
        if (rollback_succeeded) *rollback_succeeded = true;
        return true;
    }
    const bool restored = apply(previous);
    if (rollback_succeeded) *rollback_succeeded = restored;
    return false;
}

} // namespace mesh
} // namespace sigurdos
