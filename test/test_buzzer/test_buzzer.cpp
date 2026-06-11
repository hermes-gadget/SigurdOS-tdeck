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

#include <gtest/gtest.h>

#include <Arduino.h>

#include "hal/buzzer.h"
#include "hal/tdeck_pins.h"

namespace {

using sigurdos::hal::BuzzerPatternKind;
using sigurdos::hal::BuzzerPatternStep;
using sigurdos::hal::SIGURDOS_BUZZER_DOUBLE_GAP_MS;
using sigurdos::hal::SIGURDOS_BUZZER_DOUBLE_ON_MS;
using sigurdos::hal::SIGURDOS_BUZZER_SHORT_ON_MS;
using sigurdos::hal::sigurdos_buzzer_pattern;

class BuzzerPatternTest : public ::testing::Test {};

TEST_F(BuzzerPatternTest, ShortPatternMatchesNotificationContract) {
    std::size_t count = 0;
    const BuzzerPatternStep* pattern =
        sigurdos_buzzer_pattern(BuzzerPatternKind::Short, &count);

    ASSERT_NE(pattern, nullptr);
    ASSERT_EQ(count, 2U);
    EXPECT_TRUE(pattern[0].level_high);
    EXPECT_EQ(pattern[0].duration_ms, SIGURDOS_BUZZER_SHORT_ON_MS);
    EXPECT_FALSE(pattern[1].level_high);
    EXPECT_EQ(pattern[1].duration_ms, 0U);
}

TEST_F(BuzzerPatternTest, DoublePatternMatchesNotificationContract) {
    std::size_t count = 0;
    const BuzzerPatternStep* pattern =
        sigurdos_buzzer_pattern(BuzzerPatternKind::Double, &count);

    ASSERT_NE(pattern, nullptr);
    ASSERT_EQ(count, 4U);
    EXPECT_TRUE(pattern[0].level_high);
    EXPECT_EQ(pattern[0].duration_ms, SIGURDOS_BUZZER_DOUBLE_ON_MS);
    EXPECT_FALSE(pattern[1].level_high);
    EXPECT_EQ(pattern[1].duration_ms, SIGURDOS_BUZZER_DOUBLE_GAP_MS);
    EXPECT_TRUE(pattern[2].level_high);
    EXPECT_EQ(pattern[2].duration_ms, SIGURDOS_BUZZER_DOUBLE_ON_MS);
    EXPECT_FALSE(pattern[3].level_high);
    EXPECT_EQ(pattern[3].duration_ms, 0U);
}

TEST_F(BuzzerPatternTest, PatternLookupSupportsNullCount) {
    const BuzzerPatternStep* pattern =
        sigurdos_buzzer_pattern(BuzzerPatternKind::Short, nullptr);

    ASSERT_NE(pattern, nullptr);
    EXPECT_TRUE(pattern[0].level_high);
    EXPECT_EQ(pattern[0].duration_ms, SIGURDOS_BUZZER_SHORT_ON_MS);
}

TEST_F(BuzzerPatternTest, PatternDurationsStayWithinResponsiveBounds) {
    std::size_t count = 0;
    const BuzzerPatternStep* pattern =
        sigurdos_buzzer_pattern(BuzzerPatternKind::Double, &count);

    uint16_t total_ms = 0;
    for (std::size_t i = 0; i < count; ++i) {
        total_ms = static_cast<uint16_t>(total_ms + pattern[i].duration_ms);
        EXPECT_LE(pattern[i].duration_ms, 100U);
    }

    EXPECT_EQ(total_ms,
              static_cast<uint16_t>((2 * SIGURDOS_BUZZER_DOUBLE_ON_MS) +
                                    SIGURDOS_BUZZER_DOUBLE_GAP_MS));
    EXPECT_LE(total_ms, 250U);
}

class BuzzerPlaybackTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::current_millis = 1000;
        sigurdos::hal::buzzer_init();
        // Drain any pattern left active by a previous test: each loop call
        // can advance at most one step, so a handful of far-future ticks
        // always reaches the idle state.
        for (int i = 0; i < 8; ++i) {
            arduino_mock::current_millis += 1000;
            sigurdos::hal::buzzer_loop();
        }
        arduino_mock::current_millis = 1000;
        ASSERT_EQ(pin(), LOW);
    }

    int pin() const { return arduino_mock::pin_states[PIN_BUZZER]; }

    void loop_at(unsigned long ms) {
        arduino_mock::current_millis = ms;
        sigurdos::hal::buzzer_loop();
    }
};

TEST_F(BuzzerPlaybackTest, ShortBeepReturnsImmediatelyAndPlaysSequence) {
    sigurdos::hal::buzzer_beep_short();

    // The mock delay() advances current_millis, so an unchanged clock
    // proves the call no longer blocks (the old code burned 80 ms here).
    EXPECT_EQ(arduino_mock::current_millis, 1000UL);
    EXPECT_EQ(pin(), HIGH);

    loop_at(1079);  // 1 ms before the ON step ends
    EXPECT_EQ(pin(), HIGH);

    loop_at(1080);  // ON step elapsed — advance to terminal LOW
    EXPECT_EQ(pin(), LOW);

    loop_at(1081);  // pattern finished — further loops are no-ops
    EXPECT_EQ(pin(), LOW);
    loop_at(5000);
    EXPECT_EQ(pin(), LOW);
}

TEST_F(BuzzerPlaybackTest, DoubleBeepPlaysOnGapOnSequence) {
    sigurdos::hal::buzzer_beep_double();
    EXPECT_EQ(arduino_mock::current_millis, 1000UL);
    EXPECT_EQ(pin(), HIGH);

    loop_at(1059);
    EXPECT_EQ(pin(), HIGH);
    loop_at(1060);  // first ON elapsed → gap
    EXPECT_EQ(pin(), LOW);

    loop_at(1119);
    EXPECT_EQ(pin(), LOW);
    loop_at(1120);  // gap elapsed → second ON
    EXPECT_EQ(pin(), HIGH);

    loop_at(1180);  // second ON elapsed → terminal LOW
    EXPECT_EQ(pin(), LOW);
    loop_at(1181);
    EXPECT_EQ(pin(), LOW);
}

TEST_F(BuzzerPlaybackTest, NewBeepRestartsActivePattern) {
    sigurdos::hal::buzzer_beep_double();
    loop_at(1060);  // into the gap step
    ASSERT_EQ(pin(), LOW);

    arduino_mock::current_millis = 1070;
    sigurdos::hal::buzzer_beep_short();  // replaces the double pattern
    EXPECT_EQ(pin(), HIGH);

    loop_at(1149);  // short ON runs from 1070 → 1150
    EXPECT_EQ(pin(), HIGH);
    loop_at(1150);
    EXPECT_EQ(pin(), LOW);

    // No resurrected double-pattern step later on.
    loop_at(1300);
    EXPECT_EQ(pin(), LOW);
}

} // namespace
