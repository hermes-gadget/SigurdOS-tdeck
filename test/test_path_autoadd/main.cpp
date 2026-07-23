// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include <cstring>

#include "mesh/autoadd_policy.h"
#include "mesh/path_discovery.h"
#include "mesh/node_discovery.h"

namespace sm = sigurdos::mesh;

TEST(PathDiscovery, BuildsUpstreamTelemetryRequestPayload)
{
    const uint8_t random_bytes[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t request[sm::PATH_DISCOVERY_REQUEST_LEN] = {};

    sm::buildPathDiscoveryRequest(request, random_bytes);

    const uint8_t expected[sm::PATH_DISCOVERY_REQUEST_LEN] = {
        0x03, 0xFE, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44
    };
    EXPECT_EQ(std::memcmp(request, expected, sizeof(expected)), 0);
}

TEST(NodeDiscovery, ParsesSinceFilterAndBuildsSignedSnrVector)
{
    const uint8_t request_bytes[10] = {
        0x81, 0x04, 0x78, 0x56, 0x34, 0x12, 0x40, 0x30, 0x20, 0x10
    };
    sm::node_discovery::Request request{};
    ASSERT_TRUE(sm::node_discovery::parseRequest(
        request_bytes, sizeof(request_bytes), request));
    EXPECT_TRUE(request.prefix_only);
    EXPECT_EQ(request.filter, 0x04);
    EXPECT_EQ(request.tag, 0x12345678U);
    EXPECT_EQ(request.since, 0x10203040U);
    EXPECT_TRUE(sm::node_discovery::matches(request, 2, 0x10203040U));
    EXPECT_FALSE(sm::node_discovery::matches(request, 2, 0x1020303FU));

    uint8_t key[32];
    for (size_t i = 0; i < sizeof(key); ++i) key[i] = (uint8_t)i;
    uint8_t response[38]{};
    ASSERT_EQ(sm::node_discovery::buildResponse(
        request, 2, -9, key, response, sizeof(response)), 14U);
    const uint8_t expected[14] = {
        0x92, 0xF7, 0x78, 0x56, 0x34, 0x12,
        0, 1, 2, 3, 4, 5, 6, 7
    };
    EXPECT_EQ(std::memcmp(response, expected, sizeof(expected)), 0);
}

TEST(NodeDiscovery, RejectsEmptyFiltersMalformedLengthsAndResponseStorms)
{
    uint8_t bytes[10] = {0x80, 0, 1, 0, 0, 0, 0, 0, 0, 0};
    sm::node_discovery::Request request{};
    ASSERT_TRUE(sm::node_discovery::parseRequest(bytes, 6, request));
    EXPECT_FALSE(sm::node_discovery::matches(request, 1, 0));
    EXPECT_FALSE(sm::node_discovery::parseRequest(bytes, 7, request));

    sm::node_discovery::RateLimiter limiter;
    for (uint16_t i = 0; i < sm::node_discovery::RATE_MAX; ++i) {
        EXPECT_TRUE(limiter.allow(100));
    }
    EXPECT_FALSE(limiter.allow(100));
    EXPECT_TRUE(limiter.allow(220));
}

TEST(AutoAddPolicy, ManualAddBitControlsGlobalAutoAdd)
{
    EXPECT_TRUE(sm::autoAddEnabled(0));
    EXPECT_FALSE(sm::autoAddEnabled(1));
    EXPECT_TRUE(sm::autoAddEnabled(2));
}

TEST(AutoAddPolicy, AutomaticModeAcceptsAllAdvertTypes)
{
    EXPECT_TRUE(sm::shouldAutoAddType(0, 0, sm::AUTOADD_ADV_TYPE_CHAT));
    EXPECT_TRUE(sm::shouldAutoAddType(0, 0, sm::AUTOADD_ADV_TYPE_SENSOR));
    EXPECT_TRUE(sm::shouldAutoAddType(0, 0, 0xFF));
}

TEST(AutoAddPolicy, ManualModeUsesConfiguredTypeMask)
{
    const uint8_t config = sm::AUTO_ADD_CHAT | sm::AUTO_ADD_SENSOR;
    EXPECT_TRUE(sm::shouldAutoAddType(1, config, sm::AUTOADD_ADV_TYPE_CHAT));
    EXPECT_TRUE(sm::shouldAutoAddType(1, config, sm::AUTOADD_ADV_TYPE_SENSOR));
    EXPECT_FALSE(sm::shouldAutoAddType(1, config, sm::AUTOADD_ADV_TYPE_REPEATER));
    EXPECT_FALSE(sm::shouldAutoAddType(1, config, sm::AUTOADD_ADV_TYPE_ROOM));
    EXPECT_FALSE(sm::shouldAutoAddType(1, 0xFF, 0));
}

TEST(AutoAddPolicy, OverwriteRequiresExplicitConfigBit)
{
    EXPECT_FALSE(sm::shouldOverwriteAutoAddContact(0x1E));
    EXPECT_TRUE(sm::shouldOverwriteAutoAddContact(0x1F));
}

TEST(AutoAddPolicy, CompatibilityApiDelegatesToCanonicalPolicy)
{
    EXPECT_EQ(sm::autoAddEnabled(0), sm::auto_add_policy::enabled(0));
    EXPECT_EQ(sm::autoAddEnabled(1), sm::auto_add_policy::enabled(1));
    EXPECT_EQ(sm::shouldAutoAddType(1, sm::AUTO_ADD_ROOM_SERVER,
                                    sm::AUTOADD_ADV_TYPE_ROOM),
              sm::auto_add_policy::typeAllowed(
                  1, sm::auto_add_policy::ADD_ROOM,
                  sm::AUTOADD_ADV_TYPE_ROOM));
    EXPECT_EQ(sm::shouldOverwriteAutoAddContact(sm::AUTO_ADD_OVERWRITE_OLDEST),
              sm::auto_add_policy::overwriteWhenFull(
                  sm::auto_add_policy::OVERWRITE_OLDEST));
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
