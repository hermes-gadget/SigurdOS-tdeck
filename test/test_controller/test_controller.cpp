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

#include "test/test_controller.h"

namespace {

SigurdOSTestRfParseResult parse(const char* arg, SigurdOSTestRfParams* out,
                                int* parsed_fields = nullptr) {
    return sigurdos_test_controller_parse_rf_params(arg, out, parsed_fields);
}

TEST(TestControllerRfParserTest, ValidRfParametersParse) {
    SigurdOSTestRfParams out{};
    int parsed = 0;

    EXPECT_EQ(parse("869.525 10 250 5 22", &out, &parsed),
              SigurdOSTestRfParseResult::Ok);

    EXPECT_EQ(parsed, 5);
    EXPECT_FLOAT_EQ(out.freq, 869.525f);
    EXPECT_EQ(out.sf, 10);
    EXPECT_FLOAT_EQ(out.bw, 250.0f);
    EXPECT_EQ(out.cr, 5);
    EXPECT_EQ(out.tx_power_dbm, 22);
}

TEST(TestControllerRfParserTest, MissingOrNullOutputIsRejected) {
    SigurdOSTestRfParams out{};

    EXPECT_EQ(parse(nullptr, &out), SigurdOSTestRfParseResult::MissingArgs);
    EXPECT_EQ(parse("869.525 10 250 5 22", nullptr),
              SigurdOSTestRfParseResult::MissingArgs);
}

TEST(TestControllerRfParserTest, BadArgumentCountIsReported) {
    SigurdOSTestRfParams out{};
    int parsed = 0;

    EXPECT_EQ(parse("869.525 10 250 5", &out, &parsed),
              SigurdOSTestRfParseResult::BadArgumentCount);
    EXPECT_EQ(parsed, 4);
}

TEST(TestControllerRfParserTest, SupportsRangeBoundaries) {
    SigurdOSTestRfParams out{};

    EXPECT_EQ(parse("400 6 7.8 5 2", &out), SigurdOSTestRfParseResult::Ok);
    EXPECT_FLOAT_EQ(out.freq, 400.0f);
    EXPECT_EQ(out.sf, 6);
    EXPECT_FLOAT_EQ(out.bw, 7.8f);
    EXPECT_EQ(out.cr, 5);
    EXPECT_EQ(out.tx_power_dbm, 2);

    EXPECT_EQ(parse("1000 12 500 8 22", &out), SigurdOSTestRfParseResult::Ok);
    EXPECT_FLOAT_EQ(out.freq, 1000.0f);
    EXPECT_EQ(out.sf, 12);
    EXPECT_FLOAT_EQ(out.bw, 500.0f);
    EXPECT_EQ(out.cr, 8);
    EXPECT_EQ(out.tx_power_dbm, 22);
}

TEST(TestControllerRfParserTest, RejectsFrequencyOutsideRange) {
    SigurdOSTestRfParams out{};

    EXPECT_EQ(parse("399.9 10 250 5 22", &out),
              SigurdOSTestRfParseResult::FrequencyOutOfRange);
    EXPECT_EQ(parse("1000.1 10 250 5 22", &out),
              SigurdOSTestRfParseResult::FrequencyOutOfRange);
}

TEST(TestControllerRfParserTest, RejectsSpreadingFactorOutsideRange) {
    SigurdOSTestRfParams out{};

    EXPECT_EQ(parse("869.525 5 250 5 22", &out),
              SigurdOSTestRfParseResult::SpreadingFactorOutOfRange);
    EXPECT_EQ(parse("869.525 13 250 5 22", &out),
              SigurdOSTestRfParseResult::SpreadingFactorOutOfRange);
}

TEST(TestControllerRfParserTest, RejectsBandwidthOutsideRange) {
    SigurdOSTestRfParams out{};

    EXPECT_EQ(parse("869.525 10 7.7 5 22", &out),
              SigurdOSTestRfParseResult::BandwidthOutOfRange);
    EXPECT_EQ(parse("869.525 10 500.1 5 22", &out),
              SigurdOSTestRfParseResult::BandwidthOutOfRange);
}

TEST(TestControllerRfParserTest, RejectsCodingRateOutsideRange) {
    SigurdOSTestRfParams out{};

    EXPECT_EQ(parse("869.525 10 250 4 22", &out),
              SigurdOSTestRfParseResult::CodingRateOutOfRange);
    EXPECT_EQ(parse("869.525 10 250 9 22", &out),
              SigurdOSTestRfParseResult::CodingRateOutOfRange);
}

TEST(TestControllerRfParserTest, RejectsTxPowerOutsideRange) {
    SigurdOSTestRfParams out{};

    EXPECT_EQ(parse("869.525 10 250 5 1", &out),
              SigurdOSTestRfParseResult::TxPowerOutOfRange);
    EXPECT_EQ(parse("869.525 10 250 5 23", &out),
              SigurdOSTestRfParseResult::TxPowerOutOfRange);
}

TEST(TestControllerRfParserTest, OptionalRxBoostedGainEnabled) {
    SigurdOSTestRfParams out{};
    int parsed = 0;

    EXPECT_EQ(parse("869.525 10 250 5 22 1", &out, &parsed),
              SigurdOSTestRfParseResult::Ok);
    EXPECT_EQ(parsed, 6);
    EXPECT_FLOAT_EQ(out.freq, 869.525f);
    EXPECT_EQ(out.sf, 10);
    EXPECT_FLOAT_EQ(out.bw, 250.0f);
    EXPECT_EQ(out.cr, 5);
    EXPECT_EQ(out.tx_power_dbm, 22);
    EXPECT_TRUE(out.rx_boosted_gain);
}

TEST(TestControllerRfParserTest, OptionalRxBoostedGainDisabled) {
    SigurdOSTestRfParams out{};
    int parsed = 0;

    EXPECT_EQ(parse("869.525 10 250 5 22 0", &out, &parsed),
              SigurdOSTestRfParseResult::Ok);
    EXPECT_EQ(parsed, 6);
    EXPECT_FALSE(out.rx_boosted_gain);
}

TEST(TestControllerRfParserTest, RxBoostedGainDefaultsToFalseWhenAbsent) {
    SigurdOSTestRfParams out{};
    int parsed = 0;

    // 5 args (no rx_boost) — backward compatible
    EXPECT_EQ(parse("869.525 10 250 5 22", &out, &parsed),
              SigurdOSTestRfParseResult::Ok);
    EXPECT_EQ(parsed, 5);
    EXPECT_FALSE(out.rx_boosted_gain);
}

TEST(TestControllerRfParserTest, ExtraTrailingArgsIgnoredGracefully) {
    SigurdOSTestRfParams out{};
    int parsed = 0;

    // sscanf only reads the 6 specifiers, ignoring trailing data.
    // This is acceptable CLI behavior — parse what you need, ignore the rest.
    EXPECT_EQ(parse("869.525 10 250 5 22 1 999 extra junk", &out, &parsed),
              SigurdOSTestRfParseResult::Ok);
    EXPECT_EQ(parsed, 6);
    EXPECT_TRUE(out.rx_boosted_gain);
}

// getrf does not use the parser — it reads prefs directly and prints via Serial.
// Dispatch/Serial smoke tests require the full controller linked on hardware.

} // namespace
