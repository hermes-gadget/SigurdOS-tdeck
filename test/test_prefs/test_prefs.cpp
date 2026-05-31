// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SlopOS-TDeck.
//
// SlopOS-TDeck is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SlopOS-TDeck is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SlopOS-TDeck.  If not, see <https://www.gnu.org/licenses/>.


/**
 * Unit tests for NodePrefs defaults and native preference persistence.
 */
#include <gtest/gtest.h>

#include "hal/prefs.h"

namespace {

class PrefsTest : public ::testing::Test {
protected:
    void SetUp() override {
        slopos::NodePrefs defaults;
        defaults.set_defaults();
        slopos::prefs_set(defaults);
    }
};

TEST_F(PrefsTest, DefaultRxBoostedGainIsDisabled) {
    slopos::NodePrefs prefs;
    prefs.set_defaults();

    EXPECT_FALSE(prefs.rx_boosted_gain);
}

TEST_F(PrefsTest, RxBoostedGainRoundTripsThroughPrefsSetAndGet) {
    slopos::NodePrefs prefs;
    prefs.set_defaults();
    prefs.rx_boosted_gain = true;

    slopos::prefs_set(prefs);

    EXPECT_TRUE(slopos::prefs_get().rx_boosted_gain);

    prefs.rx_boosted_gain = false;
    slopos::prefs_set(prefs);

    EXPECT_FALSE(slopos::prefs_get().rx_boosted_gain);
}

TEST_F(PrefsTest, RxBoostedGainRoundTripsThroughPrefsSaveAndLoad) {
    slopos::NodePrefs saved;
    saved.set_defaults();
    saved.rx_boosted_gain = true;

    ASSERT_TRUE(slopos::prefs_save(saved));

    slopos::NodePrefs loaded;
    loaded.set_defaults();

    ASSERT_TRUE(slopos::prefs_load(loaded));
    EXPECT_TRUE(loaded.rx_boosted_gain);
}

} // namespace
