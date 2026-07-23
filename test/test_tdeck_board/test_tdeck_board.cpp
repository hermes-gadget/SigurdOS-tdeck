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

#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "hal/tdeck_board.h"

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

class TDeckBoardPowerTest : public ::testing::Test {};

TEST_F(TDeckBoardPowerTest, AutoShutdownThresholdIs3200mV) {
    EXPECT_EQ(sigurdos::SIGURDOS_TDECK_AUTO_SHUTDOWN_MV, 3200u);
}

TEST_F(TDeckBoardPowerTest, ZeroVoltageReadingIsNotCritical) {
    EXPECT_FALSE(sigurdos::tdeck_battery_mv_is_critical(0));
}

TEST_F(TDeckBoardPowerTest, BelowThresholdVoltageIsCritical) {
    EXPECT_TRUE(sigurdos::tdeck_battery_mv_is_critical(1));
    EXPECT_TRUE(sigurdos::tdeck_battery_mv_is_critical(3199));
}

TEST_F(TDeckBoardPowerTest, ThresholdVoltageIsNotCritical) {
    EXPECT_FALSE(sigurdos::tdeck_battery_mv_is_critical(3200));
}

TEST_F(TDeckBoardPowerTest, NominalBatteryVoltageIsNotCritical) {
    EXPECT_FALSE(sigurdos::tdeck_battery_mv_is_critical(3700));
    EXPECT_FALSE(sigurdos::tdeck_battery_mv_is_critical(UINT16_MAX));
}

TEST_F(TDeckBoardPowerTest, CriticalTimerWakeResleepsBeforeFullBoot) {
    EXPECT_TRUE(sigurdos::tdeck_should_resleep_early(true, true, 3199));
}

TEST_F(TDeckBoardPowerTest, IndefiniteSleepUsesFifteenMinuteSafetyWake) {
    EXPECT_EQ(sigurdos::tdeck_sleep_wake_seconds(0), 900u);
    EXPECT_EQ(sigurdos::tdeck_sleep_wake_seconds(42), 42u);
}

TEST_F(TDeckBoardPowerTest, WakeConfigurationErrorsAreRecognized) {
    EXPECT_TRUE(sigurdos::tdeck_wake_configuration_succeeded(0));
    EXPECT_FALSE(sigurdos::tdeck_wake_configuration_succeeded(-1));
    EXPECT_FALSE(sigurdos::tdeck_wake_configuration_succeeded(0x102));
}

TEST_F(TDeckBoardPowerTest, SleepPreflightReportsInhibitionAndWakeFailure) {
    using sigurdos::TDeckSleepStatus;

    EXPECT_EQ(sigurdos::tdeck_sleep_preflight(true, 0),
              TDeckSleepStatus::Inhibited);
    EXPECT_EQ(sigurdos::tdeck_sleep_preflight(false, -1),
              TDeckSleepStatus::WakeConfigurationFailed);
    EXPECT_EQ(sigurdos::tdeck_sleep_preflight(false, 0),
              TDeckSleepStatus::Ready);
    EXPECT_STREQ(sigurdos::tdeck_sleep_status_name(
                     TDeckSleepStatus::WakeConfigurationFailed),
                 "wake configuration failed");
    EXPECT_STREQ(sigurdos::tdeck_sleep_status_name(
                     TDeckSleepStatus::PeripheralRailHoldFailed),
                 "peripheral rail hold failed");
}

TEST_F(TDeckBoardPowerTest, PeripheralRailHoldIsReleasedWithoutGlitch) {
    const std::string source = read_project_file("src/hal/tdeck_board.h");
    ASSERT_FALSE(source.empty());

    const size_t begin_pos = source.find("void begin()");
    const size_t active_pos = source.find(
        "digitalWrite(PIN_PERIPH_PWR, HIGH);", begin_pos);
    const size_t release_pos = source.find("gpio_hold_dis(", active_pos);
    const size_t release_global_pos = source.find(
        "gpio_deep_sleep_hold_dis();", release_pos);
    ASSERT_NE(begin_pos, std::string::npos);
    ASSERT_NE(active_pos, std::string::npos);
    ASSERT_NE(release_pos, std::string::npos);
    ASSERT_NE(release_global_pos, std::string::npos);
    EXPECT_LT(active_pos, release_pos);
    EXPECT_LT(release_pos, release_global_pos);
    EXPECT_EQ(source.find("ESP_PD_DOMAIN_RTC_PERIPH"), std::string::npos);
}

TEST_F(TDeckBoardPowerTest, OtherBootReasonsAndRecoveredBatteryContinue) {
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(false, true, 3199));
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(true, false, 3199));
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(true, true, 3200));
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(true, true, 0));
}

TEST_F(TDeckBoardPowerTest, TemperatureAveragePreservesFractionalSamples) {
    const float samples[] = {41.25f, 42.5f, 43.75f, 44.5f};
    EXPECT_FLOAT_EQ(sigurdos::tdeck_average_mcu_temperature(samples, 4),
                    43.0f);
}

TEST_F(TDeckBoardPowerTest, TemperatureAverageRejectsInvalidSamples) {
    const float samples[] = {
        -10.0f,
        30.0f,
        -100.0f,
        200.0f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
    };
    EXPECT_FLOAT_EQ(sigurdos::tdeck_average_mcu_temperature(samples, 6),
                    10.0f);
}

TEST_F(TDeckBoardPowerTest, TemperatureAverageReturnsZeroWithoutValidSamples) {
    const float samples[] = {
        -100.0f, std::numeric_limits<float>::quiet_NaN(), 200.0f};
    EXPECT_FLOAT_EQ(sigurdos::tdeck_average_mcu_temperature(samples, 3),
                    0.0f);
    EXPECT_FLOAT_EQ(sigurdos::tdeck_average_mcu_temperature(nullptr, 3),
                    0.0f);
}

} // namespace
