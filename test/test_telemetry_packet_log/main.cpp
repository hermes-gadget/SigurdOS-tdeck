// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "diagnostics/telemetry.h"

namespace {

using sigurdos::telemetry::packet_log_field_or_empty;
using sigurdos::telemetry::packet_log_copy_field;

TEST(TelemetryPacketLogTest, NullFieldReturnsEmptyString)
{
    const char* field = packet_log_field_or_empty(nullptr);

    ASSERT_NE(field, nullptr);
    EXPECT_STREQ(field, "");
}

TEST(TelemetryPacketLogTest, NonNullFieldIsPreserved)
{
    const char* field = packet_log_field_or_empty("sender");

    EXPECT_STREQ(field, "sender");
}

TEST(TelemetryPacketLogTest, EmptyFieldIsPreserved)
{
    const char* field = packet_log_field_or_empty("");

    ASSERT_NE(field, nullptr);
    EXPECT_STREQ(field, "");
}

TEST(TelemetryPacketLogTest, CopyDoesNotSplitUtf8Sequence)
{
    char short_out[5]{};
    char exact_out[6]{};

    EXPECT_EQ(packet_log_copy_field(short_out, sizeof(short_out), "ab\xE2\x82\xAC"), 2u);
    EXPECT_STREQ(short_out, "ab");
    EXPECT_EQ(packet_log_copy_field(exact_out, sizeof(exact_out), "ab\xE2\x82\xAC"), 5u);
    EXPECT_STREQ(exact_out, "ab\xE2\x82\xAC");
}

TEST(TelemetryPacketLogTest, MessageTextIsRedactedByDefault)
{
    EXPECT_STREQ(sigurdos::telemetry::packet_log_text_by_policy("secret"),
                 "<redacted>");
    EXPECT_STREQ(sigurdos::telemetry::packet_log_text_by_policy(""), "");
    EXPECT_STREQ(sigurdos::telemetry::packet_log_text_by_policy(nullptr), "");
}

TEST(TelemetryPacketLogTest, QueryNamesAreValidatedBeforeDispatch)
{
    EXPECT_TRUE(sigurdos::telemetry::is_supported_query("full"));
    EXPECT_TRUE(sigurdos::telemetry::is_supported_query("hb-ring"));
    EXPECT_FALSE(sigurdos::telemetry::is_supported_query("full|unexpected"));
    EXPECT_FALSE(sigurdos::telemetry::is_supported_query(nullptr));
}

}  // anonymous namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
