// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

#include "hal/boot_watchdog.h"
#include "hal/github_ota.h"
#include "hal/ota_runtime_policy.h"
#include "hal/wifi_ota.h"

namespace {

using namespace sigurdos::hal;

std::string readProjectFile(const char* path) {
    const char* prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    for (const char* prefix : prefixes) {
        std::ifstream in(std::string(prefix) + path);
        if (in.good()) {
            return std::string(std::istreambuf_iterator<char>(in), {});
        }
    }
    return {};
}

TEST(OtaRuntimePolicyTest, LoopTaskAloneOwnsProductionWatchdog) {
    EXPECT_EQ(RUNTIME_WATCHDOG_OWNER, RuntimeWatchdogOwner::LoopTask);
    EXPECT_EQ(RUNTIME_WATCHDOG_TIMEOUT_SEC, 10U);
    EXPECT_TRUE(OTA_WORKER_OWNS_STARTUP);
    EXPECT_TRUE(OTA_WORKER_OWNS_TRANSPORT_AND_FLASH);
    EXPECT_FALSE(OTA_WORKER_SUBSCRIBES_RUNTIME_WATCHDOG);
}

TEST(OtaRuntimePolicyTest, LocalOtaStartStatusHasExplicitTerminalStates) {
    using sigurdos::ota::StartStatus;
    using sigurdos::ota::otaStartFinished;
    using sigurdos::ota::otaStartSucceeded;

    EXPECT_FALSE(otaStartFinished(StartStatus::Idle));
    EXPECT_FALSE(otaStartFinished(StartStatus::Starting));
    EXPECT_TRUE(otaStartFinished(StartStatus::Ready));
    EXPECT_TRUE(otaStartFinished(StartStatus::Failed));
    EXPECT_TRUE(otaStartSucceeded(StartStatus::Ready));
    EXPECT_FALSE(otaStartSucceeded(StartStatus::Failed));
}

TEST(OtaRuntimePolicyTest, LocalOtaQueuesAllHardwareStartupOnWorker) {
    const std::string source = readProjectFile("src/hal/wifi_ota.cpp");
    ASSERT_FALSE(source.empty());

    const size_t worker_pos = source.find("static void otaServerWorker(void*)");
    const size_t worker_start_pos = source.find(
        "startSession(startup_ssid, startup_password, startup_reuse_sta)",
        worker_pos);
    const size_t session_pos = source.find(
        "static bool startSession(const char* ssid, const char* password,\n"
        "                         bool reuse_sta) {");
    const size_t public_start_pos = source.find(
        "bool start(const char* ssid, const char* password) {", session_pos);
    const size_t task_create_pos = source.find(
        "xTaskCreatePinnedToCore(", public_start_pos);
    const size_t public_start_end = source.find("\nvoid loop()", public_start_pos);

    ASSERT_NE(worker_pos, std::string::npos);
    ASSERT_NE(worker_start_pos, std::string::npos);
    ASSERT_NE(session_pos, std::string::npos);
    ASSERT_NE(public_start_pos, std::string::npos);
    ASSERT_NE(task_create_pos, std::string::npos);
    ASSERT_NE(public_start_end, std::string::npos);
    EXPECT_LT(worker_pos, worker_start_pos);
    EXPECT_LT(worker_start_pos, session_pos);
    EXPECT_LT(session_pos, public_start_pos);
    EXPECT_LT(public_start_pos, task_create_pos);

    const std::string public_start = source.substr(
        public_start_pos, public_start_end - public_start_pos);
    EXPECT_NE(public_start.find("wifi::reserveForWorker("), std::string::npos);
    EXPECT_EQ(public_start.find("startAccessPoint("), std::string::npos);
    EXPECT_EQ(public_start.find("server->begin()"), std::string::npos);
}

TEST(OtaRuntimePolicyTest, LocalOtaDialogPollsWorkerStartupState) {
    const std::string source = readProjectFile(
        "src/ui/screens/screen_settings_system.cpp");
    ASSERT_FALSE(source.empty());

    const size_t start_pos = source.find(
        "sigurdos::ota::start(\"SigurdOS-OTA\")");
    const size_t status_pos = source.find(
        "sigurdos::ota::getStartStatus()", start_pos);
    const size_t timer_pos = source.find(
        "ota_ctx->poll_timer.attach(lv_timer_create", start_pos);

    ASSERT_NE(start_pos, std::string::npos);
    ASSERT_NE(status_pos, std::string::npos);
    ASSERT_NE(timer_pos, std::string::npos);
    EXPECT_LT(start_pos, status_pos);
    EXPECT_LT(status_pos, timer_pos);
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

TEST(OtaRuntimePolicyTest, RebootWaitsForWorkerCleanup) {
    EXPECT_FALSE(otaRebootMayFinalize(false, false));
    EXPECT_FALSE(otaRebootMayFinalize(true, true));
    EXPECT_TRUE(otaRebootMayFinalize(true, false));
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
