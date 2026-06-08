// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.

#include <cstring>

#include <gtest/gtest.h>

#include "hal/prefs.h"

namespace {

sigurdos::NodePrefs defaults_from_dirty_memory() {
    sigurdos::NodePrefs prefs;
    std::memset(&prefs, 0xA5, sizeof(prefs));
    prefs.set_defaults();
    return prefs;
}

TEST(PrefsDefaultsTest, RadioDefaultsStayNonTransmittingUntilConfigured) {
    const sigurdos::NodePrefs prefs = defaults_from_dirty_memory();

    EXPECT_FALSE(prefs.configured);
    EXPECT_FLOAT_EQ(0.0f, prefs.freq);
    EXPECT_FLOAT_EQ(0.0f, prefs.bw);
    EXPECT_EQ(0, prefs.sf);
    EXPECT_EQ(0, prefs.cr);
    EXPECT_EQ(0, prefs.tx_power_dbm);
    EXPECT_EQ(0, prefs.duty_cycle);
}

TEST(PrefsDefaultsTest, IdentityAndPrivacyDefaultsAreDeterministic) {
    const sigurdos::NodePrefs prefs = defaults_from_dirty_memory();

    EXPECT_STREQ("SigurdOS T-Deck", prefs.node_name);
    EXPECT_EQ('\0', prefs.node_name[sizeof(prefs.node_name) - 1]);
    EXPECT_FALSE(prefs.share_location);
    EXPECT_FALSE(prefs.gps_enabled);
    EXPECT_EQ(0, prefs.gps_interval);
    EXPECT_EQ(0u, prefs.device_pin);
}

TEST(PrefsDefaultsTest, MeshBehaviorDefaultsMatchSafeCompanionSettings) {
    const sigurdos::NodePrefs prefs = defaults_from_dirty_memory();

    EXPECT_EQ(200, prefs.chat_msg_cap);
    EXPECT_EQ(0, prefs.flood_max_hops);
    EXPECT_EQ(0, prefs.autoadd_max_hops);
    EXPECT_EQ(0x1E, prefs.autoadd_config);
    EXPECT_EQ(0, prefs.advert_interval);
    EXPECT_EQ(1, prefs.advert_type);
    EXPECT_FALSE(prefs.multi_acks);
    EXPECT_EQ(0, prefs.client_repeat);
    EXPECT_FALSE(prefs.ble_enabled);
    // Default 1-byte path hash (mode 0) — backward compatible with pre-1.14 repeaters.
    EXPECT_EQ(0, prefs.path_hash_mode);
}

TEST(PrefsDefaultsTest, DelayAndRadioAssistDefaultsAreInitialized) {
    const sigurdos::NodePrefs prefs = defaults_from_dirty_memory();

    EXPECT_FLOAT_EQ(10.0f, prefs.rx_delay_base);
    EXPECT_FLOAT_EQ(1.0f, prefs.tx_delay_factor);
    EXPECT_FLOAT_EQ(1.0f, prefs.direct_tx_delay_factor);
    EXPECT_FALSE(prefs.rx_boosted_gain);
}

TEST(PrefsDefaultsTest, UiConnectivityAndRegionDefaultsAreInitialized) {
    const sigurdos::NodePrefs prefs = defaults_from_dirty_memory();

    EXPECT_EQ(127, prefs.kbd_backlight);
    EXPECT_EQ(200, prefs.display_brightness);
    EXPECT_EQ(30, prefs.auto_off_timeout);
    EXPECT_EQ(0, prefs.theme_id);
    EXPECT_FALSE(prefs.buzzer_quiet);
    EXPECT_EQ('\0', prefs.wifi_ssid[0]);
    EXPECT_EQ('\0', prefs.wifi_password[0]);
    EXPECT_EQ('\0', prefs.active_region[0]);
}

} // namespace
