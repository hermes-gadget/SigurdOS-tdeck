// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include <cstdint>
#include "Arduino.h"

#define SIGURDOS_TELEMETRY 1

#include "diagnostics/telemetry_input.cpp"

namespace sigurdos {
namespace telemetry {

static uint32_t s_emit_records = 0;

namespace tag {
const char PINS[] = "pins";
}

namespace key {
const char TYPE[] = "type";
}

void emit_tag(const char*) {}
void emit_kv(const char*, int32_t) {}
void emit_kv_u(const char*, uint32_t) {}
void emit_kv_s(const char*, const char*) {}
void emit_kv_f(const char*, float, int) {}
void emit_sep() {}
void emit_end() { s_emit_records++; }

uint32_t telemetry_input_test_emit_count()
{
    return s_emit_records;
}

}  // namespace telemetry
}  // namespace sigurdos

namespace {

using sigurdos::telemetry::input::report_trackball_event;
using sigurdos::telemetry::telemetry_input_test_emit_count;

TEST(TelemetryInputTest, InvalidTrackballDirectionsDoNotAdvanceSampling)
{
    arduino_mock::reset();

    for (int i = 0; i < 5; i++) {
        report_trackball_event(6);
    }
    EXPECT_EQ(telemetry_input_test_emit_count(), 0u);

    for (int i = 0; i < 4; i++) {
        report_trackball_event(1);
    }
    EXPECT_EQ(telemetry_input_test_emit_count(), 0u);

    report_trackball_event(1);
    EXPECT_EQ(telemetry_input_test_emit_count(), 1u);

    report_trackball_event(5);
    EXPECT_EQ(telemetry_input_test_emit_count(), 2u);
}

}  // anonymous namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
