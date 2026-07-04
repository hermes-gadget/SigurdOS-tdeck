// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstring>

#include <gtest/gtest.h>

#include "hal/prefs.h"
#include "hal/radio_profiles.h"

namespace {

TEST(RadioProfilesTest, DefaultProfileIsUnitedStates915) {
    const auto* profile = sigurdos::radio_profile_default();

    ASSERT_NE(nullptr, profile);
    EXPECT_STREQ("us_902_928", profile->id);
    EXPECT_STREQ("USA 902-928", profile->short_label);
    EXPECT_FLOAT_EQ(915.000f, profile->freq_mhz);
    EXPECT_FLOAT_EQ(62.5f, profile->bw_khz);
    EXPECT_EQ(8, profile->sf);
    EXPECT_EQ(5, profile->cr);
    EXPECT_EQ(22, profile->tx_power_dbm);
}

TEST(RadioProfilesTest, UnitedStatesAndCanadaUseAuditTuple) {
    const auto* us = sigurdos::radio_profile_find("us_902_928");
    const auto* ca = sigurdos::radio_profile_find("ca_902_928");

    ASSERT_NE(nullptr, us);
    ASSERT_NE(nullptr, ca);
    EXPECT_FLOAT_EQ(us->freq_mhz, ca->freq_mhz);
    EXPECT_FLOAT_EQ(915.000f, ca->freq_mhz);
    EXPECT_FLOAT_EQ(62.5f, ca->bw_khz);
    EXPECT_EQ(8, ca->sf);
    EXPECT_EQ(5, ca->cr);
    EXPECT_EQ(22, ca->tx_power_dbm);
}

TEST(RadioProfilesTest, ApplySetsPrefsAndKeepsTransmitGuardExplicit) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    ASSERT_FALSE(prefs.configured);

    const auto* ca = sigurdos::radio_profile_find("ca_902_928");
    ASSERT_NE(nullptr, ca);
    sigurdos::radio_profile_apply(*ca, prefs);

    EXPECT_TRUE(prefs.configured);
    EXPECT_STREQ("ca_902_928", prefs.radio_profile);
    EXPECT_FLOAT_EQ(915.000f, prefs.freq);
    EXPECT_FLOAT_EQ(62.5f, prefs.bw);
    EXPECT_EQ(8, prefs.sf);
    EXPECT_EQ(5, prefs.cr);
    EXPECT_EQ(22, prefs.tx_power_dbm);
}

TEST(RadioProfilesTest, SavedProfileBreaksTiesForIdenticalTuples) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    const auto* ca = sigurdos::radio_profile_find("ca_902_928");
    ASSERT_NE(nullptr, ca);
    sigurdos::radio_profile_apply(*ca, prefs);

    const auto* matched = sigurdos::radio_profile_match(prefs);

    ASSERT_NE(nullptr, matched);
    EXPECT_STREQ("ca_902_928", matched->id);
}

TEST(RadioProfilesTest, CustomMarksManualSettings) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    prefs.freq = 916.250f;
    prefs.bw = 125.0f;
    prefs.sf = 9;
    prefs.cr = 6;
    prefs.tx_power_dbm = 20;
    prefs.configured = true;

    EXPECT_EQ(nullptr, sigurdos::radio_profile_match(prefs));

    sigurdos::radio_profile_set_custom(prefs);
    EXPECT_STREQ("custom", prefs.radio_profile);
}

} // namespace
