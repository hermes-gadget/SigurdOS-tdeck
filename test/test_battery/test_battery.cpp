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

TEST_F(BatteryTest, HalfCharge_3600mV_Returns50) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(3600), 50);
}

TEST_F(BatteryTest, QuarterCharge_3300mV_Returns25) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(3300), 25);
}

TEST_F(BatteryTest, ThreeQuarterCharge_3900mV_Returns75) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(3900), 75);
}

TEST_F(BatteryTest, NominalLiPo_3700mV_ReturnsApprox58) {
    uint8_t pct = sigurdos_battery_pct_from_mv(3700);
    EXPECT_GE(pct, 57);
    EXPECT_LE(pct, 59);
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

TEST_F(BatteryTest, JustAboveMin_3001mV_Returns0) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(3001), 0);
}

TEST_F(BatteryTest, JustBelowMax_4199mV_Returns99) {
    EXPECT_EQ(sigurdos_battery_pct_from_mv(4199), 99);
}

TEST_F(BatteryTest, PercentageIncreasesMonotonically) {
    uint8_t prev = 0;
    for (uint16_t mv = BAT_MIN_MV; mv <= BAT_MAX_MV; mv += 10) {
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
    // Set the mock value for the ADC pin; sigurdos_battery_mv() now uses
    // analogReadMilliVolts internally with efuse-calibrated conversion.
    // 2048 raw → mock calibrates to ~1650mV at pin → *2 (divider) → ~3300mV
    arduino_mock::analog_values[PIN_BAT_ADC] = 2048;
    uint16_t mv = sigurdos_battery_mv();
    EXPECT_NEAR(mv, 3300, 10);
}

TEST_F(BatteryTest, AnalogReadMilliVoltsFullScale) {
    // 4095 raw → mock calibrates to ~3300mV at pin → *2 → ~6600mV
    arduino_mock::analog_values[PIN_BAT_ADC] = 4095;
    uint16_t mv = sigurdos_battery_mv();
    EXPECT_NEAR(mv, 6600, 10);
}

TEST_F(BatteryTest, AnalogReadMilliVoltsZero) {
    // 0 raw → 0mV at pin → *2 → 0mV battery
    arduino_mock::analog_values[PIN_BAT_ADC] = 0;
    uint16_t mv = sigurdos_battery_mv();
    EXPECT_EQ(mv, 0);
}

TEST_F(BatteryTest, AnalogReadMilliVoltsDischarged) {
    // ~1820 raw → ~1465mV at pin → *2 → ~2930mV battery → ~0%
    arduino_mock::analog_values[PIN_BAT_ADC] = 1820;
    uint16_t mv = sigurdos_battery_mv();
    EXPECT_NEAR(mv, 2930, 10);
    uint8_t pct = sigurdos_battery_pct();
    EXPECT_EQ(pct, 0);
}

TEST_F(BatteryTest, AnalogReadMilliVoltsCharged) {
    // ~2606 raw → ~2100mV at pin → *2 → ~4200mV battery → 100%
    arduino_mock::analog_values[PIN_BAT_ADC] = 2606;
    uint16_t mv = sigurdos_battery_mv();
    EXPECT_NEAR(mv, 4200, 10);
    uint8_t pct = sigurdos_battery_pct();
    EXPECT_EQ(pct, 100);
}

} // anonymous namespace
