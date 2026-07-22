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


/**
 * Unit tests for NodePrefs defaults and native preference persistence.
 */
#include <gtest/gtest.h>

#include "hal/prefs.h"
#include "hal/prefs_write_policy.h"
#include <limits>
#include <string>

namespace {

class PrefsTest : public ::testing::Test {
protected:
    void SetUp() override {
        sigurdos::NodePrefs defaults;
        defaults.set_defaults();
        ASSERT_TRUE(sigurdos::prefs_set(defaults));
    }
};

TEST_F(PrefsTest, DefaultRxBoostedGainIsDisabled) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();

    EXPECT_FALSE(prefs.rx_boosted_gain);
}

TEST_F(PrefsTest, RxBoostedGainRoundTripsThroughPrefsSetAndGet) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    prefs.rx_boosted_gain = true;

    ASSERT_TRUE(sigurdos::prefs_set(prefs));

    EXPECT_TRUE(sigurdos::prefs_get().rx_boosted_gain);

    prefs.rx_boosted_gain = false;
    ASSERT_TRUE(sigurdos::prefs_set(prefs));

    EXPECT_FALSE(sigurdos::prefs_get().rx_boosted_gain);
}

TEST_F(PrefsTest, RxBoostedGainRoundTripsThroughPrefsSaveAndLoad) {
    sigurdos::NodePrefs saved;
    saved.set_defaults();
    saved.rx_boosted_gain = true;

    ASSERT_TRUE(sigurdos::prefs_save(saved));

    sigurdos::NodePrefs loaded;
    loaded.set_defaults();

    ASSERT_TRUE(sigurdos::prefs_load(loaded));
    EXPECT_TRUE(loaded.rx_boosted_gain);
}

TEST_F(PrefsTest, DefaultPathHashModeIsOneByte) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();

    EXPECT_EQ(0, prefs.path_hash_mode);
}

TEST_F(PrefsTest, PathHashModeRoundTripsThroughPrefsSetAndGet) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    prefs.path_hash_mode = 2;  // 3-byte path hash

    ASSERT_TRUE(sigurdos::prefs_set(prefs));

    EXPECT_EQ(2, sigurdos::prefs_get().path_hash_mode);
}

TEST_F(PrefsTest, PathHashModeRoundTripsThroughPrefsSaveAndLoad) {
    sigurdos::NodePrefs saved;
    saved.set_defaults();
    saved.path_hash_mode = 1;  // 2-byte path hash

    ASSERT_TRUE(sigurdos::prefs_save(saved));

    sigurdos::NodePrefs loaded;
    loaded.set_defaults();

    ASSERT_TRUE(sigurdos::prefs_load(loaded));
    EXPECT_EQ(1, loaded.path_hash_mode);
}

TEST_F(PrefsTest, CorruptPathHashModeClampsToDefault) {
    EXPECT_EQ(0, sigurdos::detail::normalizePathHashMode(0));
    EXPECT_EQ(1, sigurdos::detail::normalizePathHashMode(1));
    EXPECT_EQ(2, sigurdos::detail::normalizePathHashMode(2));
    EXPECT_EQ(0, sigurdos::detail::normalizePathHashMode(3));
    EXPECT_EQ(0, sigurdos::detail::normalizePathHashMode(255));
}

TEST_F(PrefsTest, AirtimeFactorAndMultiAckCountRoundTripThroughPrefs) {
    sigurdos::NodePrefs saved;
    saved.set_defaults();
    saved.airtime_factor = 9.0f;
    saved.duty_cycle = 10;
    saved.multi_acks = 3;

    ASSERT_TRUE(sigurdos::prefs_save(saved));

    sigurdos::NodePrefs loaded;
    loaded.set_defaults();
    ASSERT_TRUE(sigurdos::prefs_load(loaded));
    EXPECT_FLOAT_EQ(loaded.airtime_factor, 9.0f);
    EXPECT_EQ(loaded.duty_cycle, 10);
    EXPECT_EQ(loaded.multi_acks, 3);
}

TEST_F(PrefsTest, KeyboardLayoutRoundTripsThroughPrefs) {
    sigurdos::NodePrefs saved;
    saved.set_defaults();
    saved.kbd_layout = 9;

    ASSERT_TRUE(sigurdos::prefs_save(saved));

    sigurdos::NodePrefs loaded;
    loaded.set_defaults();
    ASSERT_TRUE(sigurdos::prefs_load(loaded));
    EXPECT_EQ(9, loaded.kbd_layout);
}

TEST_F(PrefsTest, GpsIntervalPreservesZeroWhenUpdatedAtRuntime) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    prefs.gps_interval = 0;

    ASSERT_TRUE(sigurdos::prefs_set(prefs));

    EXPECT_EQ(0u, sigurdos::prefs_get().gps_interval);
}

