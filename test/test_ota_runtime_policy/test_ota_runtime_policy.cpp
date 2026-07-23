// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "hal/boot_watchdog.h"
#include "hal/github_ota.h"
#include "hal/ota_runtime_policy.h"

namespace {

using namespace sigurdos::hal;

TEST(OtaRuntimePolicyTest, LoopTaskAloneOwnsProductionWatchdog) {
    EXPECT_EQ(RUNTIME_WATCHDOG_OWNER, RuntimeWatchdogOwner::LoopTask);
    EXPECT_EQ(RUNTIME_WATCHDOG_TIMEOUT_SEC, 10U);
    EXPECT_TRUE(OTA_WORKER_OWNS_TRANSPORT_AND_FLASH);
    EXPECT_FALSE(OTA_WORKER_SUBSCRIBES_RUNTIME_WATCHDOG);
}

TEST(OtaRuntimePolicyTest, SlowResponseUsesWrapSafeLastByteDeadline) {
    using sigurdos::github_ota::githubOtaDownloadIdleTimedOut;
    using sigurdos::github_ota::GITHUB_OTA_DOWNLOAD_IDLE_TIMEOUT_MS;

    uint32_t last_byte_at = 100;
    for (uint32_t now = 1000;
         now < GITHUB_OTA_DOWNLOAD_IDLE_TIMEOUT_MS * 3U;
         now += GITHUB_OTA_DOWNLOAD_IDLE_TIMEOUT_MS - 1U) {
        EXPECT_FALSE(githubOtaDownloadIdleTimedOut(last_byte_at, now));
        last_byte_at = now;
    }
    EXPECT_TRUE(githubOtaDownloadIdleTimedOut(
        last_byte_at, last_byte_at + GITHUB_OTA_DOWNLOAD_IDLE_TIMEOUT_MS));
}

TEST(OtaRuntimePolicyTest, ConnectedClientWithoutBodyCannotOwnLoopTask) {
    EXPECT_TRUE(OTA_WORKER_OWNS_TRANSPORT_AND_FLASH);
    EXPECT_GE(OTA_WORKER_STACK_BYTES, 8U * 1024U);
    EXPECT_NE(OTA_WORKER_CORE, 1);
}

TEST(OtaRuntimePolicyTest, ThrottledSixMegabyteUploadUsesBoundedWorkerSlices) {
    size_t remaining = OTA_MAX_IMAGE_BYTES;
    size_t slices = 0;
    uint32_t elapsed_ms = 0;
    while (remaining > 0) {
        const size_t slice = otaTransferReadLimit(
            remaining, remaining, OTA_TRANSFER_SLICE_BYTES, remaining);
        ASSERT_GT(slice, 0U);
        EXPECT_LE(slice, OTA_TRANSFER_SLICE_BYTES);
        remaining -= slice;
        ++slices;
        elapsed_ms += 250U;  // representative throttled peer cadence
    }

    EXPECT_EQ(slices,
              OTA_MAX_IMAGE_BYTES / OTA_TRANSFER_SLICE_BYTES);
    EXPECT_GT(slices, 1U);
    EXPECT_GT(elapsed_ms, RUNTIME_WATCHDOG_TIMEOUT_SEC * 1000U);
    EXPECT_TRUE(OTA_WORKER_OWNS_TRANSPORT_AND_FLASH);
}

TEST(OtaRuntimePolicyTest, TransferSliceHasByteAndTimeBudgets) {
    EXPECT_FALSE(otaTransferSliceExhausted(
        OTA_TRANSFER_SLICE_BYTES - 1U, 100U,
        100U + OTA_TRANSFER_SLICE_MS - 1U));
    EXPECT_TRUE(otaTransferSliceExhausted(
        OTA_TRANSFER_SLICE_BYTES, 100U, 100U));
    EXPECT_TRUE(otaTransferSliceExhausted(
        0U, 100U, 100U + OTA_TRANSFER_SLICE_MS));
    EXPECT_TRUE(otaTransferSliceExhausted(
        0U, 0xFFFFFFFCU, OTA_TRANSFER_SLICE_MS - 4U));
}

}  // namespace
