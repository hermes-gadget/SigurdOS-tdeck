// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include <cstring>

#include "mesh/autoadd_policy.h"
#include "mesh/path_discovery.h"

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
