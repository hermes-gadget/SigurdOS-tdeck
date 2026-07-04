#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <cstddef>
#include <cstdint>

namespace sigurdos {

struct NodePrefs;

static constexpr size_t RADIO_PROFILE_ID_SIZE = 16;

struct RadioProfile {
    const char* id;
    const char* label;
    const char* short_label;
    const char* band_label;
    float freq_mhz;
    float bw_khz;
    uint8_t sf;
    uint8_t cr;
    int8_t tx_power_dbm;
};

size_t radio_profile_count();
const RadioProfile* radio_profile_at(size_t index);
const RadioProfile* radio_profile_default();
const RadioProfile* radio_profile_find(const char* id);
const RadioProfile* radio_profile_match(float freq_mhz, float bw_khz,
                                         uint8_t sf, uint8_t cr,
                                         int8_t tx_power_dbm);
const RadioProfile* radio_profile_match(const NodePrefs& prefs);
void radio_profile_apply(const RadioProfile& profile, NodePrefs& prefs);
void radio_profile_set_custom(NodePrefs& prefs);

} // namespace sigurdos
