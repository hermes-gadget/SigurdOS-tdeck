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

#include <gtest/gtest.h>

#include "hal/tdeck_board.h"

namespace {

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

TEST_F(TDeckBoardPowerTest, OtherBootReasonsAndRecoveredBatteryContinue) {
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(false, true, 3199));
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(true, false, 3199));
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(true, true, 3200));
    EXPECT_FALSE(sigurdos::tdeck_should_resleep_early(true, true, 0));
}

} // namespace
