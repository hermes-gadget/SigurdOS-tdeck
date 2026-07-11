#include <gtest/gtest.h>

#include "hal/wifi_ota.h"

using sigurdos::wifi_sta::Status;
using sigurdos::wifi_sta::advanceConnectingStatus;

TEST(WifiStaState, ConnectingSucceedsWhenHardwareConnects) {
    EXPECT_EQ(advanceConnectingStatus(Status::Connecting, true, 1),
              Status::Connected);
}

TEST(WifiStaState, ConnectingTimesOutOnlyAfterDeadline) {
    EXPECT_EQ(advanceConnectingStatus(Status::Connecting, false, 15000),
              Status::Connecting);
    EXPECT_EQ(advanceConnectingStatus(Status::Connecting, false, 15001),
              Status::Failed);
}

TEST(WifiStaState, NonConnectingStatesAreUnchanged) {
    EXPECT_EQ(advanceConnectingStatus(Status::Idle, true, 99999), Status::Idle);
    EXPECT_EQ(advanceConnectingStatus(Status::Failed, true, 99999), Status::Failed);
}

TEST(WifiStaState, UnsignedElapsedSupportsMillisWrap) {
    const uint32_t start = 0xFFFFFF00U;
    const uint32_t now = 0x00003900U;
    EXPECT_EQ(advanceConnectingStatus(Status::Connecting, false, now - start),
              Status::Connecting);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
