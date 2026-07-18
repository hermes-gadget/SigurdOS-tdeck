// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include <cstring>

#include "mesh/airtime_policy.h"
#include "mesh/auto_add_policy.h"
#include "mesh/radio_timing_policy.h"
#include "mesh/telemetry_response_policy.h"

namespace {

namespace airtime = sigurdos::mesh::airtime_policy;
namespace auto_add = sigurdos::mesh::auto_add_policy;
namespace timing = sigurdos::mesh::radio_timing_policy;
namespace telemetry = sigurdos::mesh::telemetry_policy;

TEST(AirtimePolicyTest, ConvertsDutyCyclePercentToMeshCoreFactor) {
    // C-01: 0% is explicitly unlimited; 100% is naturally unrestricted too.
    EXPECT_FLOAT_EQ(airtime::dutyCyclePercentToFactor(0), 0.0f);
    EXPECT_FLOAT_EQ(airtime::dutyCyclePercentToFactor(1), 99.0f);
    EXPECT_FLOAT_EQ(airtime::dutyCyclePercentToFactor(10), 9.0f);
    EXPECT_FLOAT_EQ(airtime::dutyCyclePercentToFactor(100), 0.0f);
}

TEST(AirtimePolicyTest, CompanionFactorMapsBackToDisplayPolicy) {
    EXPECT_EQ(airtime::factorToDutyCyclePercent(0.0f), 0);
    EXPECT_EQ(airtime::factorToDutyCyclePercent(1.0f), 50);
    EXPECT_EQ(airtime::factorToDutyCyclePercent(9.0f), 10);
    EXPECT_EQ(airtime::factorToDutyCyclePercent(99.0f), 1);
}

TEST(AutoAddPolicyTest, StoredManualAndTypeSettingsAreHonored) {
    EXPECT_TRUE(auto_add::enabled(0));
    EXPECT_TRUE(auto_add::typeAllowed(0, 0, 4));

    EXPECT_FALSE(auto_add::enabled(1));
    EXPECT_TRUE(auto_add::typeAllowed(1, auto_add::ADD_CHAT, 1));
    EXPECT_FALSE(auto_add::typeAllowed(1, auto_add::ADD_CHAT, 2));
    EXPECT_FALSE(auto_add::typeAllowed(1, 0xFF, 0));
}

TEST(AutoAddPolicyTest, OverwriteBitIsIndependentOfTypeBits) {
    EXPECT_FALSE(auto_add::overwriteWhenFull(auto_add::ADD_CHAT));
    EXPECT_TRUE(auto_add::overwriteWhenFull(
        auto_add::OVERWRITE_OLDEST | auto_add::ADD_CHAT));
}

TEST(RadioTimingPolicyTest, RxDelayConsumesConfiguredBase) {
    EXPECT_EQ(timing::rxDelay(0.0f, 0.5f, 100), 0);
    EXPECT_NE(timing::rxDelay(2.0f, 0.5f, 100),
              timing::rxDelay(10.0f, 0.5f, 100));
}

TEST(RadioTimingPolicyTest, FloodAndDirectWindowsConsumeTheirFactors) {
    EXPECT_EQ(timing::retransmitUpperBound(100, 0.0f, 0.5f), 0u);
    EXPECT_EQ(timing::retransmitUpperBound(100, 1.0f, 0.5f), 251u);
    EXPECT_EQ(timing::retransmitUpperBound(100, 2.0f, 0.5f), 501u);
    EXPECT_EQ(timing::retransmitUpperBound(100, 1.0f, 0.2f), 101u);
}

TEST(TelemetryPolicyTest, DecodesIndependentModesAndContactFlags) {
    const uint8_t modes = telemetry::MODE_ALLOW_FLAGS |
        (telemetry::MODE_ALLOW_ALL << 2) |
        (telemetry::MODE_DENY << 4);
    const uint8_t contact_flags = static_cast<uint8_t>(telemetry::PERM_BASE << 1);

    EXPECT_EQ(telemetry::resolvePermissions(modes, contact_flags, 0),
              telemetry::PERM_BASE | telemetry::PERM_LOCATION);
}

TEST(TelemetryPolicyTest, InverseRequestMaskRemovesRequestedPermission) {
    const uint8_t allow_all = telemetry::MODE_ALLOW_ALL |
        (telemetry::MODE_ALLOW_ALL << 2) |
        (telemetry::MODE_ALLOW_ALL << 4);

    EXPECT_EQ(telemetry::resolvePermissions(
                  allow_all, 0, telemetry::PERM_LOCATION),
              telemetry::PERM_BASE | telemetry::PERM_ENVIRONMENT);
}

TEST(TelemetryPolicyTest, BaseDeniedProducesNoReply) {
    uint8_t reply[64]{};
    const telemetry::GpsSample gps{true, 51.5f, -0.1f, 25.0f};

    EXPECT_EQ(telemetry::buildResponse(
                  reply, sizeof(reply), 0x12345678, telemetry::PERM_LOCATION,
                  4200, gps),
              0u);
}

TEST(TelemetryPolicyTest, GoldenReplyReflectsTagAndIncludesAuthorizedLocation) {
    uint8_t reply[64]{};
    const telemetry::GpsSample gps{true, 1.25f, -2.5f, 3.0f};
    const size_t len = telemetry::buildResponse(
        reply, sizeof(reply), 0x12345678,
        telemetry::PERM_BASE | telemetry::PERM_LOCATION, 4200, gps);

    const uint8_t expected[] = {
        0x78, 0x56, 0x34, 0x12,
        0x01, 0x74, 0x01, 0xA4,
        0x01, 0x88,
        0x00, 0x30, 0xD4,
        0xFF, 0x9E, 0x58,
        0x00, 0x01, 0x2C,
    };
    ASSERT_EQ(len, sizeof(expected));
    EXPECT_EQ(0, std::memcmp(reply, expected, sizeof(expected)));
}

TEST(TelemetryPolicyTest, LocationDeniedGoldenReplyCannotLeakGps) {
    uint8_t reply[64]{};
    const telemetry::GpsSample gps{true, 51.5f, -0.1f, 25.0f};
    const size_t len = telemetry::buildResponse(
        reply, sizeof(reply), 0xAABBCCDD, telemetry::PERM_BASE, 3700, gps);

    const uint8_t expected[] = {
        0xDD, 0xCC, 0xBB, 0xAA,
        0x01, 0x74, 0x01, 0x72,
    };
    ASSERT_EQ(len, sizeof(expected));
    EXPECT_EQ(0, std::memcmp(reply, expected, sizeof(expected)));
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
