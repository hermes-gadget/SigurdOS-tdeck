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

TEST_F(PrefsTest, GpsIntervalIsNormalizedWhenUpdatedAtRuntime) {
    sigurdos::NodePrefs prefs;
    prefs.set_defaults();
    prefs.gps_interval = 0;

    ASSERT_TRUE(sigurdos::prefs_set(prefs));

    EXPECT_EQ(5, sigurdos::prefs_get().gps_interval);
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
    ASSERT_EQ(47, successful.write_calls);
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
    EXPECT_EQ(47, failing.write_calls);
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
    EXPECT_EQ(44, writer.write_calls);
    EXPECT_EQ(0, writer.ble_write_calls);
    EXPECT_EQ(1, writer.commit_calls);
}

} // namespace
