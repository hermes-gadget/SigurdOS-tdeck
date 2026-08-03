// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "hal/wifi_ota.h"

namespace {

TEST(WifiRuntimePolicy, OtaApIsExclusiveToCompanionTransports)
{
    EXPECT_TRUE(sigurdos::ota::companionTransportsAllowed(false));
    EXPECT_FALSE(sigurdos::ota::companionTransportsAllowed(true));
}

TEST(WifiRuntimePolicy, OtaModeFollowsExistingStaLease)
{
    EXPECT_EQ(sigurdos::ota::modeForOtaSession(true),
              sigurdos::ota::NetworkMode::Station);
    EXPECT_EQ(sigurdos::ota::modeForOtaSession(false),
              sigurdos::ota::NetworkMode::AccessPoint);
}

TEST(WifiRuntimePolicy, StaTransitionDoesNotFailAtTheDeadline)
{
    using sigurdos::wifi_sta::Status;
    EXPECT_EQ(sigurdos::wifi_sta::advanceConnectingStatus(
                  Status::Connecting, false,
                  sigurdos::wifi_sta::CONNECT_TIMEOUT_MS),
              Status::Connecting);
    EXPECT_EQ(sigurdos::wifi_sta::advanceConnectingStatus(
                  Status::Connecting, false,
                  sigurdos::wifi_sta::CONNECT_TIMEOUT_MS + 1U),
              Status::Failed);
}

TEST(WifiRuntimePolicy, StatusSnapshotStartsDisconnectedAndBounded)
{
    const sigurdos::wifi_sta::StatusInfo info{};
    EXPECT_EQ(info.status, sigurdos::wifi_sta::Status::Idle);
    EXPECT_EQ(info.mode, sigurdos::wifi_sta::LinkMode::Off);
    EXPECT_FALSE(info.connected);
    EXPECT_EQ(info.ssid[0], '\0');
    EXPECT_EQ(info.ip[0], '\0');
    EXPECT_EQ(info.error[0], '\0');
}

}  // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
