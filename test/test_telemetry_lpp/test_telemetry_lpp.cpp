// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <helpers/sensors/LPPDataHelpers.h>

#include "mesh/telemetry_lpp_parser.h"

namespace {

using sigurdos::mesh::TelemetryResult;
using sigurdos::mesh::detail::parseTelemetryLpp;

struct TypeShape {
    uint8_t type;
    uint8_t size;
};

constexpr TypeShape kFixedShapes[] = {
    {LPP_DIGITAL_INPUT, 1},       {LPP_DIGITAL_OUTPUT, 1},
    {LPP_ANALOG_INPUT, 2},        {LPP_ANALOG_OUTPUT, 2},
    {LPP_GENERIC_SENSOR, 4},      {LPP_LUMINOSITY, 2},
    {LPP_PRESENCE, 1},            {LPP_TEMPERATURE, 2},
    {LPP_RELATIVE_HUMIDITY, 1},   {LPP_ACCELEROMETER, 6},
    {LPP_BAROMETRIC_PRESSURE, 2}, {LPP_VOLTAGE, 2},
    {LPP_CURRENT, 2},             {LPP_FREQUENCY, 4},
    {LPP_PERCENTAGE, 1},          {LPP_ALTITUDE, 2},
    {LPP_CONCENTRATION, 2},       {LPP_POWER, 2},
    {LPP_DISTANCE, 4},            {LPP_ENERGY, 4},
    {LPP_DIRECTION, 2},           {LPP_UNIXTIME, 4},
    {LPP_GYROMETER, 6},           {LPP_COLOUR, 3},
    {LPP_GPS, 9},                 {LPP_SWITCH, 1},
};

TelemetryResult sentinelResult()
{
    TelemetryResult result;
    memset(&result, 0xA5, sizeof(result));
    return result;
}

void expectRejectedWithoutMutation(const uint8_t* data, size_t len)
{
    TelemetryResult result = sentinelResult();
    const TelemetryResult before = result;
    EXPECT_FALSE(parseTelemetryLpp(data, len, &result));
    EXPECT_EQ(memcmp(&result, &before, sizeof(result)), 0);
}

TEST(TelemetryLppParserTest, EmptyContainerIsAValidEmptyResult)
{
    TelemetryResult result = sentinelResult();
    ASSERT_TRUE(parseTelemetryLpp(nullptr, 0, &result));
    EXPECT_EQ(result.n_items, 0);
}

TEST(TelemetryLppParserTest, RejectsEveryTruncationOfEveryFixedType)
{
    for (const TypeShape& shape : kFixedShapes) {
        std::vector<uint8_t> encoded = {1, shape.type};
        encoded.resize(2 + shape.size, 0x55);

        for (size_t truncated = 1; truncated < encoded.size(); ++truncated) {
            SCOPED_TRACE(::testing::Message()
                         << "type=" << static_cast<unsigned>(shape.type)
                         << " len=" << truncated);
            expectRejectedWithoutMutation(encoded.data(), truncated);
        }

        TelemetryResult result = {};
        ASSERT_TRUE(parseTelemetryLpp(encoded.data(), encoded.size(), &result));
        ASSERT_EQ(result.n_items, 1);
        EXPECT_EQ(result.items[0].channel, 1);
        EXPECT_EQ(result.items[0].type, shape.type);
    }
}

TEST(TelemetryLppParserTest, RejectsMalformedAndTruncatedPolyline)
{
    const uint8_t missing_size[] = {1, LPP_POLYLINE};
    expectRejectedWithoutMutation(missing_size, sizeof(missing_size));

    const uint8_t too_small[] = {1, LPP_POLYLINE, 7, 0, 0, 0, 0, 0, 0};
    expectRejectedWithoutMutation(too_small, sizeof(too_small));

    const uint8_t polyline[] = {1, LPP_POLYLINE, 10, 1, 0, 0, 0,
                                0, 0, 0, 0x11, 0x22};
    for (size_t truncated = 1; truncated < sizeof(polyline); ++truncated) {
        SCOPED_TRACE(::testing::Message() << "len=" << truncated);
        expectRejectedWithoutMutation(polyline, truncated);
    }

    TelemetryResult result = {};
    ASSERT_TRUE(parseTelemetryLpp(polyline, sizeof(polyline), &result));
    ASSERT_EQ(result.n_items, 1);
    EXPECT_EQ(result.items[0].type, LPP_POLYLINE);
}

TEST(TelemetryLppParserTest, RejectsUnknownTypesAndZeroChannels)
{
    const uint8_t unknown[] = {1, 99, 0};
    expectRejectedWithoutMutation(unknown, sizeof(unknown));

    const uint8_t zero_channel[] = {0, LPP_VOLTAGE, 0, 1};
    expectRejectedWithoutMutation(zero_channel, sizeof(zero_channel));
}

TEST(TelemetryLppParserTest, RejectsMoreThanResultCapacity)
{
    std::vector<uint8_t> encoded;
    for (int i = 0; i < MAX_TELEMETRY_ITEMS + 1; ++i) {
        encoded.push_back(static_cast<uint8_t>(i + 1));
        encoded.push_back(LPP_DIGITAL_INPUT);
        encoded.push_back(1);
    }
    expectRejectedWithoutMutation(encoded.data(), encoded.size());
}

TEST(TelemetryLppParserTest, DecodesDisplayedValuesAndSignedBoundaries)
{
    const uint8_t encoded[] = {
        1, LPP_VOLTAGE, 0x01, 0x23,             // 2.91 V
        2, LPP_TEMPERATURE, 0xFF, 0x85,         // -12.3 C
        3, LPP_RELATIVE_HUMIDITY, 0xC8,         // 100%
        4, LPP_BAROMETRIC_PRESSURE, 0x27, 0x94, // 1013.2 hPa
        5, LPP_CURRENT, 0x80, 0x00,             // -32.768 A
        6, LPP_GPS, 0x7F, 0xFF, 0xFF, 0x80, 0x00, 0x00,
        0xFF, 0xFF, 0x9C,
    };

    TelemetryResult result = {};
    ASSERT_TRUE(parseTelemetryLpp(encoded, sizeof(encoded), &result));
    ASSERT_EQ(result.n_items, 6);
    EXPECT_NEAR(result.items[0].value_float, 2.91f, 0.001f);
    EXPECT_STREQ(result.items[0].value_str, "2.91V");
    EXPECT_NEAR(result.items[1].value_float, -12.3f, 0.001f);
    EXPECT_STREQ(result.items[1].value_str, "-12.3C");
    EXPECT_FLOAT_EQ(result.items[2].value_float, 100.0f);
    EXPECT_STREQ(result.items[2].value_str, "100%");
    EXPECT_NEAR(result.items[3].value_float, 1013.2f, 0.01f);
    EXPECT_STREQ(result.items[3].value_str, "1013.2 hPa");
    EXPECT_NEAR(result.items[4].value_float, -32.768f, 0.001f);
    EXPECT_NEAR(result.items[5].value_float, 838.8607f, 0.001f);
    EXPECT_STREQ(result.items[5].value_str, "838.8607, -838.8608 -1m");
}

TEST(TelemetryLppParserTest, RejectsNullArguments)
{
    TelemetryResult result = sentinelResult();
    EXPECT_FALSE(parseTelemetryLpp(nullptr, 1, &result));
    EXPECT_FALSE(parseTelemetryLpp(nullptr, 0, nullptr));
}

} // namespace
