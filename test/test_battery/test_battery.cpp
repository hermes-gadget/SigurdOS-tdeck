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
 * Unit tests for battery HAL conversion helpers.
 * Tests: mV to percentage conversion, ADC raw to mV conversion, edge cases,
 *        clamping, monotonicity, and mock ADC reads.
 */
#include <gtest/gtest.h>
#include "Arduino.h"
#include "hal/battery.h"

namespace {

class BatteryTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
    }
};

TEST_F(BatteryTest, FullCharge_4200mV_Returns100) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(4200), 100);
}

TEST_F(BatteryTest, EmptyCharge_3000mV_Returns0) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(3000), 0);
}

// Li-ion lookup table: 3600mV is between 3610mV(10%) and 3570mV(7%)
// (3610-3600)=10, range=40mV, range_pct=3 → offset=10/40*3=0 → 10%
TEST_F(BatteryTest, Around3600mV_ReturnsApprox10) {
    uint8_t pct = sigurdos_battery_pct_from_mv(3600);
    EXPECT_GE(pct, 9);
    EXPECT_LE(pct, 11);
}

// At exactly 3300mV (lowest table entry), returns 0
TEST_F(BatteryTest, At3300mV_Returns0) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(3300), 0);
}

// At exactly 3900mV (table entry), returns 55
TEST_F(BatteryTest, At3900mV_Returns55) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(3900), 55);
}

// 3700mV is between 3680mV(20%) and 3710mV(25%)
TEST_F(BatteryTest, NominalLiPo_3700mV_ReturnsApprox23) {
    uint8_t pct = sigurdos_battery_pct_from_mv(3700);
    EXPECT_GE(pct, 22);
    EXPECT_LE(pct, 24);
}

TEST_F(BatteryTest, OverVoltage_4300mV_ClampedTo100) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(4300), 100);
}

TEST_F(BatteryTest, OverVoltage_5000mV_ClampedTo100) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(5000), 100);
}

TEST_F(BatteryTest, UnderVoltage_2500mV_ClampedTo0) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(2500), 0);
}

TEST_F(BatteryTest, UnderVoltage_0mV_ClampedTo0) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(0), 0);
}

// 3001mV is below 3300mV (lowest table entry), returns 0
TEST_F(BatteryTest, JustAboveMin_3001mV_Returns0) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(3001), 0);
}

// 4199mV is between 4150mV(95%) and 4200mV(100%)
// (4200-4199) = 1, range=50mV, range_pct=5 → offset=1*5/50 = 0 → 100%
TEST_F(BatteryTest, JustBelowMax_4199mV_ReturnsApprox100) {
    uint8_t pct = sigurdos_battery_pct_from_mv(4199);
    EXPECT_GE(pct, 99);
    EXPECT_LE(pct, 100);
}

TEST_F(BatteryTest, PercentageIncreasesMonotonically) {
    uint8_t prev = 0;
    for (uint16_t mv = 3300; mv <= 4200; mv += 10) {
        uint8_t curr = sigurdos_battery_pct_from_mv(mv);
        EXPECT_GE(curr, prev) << "Non-monotonic at " << mv << "mV";
        prev = curr;
    }
}

TEST_F(BatteryTest, AllValuesInRange) {
    for (uint16_t mv = 0; mv < 6000; mv += 50) {
        uint8_t pct = sigurdos_battery_pct_from_mv(mv);
        EXPECT_GE(pct, 0);
        EXPECT_LE(pct, 100);
    }
}

TEST_F(BatteryTest, ADCRawToMillivolts) {
    EXPECT_EQ(sigurdos_battery_mv_from_adc_raw(0), 0);
    EXPECT_NEAR(sigurdos_battery_mv_from_adc_raw(2048), 3300, 1);
    EXPECT_NEAR(sigurdos_battery_mv_from_adc_raw(4095), 6598, 5);
}

TEST_F(BatteryTest, AnalogReadMilliVoltsIntegration) {
    arduino_mock::analog_values[PIN_BAT_ADC] = 2048;
    uint16_t mv = sigurdos_battery_mv();
    EXPECT_NEAR(mv, 3300, 10);
}

TEST_F(BatteryTest, AnalogReadMilliVoltsFullScale) {
    arduino_mock::analog_values[PIN_BAT_ADC] = 4095;
    uint16_t mv = sigurdos_battery_mv();
    EXPECT_NEAR(mv, 6600, 10);
}

TEST_F(BatteryTest, AnalogReadMilliVoltsZero) {
    arduino_mock::analog_values[PIN_BAT_ADC] = 0;
    uint16_t mv = sigurdos_battery_mv();
    EXPECT_EQ(mv, 0);
}

TEST_F(BatteryTest, AnalogReadMilliVoltsDischarged) {
    arduino_mock::analog_values[PIN_BAT_ADC] = 1820;
    uint16_t mv = sigurdos_battery_mv();
    EXPECT_NEAR(mv, 2930, 10);
    uint8_t pct = sigurdos_battery_pct();
    EXPECT_EQ(pct, 0);
}

TEST_F(BatteryTest, AnalogReadMilliVoltsCharged) {
    arduino_mock::analog_values[PIN_BAT_ADC] = 2606;
    uint16_t mv = sigurdos_battery_mv();
    EXPECT_NEAR(mv, 4200, 10);
    uint8_t pct = sigurdos_battery_pct();
    EXPECT_EQ(pct, 100);
}

} // anonymous namespace
