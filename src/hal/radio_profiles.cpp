// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "radio_profiles.h"

#include "prefs.h"

#include <cmath>
#include <cstring>

namespace sigurdos {
namespace {

static constexpr RadioProfile PROFILES[] = {
    {"us_902_928", "United States", "USA 902-928", "902-928 MHz",
     915.000f, 62.5f, 8, 5, 22},
    {"ca_902_928", "Canada", "Canada 902-928", "902-928 MHz",
     915.000f, 62.5f, 8, 5, 22},
    {"eu_868", "Europe", "EU 868", "868 MHz",
     868.000f, 62.5f, 8, 5, 22},
    {"uk_869_525", "United Kingdom", "UK 869.525", "869 MHz",
     869.525f, 250.0f, 10, 5, 22},
    {"uk_869_618", "United Kingdom alt", "UK 869.618", "869 MHz",
     869.618f, 62.5f, 8, 5, 22},
};

struct RepeatFrequencyRange {
    uint32_t lower_khz;
    uint32_t upper_khz;
};

// Keep the companion-radio compile-time policy and allow board variants to
// replace it with one or more `{ lower_khz, upper_khz }` entries.
#ifndef ALLOWED_REPEAT_FREQ_RANGE
#define ALLOWED_REPEAT_FREQ_RANGE \
    { 433000, 433000 },          \
    { 869495, 869495 },          \
    { 918000, 918000 }
#endif
static constexpr RepeatFrequencyRange REPEAT_FREQUENCY_RANGES[] = {
    ALLOWED_REPEAT_FREQ_RANGE
};

static bool nearly_equal(float a, float b, float epsilon)
{
    return std::fabs(a - b) <= epsilon;
}

static void copy_profile_id(char* dest, const char* id)
{
    if (!dest) return;
    if (!id) id = "";
    std::strncpy(dest, id, RADIO_PROFILE_ID_SIZE - 1);
    dest[RADIO_PROFILE_ID_SIZE - 1] = '\0';
}

} // namespace

size_t radio_profile_count()
{
    return sizeof(PROFILES) / sizeof(PROFILES[0]);
}

const RadioProfile* radio_profile_at(size_t index)
{
    return index < radio_profile_count() ? &PROFILES[index] : nullptr;
}

const RadioProfile* radio_profile_default()
{
    return &PROFILES[0];
}

const RadioProfile* radio_profile_find(const char* id)
{
    if (!id || !id[0]) return nullptr;
    for (size_t i = 0; i < radio_profile_count(); ++i) {
        if (std::strcmp(PROFILES[i].id, id) == 0) return &PROFILES[i];
    }
    return nullptr;
}

const RadioProfile* radio_profile_match(float freq_mhz, float bw_khz,
                                        uint8_t sf, uint8_t cr,
                                        int8_t tx_power_dbm)
{
    for (size_t i = 0; i < radio_profile_count(); ++i) {
        const RadioProfile& p = PROFILES[i];
        if (nearly_equal(freq_mhz, p.freq_mhz, 0.001f) &&
            nearly_equal(bw_khz, p.bw_khz, 0.01f) &&
            sf == p.sf && cr == p.cr && tx_power_dbm == p.tx_power_dbm) {
            return &p;
        }
    }
    return nullptr;
}

const RadioProfile* radio_profile_match(const NodePrefs& prefs)
{
    const RadioProfile* saved = radio_profile_find(prefs.radio_profile);
    if (saved &&
        nearly_equal(prefs.freq, saved->freq_mhz, 0.001f) &&
        nearly_equal(prefs.bw, saved->bw_khz, 0.01f) &&
        prefs.sf == saved->sf && prefs.cr == saved->cr &&
        prefs.tx_power_dbm == saved->tx_power_dbm) {
        return saved;
    }
    return radio_profile_match(prefs.freq, prefs.bw, prefs.sf, prefs.cr,
                               prefs.tx_power_dbm);
}

void radio_profile_apply(const RadioProfile& profile, NodePrefs& prefs)
{
    prefs.freq = profile.freq_mhz;
    prefs.bw = profile.bw_khz;
    prefs.sf = profile.sf;
    prefs.cr = profile.cr;
    prefs.tx_power_dbm = profile.tx_power_dbm;
    prefs.configured = true;
    copy_profile_id(prefs.radio_profile, profile.id);
}

void radio_profile_set_custom(NodePrefs& prefs)
{
    copy_profile_id(prefs.radio_profile, "custom");
}

bool radio_profile_repeat_frequency_khz(const NodePrefs& prefs,
                                        uint32_t* frequency_khz)
{
    if (!prefs.configured || !frequency_khz ||
        std::strcmp(prefs.radio_profile, "custom") == 0) {
        return false;
    }

    const RadioProfile* profile = radio_profile_match(prefs);
    if (!profile) return false;

    *frequency_khz = (uint32_t)std::lround(profile->freq_mhz * 1000.0f);
    return true;
}

size_t radio_profile_repeat_frequency_ranges(uint32_t* pairs, size_t max_pairs)
{
    if (!pairs || max_pairs == 0) return 0;
    const size_t range_count = sizeof(REPEAT_FREQUENCY_RANGES) /
                               sizeof(REPEAT_FREQUENCY_RANGES[0]);
    const size_t count = range_count < max_pairs ? range_count : max_pairs;
    for (size_t i = 0; i < count; ++i) {
        pairs[i * 2] = REPEAT_FREQUENCY_RANGES[i].lower_khz;
        pairs[i * 2 + 1] = REPEAT_FREQUENCY_RANGES[i].upper_khz;
    }
    return count;
}

bool radio_profile_repeat_frequency_allowed(uint32_t frequency_khz)
{
    for (size_t i = 0;
         i < sizeof(REPEAT_FREQUENCY_RANGES) / sizeof(REPEAT_FREQUENCY_RANGES[0]);
         ++i) {
        if (frequency_khz >= REPEAT_FREQUENCY_RANGES[i].lower_khz &&
            frequency_khz <= REPEAT_FREQUENCY_RANGES[i].upper_khz) {
            return true;
        }
    }
    return false;
}

} // namespace sigurdos