TEST_F(PrefsTest, CompanionPolicyWidthsRoundTripWithoutTruncation) {
    sigurdos::NodePrefs saved;
    saved.set_defaults();
    saved.gps_interval = 86400;
    saved.multi_acks = 7;
    saved.advert_loc_policy = 3;

    ASSERT_TRUE(sigurdos::prefs_save(saved));
    sigurdos::NodePrefs loaded;
    loaded.set_defaults();
    ASSERT_TRUE(sigurdos::prefs_load(loaded));
    EXPECT_EQ(86400u, loaded.gps_interval);
    EXPECT_EQ(7, loaded.multi_acks);
    EXPECT_EQ(3, loaded.advert_loc_policy);
}

TEST(PrefsValidationTest, InvalidConfiguredRadioFallsBackToReceiveDisabledDefaults) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    prefs.configured = true;
    prefs.freq = std::numeric_limits<float>::quiet_NaN();
    prefs.bw = 0.0f;
    prefs.sf = 255;
    prefs.cr = 0;
    prefs.tx_power_dbm = -20;

    EXPECT_FALSE(sigurdos::detail::normalizeAndValidate(prefs));
    EXPECT_FALSE(prefs.configured);
    EXPECT_FLOAT_EQ(prefs.freq, 0.0f);
    EXPECT_FLOAT_EQ(prefs.bw, 0.0f);
    EXPECT_EQ(prefs.sf, 0);
    EXPECT_EQ(prefs.cr, 0);
    EXPECT_EQ(prefs.tx_power_dbm, 0);
}

TEST(PrefsValidationTest, ValidBoundariesSurviveAndTimingCorruptionIsNormalized) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    prefs.configured = true;
    prefs.freq = 400.0f;
    prefs.bw = 7.8f;
    prefs.sf = 6;
    prefs.cr = 5;
    prefs.tx_power_dbm = 2;
    prefs.rx_delay_base = std::numeric_limits<float>::infinity();
    prefs.tx_delay_factor = -1.0f;
    prefs.direct_tx_delay_factor = std::numeric_limits<float>::quiet_NaN();

    EXPECT_TRUE(sigurdos::detail::normalizeAndValidate(prefs));
    EXPECT_FLOAT_EQ(prefs.rx_delay_base, 10.0f);
    EXPECT_FLOAT_EQ(prefs.tx_delay_factor, 1.0f);
    EXPECT_FLOAT_EQ(prefs.direct_tx_delay_factor, 1.0f);
}

TEST(PrefsValidationTest, RepeaterPasswordSchemaMatchesReadBuffers) {
    const std::string max_name(31, 'n');
    const std::string too_long_name(32, 'n');
    const std::string max_password(63, 'p');
    const std::string too_long_password(64, 'p');

    EXPECT_TRUE(sigurdos::detail::repeaterPasswordFitsSchema(
        max_name.c_str(), max_password.c_str()));
    EXPECT_FALSE(sigurdos::detail::repeaterPasswordFitsSchema(
        too_long_name.c_str(), "password"));
    EXPECT_FALSE(sigurdos::detail::repeaterPasswordFitsSchema(
        "repeater", too_long_password.c_str()));
    EXPECT_FALSE(sigurdos::detail::repeaterPasswordFitsSchema("", "password"));
    EXPECT_FALSE(sigurdos::detail::repeaterPasswordFitsSchema("repeater", ""));
}

TEST(BlePrefsMigrationTest, FreshInstallUsesDiscoverableDefault) {
    const auto state = sigurdos::detail::resolveBlePrefs(
        false, false, false, false, 0);

    EXPECT_TRUE(state.enabled);
    EXPECT_FALSE(state.user_set);
    EXPECT_TRUE(state.needs_migration);
}

TEST(BlePrefsMigrationTest, LegacyFalseWithoutIntentMigratesToEnabled) {
    const auto state = sigurdos::detail::resolveBlePrefs(
        true, false, false, false, 0);

    EXPECT_TRUE(state.enabled);
    EXPECT_FALSE(state.user_set);
    EXPECT_TRUE(state.needs_migration);
}

TEST(BlePrefsMigrationTest, LegacyExplicitDisableIsPreservedAndVersioned) {
    const auto state = sigurdos::detail::resolveBlePrefs(
        true, false, true, true, 0);

    EXPECT_FALSE(state.enabled);
    EXPECT_TRUE(state.user_set);
    EXPECT_TRUE(state.needs_migration);
}

