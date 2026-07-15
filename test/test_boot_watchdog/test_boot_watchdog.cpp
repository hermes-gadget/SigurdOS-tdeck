// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "hal/boot_watchdog.h"

namespace {

using sigurdos::hal::BOOT_WATCHDOG_MAGIC;
using sigurdos::hal::BOOT_WATCHDOG_VERSION;
using sigurdos::hal::BootStage;
using sigurdos::hal::BootWatchdogRecord;
using sigurdos::hal::RUNTIME_WATCHDOG_TIMEOUT_SEC;
using sigurdos::hal::SETUP_WATCHDOG_TIMEOUT_SEC;
using sigurdos::hal::boot_watchdog_previous_stall;
using sigurdos::hal::boot_watchdog_record_clear;
using sigurdos::hal::boot_watchdog_record_progress;
using sigurdos::hal::boot_watchdog_record_valid;
using sigurdos::hal::boot_watchdog_would_expire;

TEST(BootWatchdogPolicyTest, SetupWindowIsRealisticAndRuntimeIsTighter) {
    EXPECT_EQ(SETUP_WATCHDOG_TIMEOUT_SEC, 60u);
    EXPECT_EQ(RUNTIME_WATCHDOG_TIMEOUT_SEC, 10u);
    EXPECT_GT(SETUP_WATCHDOG_TIMEOUT_SEC, RUNTIME_WATCHDOG_TIMEOUT_SEC);
}

TEST(BootWatchdogPolicyTest, ProgressRecordCarriesStageSequenceAndTime) {
    BootWatchdogRecord record{};
    boot_watchdog_record_clear(record);

    boot_watchdog_record_progress(record, BootStage::Storage, 1234);
    boot_watchdog_record_progress(record, BootStage::Radio, 5678);

    EXPECT_TRUE(boot_watchdog_record_valid(record));
    EXPECT_EQ(record.magic, BOOT_WATCHDOG_MAGIC);
    EXPECT_EQ(record.version, BOOT_WATCHDOG_VERSION);
    EXPECT_EQ(record.stage, static_cast<uint8_t>(BootStage::Radio));
    EXPECT_EQ(record.progress_count, 2);
    EXPECT_EQ(record.last_progress_ms, 5678u);
}

TEST(BootWatchdogPolicyTest, PreviousTaskWatchdogResetIdentifiesStage) {
    BootWatchdogRecord retained{};
    boot_watchdog_record_clear(retained);
    boot_watchdog_record_progress(retained, BootStage::MapDiscovery, 4242);
    BootWatchdogRecord snapshot{};

    ASSERT_TRUE(boot_watchdog_previous_stall(retained, true, &snapshot));
    EXPECT_EQ(snapshot.stage,
              static_cast<uint8_t>(BootStage::MapDiscovery));
    EXPECT_EQ(snapshot.last_progress_ms, 4242u);
}

TEST(BootWatchdogPolicyTest, OtherResetReasonsDoNotReportStaleStage) {
    BootWatchdogRecord retained{};
    boot_watchdog_record_clear(retained);
    boot_watchdog_record_progress(retained, BootStage::Display, 100);
    BootWatchdogRecord snapshot{};

    EXPECT_FALSE(boot_watchdog_previous_stall(retained, false, &snapshot));
}

TEST(BootWatchdogPolicyTest, CorruptRetainedRecordIsRejected) {
    BootWatchdogRecord retained{};
    boot_watchdog_record_clear(retained);
    boot_watchdog_record_progress(retained, BootStage::SdCard, 100);
    BootWatchdogRecord snapshot{};

    retained.magic ^= 1;
    EXPECT_FALSE(boot_watchdog_previous_stall(retained, true, &snapshot));
    boot_watchdog_record_progress(retained, BootStage::SdCard, 100);
    retained.version++;
    EXPECT_FALSE(boot_watchdog_previous_stall(retained, true, &snapshot));
    retained.version = BOOT_WATCHDOG_VERSION;
    retained.stage = static_cast<uint8_t>(BootStage::Count);
    EXPECT_FALSE(boot_watchdog_previous_stall(retained, true, &snapshot));
}

TEST(BootWatchdogPolicyTest, SlowProgressAvoidsSetupExpiryButStallExpires) {
    uint32_t last_progress = 0;
    for (uint32_t now = 59000; now <= 177000; now += 59000) {
        EXPECT_FALSE(boot_watchdog_would_expire(
            last_progress, now, SETUP_WATCHDOG_TIMEOUT_SEC));
        last_progress = now;
    }

    EXPECT_TRUE(boot_watchdog_would_expire(
        last_progress, last_progress + SETUP_WATCHDOG_TIMEOUT_SEC * 1000u,
        SETUP_WATCHDOG_TIMEOUT_SEC));
}

TEST(BootWatchdogPolicyTest, ElapsedCheckHandlesMillisWrap) {
    const uint32_t last_progress = 0xFFFFFF00u;
    EXPECT_FALSE(boot_watchdog_would_expire(
        last_progress, 0x000000F0u, 1));
    EXPECT_TRUE(boot_watchdog_would_expire(
        last_progress, 0x00000400u, 1));
}

TEST(BootWatchdogPolicyTest, ProgressCounterSaturates) {
    BootWatchdogRecord record{};
    boot_watchdog_record_clear(record);
    record.progress_count = UINT16_MAX;

    boot_watchdog_record_progress(record, BootStage::Runtime, 1000);

    EXPECT_EQ(record.progress_count, UINT16_MAX);
    EXPECT_TRUE(boot_watchdog_record_valid(record));
}

}  // namespace
