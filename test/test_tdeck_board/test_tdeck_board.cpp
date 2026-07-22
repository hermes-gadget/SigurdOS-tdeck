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

TEST_F(TDeckBoardPowerTest, PeripheralRailIsLatchedLowAndReleasedWithoutGlitch) {
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

    const size_t sleep_pos = source.find("TDeckSleepStatus trySleep(");
    const size_t low_pos = source.find(
        "digitalWrite(PIN_PERIPH_PWR, LOW);", sleep_pos);
    const size_t hold_pos = source.find("gpio_hold_en(", low_pos);
    const size_t deep_hold_pos = source.find(
        "gpio_deep_sleep_hold_en();", hold_pos);
    const size_t enter_pos = source.find("enterDeepSleep();", deep_hold_pos);
    ASSERT_NE(sleep_pos, std::string::npos);
    ASSERT_NE(low_pos, std::string::npos);
    ASSERT_NE(hold_pos, std::string::npos);
    ASSERT_NE(deep_hold_pos, std::string::npos);
    ASSERT_NE(enter_pos, std::string::npos);
    EXPECT_LT(low_pos, hold_pos);
    EXPECT_LT(hold_pos, deep_hold_pos);
    EXPECT_LT(deep_hold_pos, enter_pos);

    EXPECT_EQ(source.find("ESP_PD_DOMAIN_RTC_PERIPH"), std::string::npos);
}

TEST_F(TDeckBoardPowerTest, OtherBootReasonsAndRecoveredBatteryContinue) {
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(false, true, 3199));
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(true, false, 3199));
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(true, true, 3200));
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(true, true, 0));
}

} // namespace