TEST(BlePrefsMigrationTest, SubsequentBootPreservesExplicitDisable) {
    const auto state = sigurdos::detail::resolveBlePrefs(
        true, false, true, true, sigurdos::detail::BLE_PREFS_SCHEMA_VERSION);

    EXPECT_FALSE(state.enabled);
    EXPECT_TRUE(state.user_set);
    EXPECT_FALSE(state.needs_migration);
}

struct FailingNvsWriter {
    int fail_at = -1;
    int write_calls = 0;
    int commit_calls = 0;
    int ble_write_calls = 0;
    int32_t error = -42;
    bool fail_commit = false;

    int32_t write(const char* key) {
        if (strcmp(key, "ble_en") == 0 || strcmp(key, "ble_user") == 0 ||
            strcmp(key, "ble_ver") == 0) {
            ble_write_calls++;
        }
        const int call = write_calls++;
        return call == fail_at ? error : 0;
    }

    static int32_t setI8(void* raw, const char* key, int8_t) {
        return static_cast<FailingNvsWriter*>(raw)->write(key);
    }
    static int32_t setU8(void* raw, const char* key, uint8_t) {
        return static_cast<FailingNvsWriter*>(raw)->write(key);
    }
    static int32_t setU16(void* raw, const char* key, uint16_t) {
        return static_cast<FailingNvsWriter*>(raw)->write(key);
    }
    static int32_t setI32(void* raw, const char* key, int32_t) {
        return static_cast<FailingNvsWriter*>(raw)->write(key);
    }
    static int32_t setU32(void* raw, const char* key, uint32_t) {
        return static_cast<FailingNvsWriter*>(raw)->write(key);
    }
    static int32_t setBlob(void* raw, const char* key, const void*, size_t) {
        return static_cast<FailingNvsWriter*>(raw)->write(key);
    }
    static int32_t setString(void* raw, const char* key, const char*) {
        return static_cast<FailingNvsWriter*>(raw)->write(key);
    }
    static int32_t commit(void* raw) {
        auto* self = static_cast<FailingNvsWriter*>(raw);
        self->commit_calls++;
        return self->fail_commit ? self->error : 0;
    }

    sigurdos::detail::PrefsNvsWriter ops() {
        return {this, setI8, setU8, setU16, setI32, setU32, setBlob, setString, commit};
    }
};

TEST(PrefsWritePolicyTest, EveryNvsSetFailureIsReturnedWithoutCommit) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();

    FailingNvsWriter successful;
    sigurdos::detail::PrefsWriteFailure failure;
    ASSERT_TRUE(sigurdos::detail::prefsWriteAll(
        prefs, successful.ops(), sigurdos::detail::BlePrefsWriteMode::Write, &failure));
    ASSERT_EQ(48, successful.write_calls);
    ASSERT_EQ(3, successful.ble_write_calls);
    ASSERT_EQ(1, successful.commit_calls);

    for (int fail_at = 0; fail_at < successful.write_calls; ++fail_at) {
        FailingNvsWriter failing;
        failing.fail_at = fail_at;
        failure = {};

        EXPECT_FALSE(sigurdos::detail::prefsWriteAll(
            prefs, failing.ops(), sigurdos::detail::BlePrefsWriteMode::Write, &failure))
            << "write index " << fail_at;
        EXPECT_EQ(failing.error, failure.error) << "write index " << fail_at;
        EXPECT_NE(nullptr, failure.key) << "write index " << fail_at;
        EXPECT_EQ(0, failing.commit_calls) << "write index " << fail_at;
    }
}

TEST(PrefsWritePolicyTest, CommitFailureIsReturned) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    FailingNvsWriter failing;
    failing.fail_commit = true;
    sigurdos::detail::PrefsWriteFailure failure;

    EXPECT_FALSE(sigurdos::detail::prefsWriteAll(
        prefs, failing.ops(), sigurdos::detail::BlePrefsWriteMode::Write, &failure));
    EXPECT_EQ(48, failing.write_calls);
    EXPECT_EQ(1, failing.commit_calls);
    EXPECT_STREQ("commit", failure.key);
    EXPECT_EQ(failing.error, failure.error);
}

TEST(PrefsWritePolicyTest, PreserveModeLeavesCrossVariantBleKeysUntouched) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    prefs.ble_enabled = false;
    prefs.ble_user_set = true;
    FailingNvsWriter writer;

    EXPECT_TRUE(sigurdos::detail::prefsWriteAll(
        prefs, writer.ops(), sigurdos::detail::BlePrefsWriteMode::Preserve));
    EXPECT_EQ(45, writer.write_calls);
    EXPECT_EQ(0, writer.ble_write_calls);
    EXPECT_EQ(1, writer.commit_calls);
}

} // namespace
