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

}  // anonymous namespace
