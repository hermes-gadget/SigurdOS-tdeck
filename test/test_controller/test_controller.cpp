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
#include "test/test_controller_command.h"

namespace {

TEST(TestControllerCommandTest, EveryDispatchedCommandAndAliasIsRecognized) {
    const char* commands[] = {
        "help", "?", "nav home", "navigate settings", "back", "tb up", "trackball c",
        "type hello world", "picker a", "press enter", "inject bob hi", "msg bob hi",
        "sendchannel Public hi", "sendmessage bob hi", "senddm bob hi", "opendm bob",
        "addchannel Public", "addchan Public", "removechannel 1", "rmchannel 1",
        "removechan 1", "rmchan 1", "addrepeater node", "addroomserver room",
        "login node pass", "setlogin node", "setloginguest node", "screen", "status",
        "stresschat", "keydiag", "kbddiag", "inputdiag", "touchdiag", "trackballdiag",
        "tbdiag", "gpsdiag", "contactstats", "ble status", "debug level", "emoji",
        "emoji-ac a", "capture", "acmd node", "loginstat node", "tree", "widgets",
        "telemetry on", "query crash", "crash report", "drift", "scrolllist 1",
        "tap 1 2", "backlight on", "fetchmsgs node 0", "getrf", "setrf args",
        "reboot", "restart", "factoryreset", "wipe", "advert"};
    for (const char* line : commands) {
        SigurdOSTestCommandLine parsed{};
        EXPECT_EQ(sigurdos_test_controller_parse_command(line, &parsed),
                  SigurdOSTestCommandParseResult::Ok) << line;
    }
}

TEST(TestControllerCommandTest, SplitsArgumentsAndRejectsMalformedInput) {
    SigurdOSTestCommandLine parsed{};
    EXPECT_EQ(sigurdos_test_controller_parse_command("  type  hello world  ", &parsed),
              SigurdOSTestCommandParseResult::Ok);
    EXPECT_STREQ(parsed.command, "type");
    EXPECT_STREQ(parsed.arguments, "hello world");
    EXPECT_EQ(sigurdos_test_controller_parse_command(" # comment", &parsed),
              SigurdOSTestCommandParseResult::Empty);
    EXPECT_EQ(sigurdos_test_controller_parse_command("unknown thing", &parsed),
              SigurdOSTestCommandParseResult::Unknown);
    char oversized[300];
    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1] = '\0';
    EXPECT_EQ(sigurdos_test_controller_parse_command(oversized, &parsed),
              SigurdOSTestCommandParseResult::TooLong);
}

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

TEST(TestControllerRfParserTest, RejectsExtraTokensAndComments) {
    SigurdOSTestRfParams out{};
    int parsed = 0;

    EXPECT_EQ(parse("869.525 10 250 5 22 1 999 extra junk", &out, &parsed),
              SigurdOSTestRfParseResult::BadArgumentCount);
    EXPECT_EQ(parsed, 6);
    EXPECT_EQ(parse("869.525 10 250 5 22 # comment", &out),
              SigurdOSTestRfParseResult::BadArgumentCount);
}

TEST(TestControllerRfParserTest, AllowsTrailingWhitespaceOnly) {
    SigurdOSTestRfParams out{};
    EXPECT_EQ(parse("869.525 10 250 5 22 1 \t\r\n", &out),
              SigurdOSTestRfParseResult::Ok);
}

TEST(TestControllerRfParserTest, RejectsInvalidBoostAndNonFiniteNumbers) {
    SigurdOSTestRfParams out{};
    EXPECT_EQ(parse("869.525 10 250 5 22 2", &out),
              SigurdOSTestRfParseResult::RxBoostedGainOutOfRange);
    EXPECT_EQ(parse("nan 10 250 5 22", &out),
              SigurdOSTestRfParseResult::NonFiniteValue);
    EXPECT_EQ(parse("869.525 10 inf 5 22", &out),
              SigurdOSTestRfParseResult::NonFiniteValue);
    EXPECT_EQ(parse("869MHz 10 250 5 22", &out),
              SigurdOSTestRfParseResult::BadArgumentCount);
}

TEST(TestControllerBoundsTest, EmojiPageNeverExceedsObjectBudget) {
    EXPECT_EQ(sigurdos_test_controller_emoji_page_count(0), 0);
    EXPECT_EQ(sigurdos_test_controller_emoji_page_count(362), 16);
    EXPECT_EQ(sigurdos_test_controller_emoji_page_items(362, 0), 24);
    EXPECT_EQ(sigurdos_test_controller_emoji_page_items(362, 15), 2);
    EXPECT_EQ(sigurdos_test_controller_emoji_page_items(362, 16), 0);
    EXPECT_TRUE(sigurdos_test_controller_emoji_object_count_allowed(24));
    EXPECT_FALSE(sigurdos_test_controller_emoji_object_count_allowed(25));
    EXPECT_LE(SIGURDOS_TEST_EMOJI_FIXED_OBJECTS + SIGURDOS_TEST_EMOJI_PAGE_SIZE * 2,
              SIGURDOS_TEST_EMOJI_OBJECT_LIMIT);
}

TEST(TestControllerBoundsTest, WidgetTreeDepthAndNodeCapsAreFinite) {
    EXPECT_TRUE(sigurdos_test_controller_tree_can_descend(0));
    EXPECT_TRUE(sigurdos_test_controller_tree_can_descend(
        SIGURDOS_TEST_TREE_MAX_DEPTH - 1));
    EXPECT_FALSE(sigurdos_test_controller_tree_can_descend(
        SIGURDOS_TEST_TREE_MAX_DEPTH));
    EXPECT_GE(SIGURDOS_TEST_TREE_MAX_NODES, 1);
    EXPECT_LE(SIGURDOS_TEST_TREE_MAX_NODES, 128);
}

TEST(TestControllerBoundsTest, CaptureAcceptsOnlyBoundedRgb565Frames) {
    EXPECT_TRUE(sigurdos_test_controller_capture_size_allowed(320, 240, 640));
    EXPECT_FALSE(sigurdos_test_controller_capture_size_allowed(0, 240, 640));
    EXPECT_FALSE(sigurdos_test_controller_capture_size_allowed(320, 0, 640));
    EXPECT_FALSE(sigurdos_test_controller_capture_size_allowed(320, 240, 639));
    EXPECT_FALSE(sigurdos_test_controller_capture_size_allowed(321, 240, 642));
    EXPECT_FALSE(sigurdos_test_controller_capture_size_allowed(320, 241, 640));
    EXPECT_FALSE(sigurdos_test_controller_capture_size_allowed(320, 240, 644));
}

TEST(TestControllerBoundsTest, DiagnosticTimeoutIgnoresStaleLoopTimestamp) {
    EXPECT_FALSE(sigurdos_test_controller_timeout_elapsed(999, 1000, 60'000));
    EXPECT_FALSE(sigurdos_test_controller_timeout_elapsed(61'000, 1000, 60'000));
    EXPECT_TRUE(sigurdos_test_controller_timeout_elapsed(61'001, 1000, 60'000));

    // A real millis() wrap remains a small positive elapsed interval.
    EXPECT_TRUE(sigurdos_test_controller_timeout_elapsed(0x20, 0xFFFFFFF0, 47));
}

// getrf does not use the parser — it reads prefs directly and prints via Serial.
// Dispatch/Serial smoke tests require the full controller linked on hardware.

} // namespace
