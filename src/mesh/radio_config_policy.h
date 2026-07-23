// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>

namespace sigurdos {
namespace mesh {

static constexpr int16_t RADIO_DRIVER_OK = 0;

struct RadioConfig {
    float frequency_mhz;
    float bandwidth_khz;
    int spreading_factor;
    int coding_rate;
    int tx_power_dbm;
    bool rx_boosted_gain;

    RadioConfig(float frequency = 0.0f, float bandwidth = 0.0f,
                int spreading = 0, int coding = 0, int power = 0,
                bool boosted = false)
        : frequency_mhz(frequency), bandwidth_khz(bandwidth),
          spreading_factor(spreading), coding_rate(coding),
          tx_power_dbm(power), rx_boosted_gain(boosted) {}
};

enum class RadioConfigField : uint8_t {
    Frequency,
    Bandwidth,
    SpreadingFactor,
    CodingRate,
    TxPower,
    RxBoostedGain,
};

struct RadioApplyResult {
    bool ok;
    RadioConfigField failed_field;
    int16_t driver_error;

    RadioApplyResult(bool success = false,
                     RadioConfigField field = RadioConfigField::Frequency,
                     int16_t error = RADIO_DRIVER_OK)
        : ok(success), failed_field(field), driver_error(error) {}
};

struct RadioTransactionResult {
    bool applied;
    bool rollback_attempted;
    bool rollback_succeeded;
    RadioApplyResult apply_result;
    RadioApplyResult rollback_result;

    RadioTransactionResult()
        : applied(false), rollback_attempted(false),
          rollback_succeeded(false), apply_result(), rollback_result() {}
};

struct RadioCommitResult {
    bool applied;
    bool commit_attempted;
    bool committed;
    bool restore_attempted;
    bool restore_succeeded;

    RadioCommitResult()
        : applied(false), commit_attempted(false), committed(false),
          restore_attempted(false), restore_succeeded(false) {}
};

inline const char* radioConfigFieldName(RadioConfigField field)
{
    switch (field) {
        case RadioConfigField::Frequency: return "frequency";
        case RadioConfigField::Bandwidth: return "bandwidth";
        case RadioConfigField::SpreadingFactor: return "spreading factor";
        case RadioConfigField::CodingRate: return "coding rate";
        case RadioConfigField::TxPower: return "TX power";
        case RadioConfigField::RxBoostedGain: return "RX gain";
    }
    return "unknown setting";
}

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
    if (!std::isfinite(bandwidth_khz)) return false;
    for (float candidate : supported) {
        if (std::fabs(bandwidth_khz - candidate) < 0.02f) return true;
    }
    return false;
}

inline bool sx1262RadioConfigSupported(const RadioConfig& config)
{
    return std::isfinite(config.frequency_mhz) &&
           config.frequency_mhz >= 150.0f &&
           config.frequency_mhz <= 960.0f &&
           sx1262BandwidthSupportedKHz(config.bandwidth_khz) &&
           config.spreading_factor >= 5 && config.spreading_factor <= 12 &&
           config.coding_rate >= 5 && config.coding_rate <= 8 &&
           config.tx_power_dbm >= -9 && config.tx_power_dbm <= 22;
}

template <typename SetFieldFn>
inline RadioApplyResult applyRadioConfigFields(const RadioConfig& config,
                                               SetFieldFn set_field)
{
    static constexpr RadioConfigField fields[] = {
        RadioConfigField::Frequency,
        RadioConfigField::Bandwidth,
        RadioConfigField::SpreadingFactor,
        RadioConfigField::CodingRate,
        RadioConfigField::TxPower,
        RadioConfigField::RxBoostedGain,
    };
    for (RadioConfigField field : fields) {
        const int16_t result = set_field(field, config);
        if (result != RADIO_DRIVER_OK) {
            return RadioApplyResult(false, field, result);
        }
    }
    return RadioApplyResult(
        true, RadioConfigField::Frequency, RADIO_DRIVER_OK);
}

// Applying each field cannot be made physically atomic by RadioLib. This
// transaction restores every field from the previous known-good config when
// any requested setter fails, and reports rollback failure separately.
template <typename ApplyFn>
inline RadioTransactionResult applyRadioConfigTransaction(
    const RadioConfig& requested,
    const RadioConfig& previous,
    ApplyFn apply)
{
    RadioTransactionResult result;
    result.apply_result = apply(requested);
    if (result.apply_result.ok) {
        result.applied = true;
        result.rollback_succeeded = true;
        return result;
    }

    result.rollback_attempted = true;
    result.rollback_result = apply(previous);
    result.rollback_succeeded = result.rollback_result.ok;
    return result;
}

// Persist only after hardware accepts the complete configuration. If the
// durable commit fails, restore the previous known-good hardware state.
template <typename ApplyFn, typename CommitFn>
inline RadioCommitResult applyAndCommitRadioConfig(
    const RadioConfig& requested,
    const RadioConfig& previous,
    ApplyFn apply,
    CommitFn commit)
{
    RadioCommitResult result;
    result.applied = apply(requested);
    if (!result.applied) return result;

    result.commit_attempted = true;
    result.committed = commit();
    if (result.committed) return result;

    result.restore_attempted = true;
    result.restore_succeeded = apply(previous);
    return result;
}

} // namespace mesh
} // namespace sigurdos
