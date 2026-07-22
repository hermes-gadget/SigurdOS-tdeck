// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string read_project_file(const char* path)
{
    const char* prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    for (const char* prefix : prefixes) {
        std::ifstream in(std::string(prefix) + path);
        if (in.good()) {
            return std::string(std::istreambuf_iterator<char>(in), {});
        }
    }
    return {};
}

TEST(MainLoopDispatchTest, ServicesRemoteTestTelemetryAndDebugAfterGpsWork)
{
    const std::string source = read_project_file("src/main.cpp");
    ASSERT_FALSE(source.empty());

    const size_t gps_pos = source.find("sigurdos_gps_mark_time_synced();");
    const size_t mesh_pos = source.find("sigurdos::mesh::loop();");
    const size_t controller_pos = source.find("sigurdos_test_controller_loop();");
    const size_t telemetry_pos = source.find("sigurdos::telemetry::loop();");
    const size_t timing_pos = source.find(
        "sigurdos::telemetry::report_loop_timing(loop_elapsed_us);");
    const size_t debug_pos = source.find("sigurdos::debug::loop();");

    ASSERT_NE(gps_pos, std::string::npos);
    ASSERT_NE(controller_pos, std::string::npos);
    ASSERT_NE(telemetry_pos, std::string::npos);
    ASSERT_NE(timing_pos, std::string::npos);
    ASSERT_NE(debug_pos, std::string::npos);

    EXPECT_LT(gps_pos, controller_pos);
    if (mesh_pos != std::string::npos) {
        EXPECT_LT(mesh_pos, controller_pos);
    }
    EXPECT_LT(controller_pos, telemetry_pos);
    EXPECT_LT(telemetry_pos, timing_pos);
    EXPECT_LT(timing_pos, debug_pos);
}

TEST(MainLoopDispatchTest, IntegratesBootHealthAndValidatedDisplayRetryInOrder)
{
    const std::string source = read_project_file("src/main.cpp");
    ASSERT_FALSE(source.empty());

    const size_t reset_pos = source.find(
        "const esp_reset_reason_t reset_reason = esp_reset_reason();");
    const size_t health_begin_pos = source.find(
        "sigurdos::ota_boot_health::begin();");
    const size_t board_pos = source.find("board.begin();");
    const size_t retry_begin_pos = source.find(
        "sigurdos::hal::display_retry_begin(");
    const size_t display_pos = source.find("sigurdos_display_init()");
    const size_t retry_failure_pos = source.find(
        "sigurdos::hal::display_retry_note_failure(");
    const size_t rollback_pos = source.find(
        "sigurdos::ota_boot_health::rollbackIfPending(");
    const size_t retry_clear_pos = source.find(
        "sigurdos::hal::display_retry_clear(");
    const size_t chats_pos = source.find(
        "sigurdos::ui::load_persisted_state();");
    const size_t core_ready_pos = source.find(
        "sigurdos::ota_boot_health::markCoreReady();");
    const size_t runtime_pos = source.find(
        "sigurdos::hal::boot_watchdog_enter_runtime();");
    const size_t health_loop_pos = source.find(
        "sigurdos::ota_boot_health::loop();");
    const size_t runtime_progress_pos = source.find(
        "sigurdos::hal::boot_watchdog_runtime_progress();");

    ASSERT_NE(reset_pos, std::string::npos);
    ASSERT_NE(health_begin_pos, std::string::npos);
    ASSERT_NE(board_pos, std::string::npos);
    ASSERT_NE(retry_begin_pos, std::string::npos);
    ASSERT_NE(display_pos, std::string::npos);
    ASSERT_NE(retry_failure_pos, std::string::npos);
    ASSERT_NE(rollback_pos, std::string::npos);
    ASSERT_NE(retry_clear_pos, std::string::npos);
    ASSERT_NE(chats_pos, std::string::npos);
    ASSERT_NE(core_ready_pos, std::string::npos);
    ASSERT_NE(runtime_pos, std::string::npos);
    ASSERT_NE(health_loop_pos, std::string::npos);
    ASSERT_NE(runtime_progress_pos, std::string::npos);

    EXPECT_LT(reset_pos, health_begin_pos);
    EXPECT_LT(health_begin_pos, board_pos);
    EXPECT_LT(board_pos, retry_begin_pos);
    EXPECT_LT(retry_begin_pos, display_pos);
    EXPECT_LT(display_pos, retry_failure_pos);
    EXPECT_LT(retry_failure_pos, rollback_pos);
    EXPECT_LT(rollback_pos, retry_clear_pos);
    EXPECT_LT(retry_clear_pos, chats_pos);
    EXPECT_LT(chats_pos, core_ready_pos);
    EXPECT_LT(core_ready_pos, runtime_pos);
    EXPECT_LT(runtime_pos, health_loop_pos);
    EXPECT_LT(health_loop_pos, runtime_progress_pos);
}

TEST(MainLoopDispatchTest, EveryCriticalBatteryPathUsesOrderlyShutdown)
{
    const std::string source = read_project_file("src/main.cpp");
    ASSERT_FALSE(source.empty());

    const size_t helper_pos = source.find(
        "[[noreturn]] static void enter_orderly_sleep()");
    const size_t shutdown_pos = source.find(
        "sigurdos::mesh::shutdown();", helper_pos);
    const size_t early_guard_pos = source.find(
        "if (sigurdos::tdeck_should_resleep_early(");
    const size_t retry_call_pos = source.find(
        "enter_orderly_sleep();", early_guard_pos);
    const size_t input_init_pos = source.find(
        "sigurdos_display_init_inputs();");

    ASSERT_NE(helper_pos, std::string::npos);
    ASSERT_NE(shutdown_pos, std::string::npos);
    ASSERT_NE(early_guard_pos, std::string::npos);
    ASSERT_NE(retry_call_pos, std::string::npos);
    ASSERT_NE(input_init_pos, std::string::npos);
    EXPECT_LT(helper_pos, shutdown_pos);
    EXPECT_LT(shutdown_pos, early_guard_pos);
    EXPECT_LT(early_guard_pos, retry_call_pos);
    EXPECT_LT(retry_call_pos, input_init_pos);

    const std::string early_guard = source.substr(
        early_guard_pos, input_init_pos - early_guard_pos);
    EXPECT_EQ(early_guard.find("return;"), std::string::npos);

    const size_t runtime_guard_pos = source.find("board.isBatteryCritical()");
    const size_t runtime_sleep_pos = source.find(
        "enter_orderly_sleep();", runtime_guard_pos);
    ASSERT_NE(runtime_guard_pos, std::string::npos);
    ASSERT_NE(runtime_sleep_pos, std::string::npos);
    EXPECT_LT(runtime_guard_pos, runtime_sleep_pos);
    EXPECT_EQ(source.find("board.trySleep("), std::string::npos);
}

}  // anonymous namespace
