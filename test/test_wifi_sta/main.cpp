#include <gtest/gtest.h>

#include "hal/wifi_ota.h"
#include "validation/gps_validation_wifi.h"

using sigurdos::wifi_sta::Status;
using sigurdos::wifi_sta::advanceConnectingStatus;
using sigurdos::gps_validation::ConnectPollResult;
using sigurdos::gps_validation::UPLOAD_LINE_CAPACITY;
using sigurdos::gps_validation::UPLOAD_QUEUE_CAPACITY;
using sigurdos::gps_validation::UploadQueue;
using sigurdos::gps_validation::pollConnect;
using sigurdos::gps_validation::reconnectDue;

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

TEST(GpsValidationWifi, ConnectPollingDoesNotBlockUntilTimeout) {
    EXPECT_EQ(pollConnect(false, 0), ConnectPollResult::Waiting);
    EXPECT_EQ(pollConnect(false, 15000), ConnectPollResult::Waiting);
    EXPECT_EQ(pollConnect(false, 15001), ConnectPollResult::TimedOut);
    EXPECT_EQ(pollConnect(true, 1), ConnectPollResult::Connected);
}

TEST(GpsValidationWifi, ReconnectDeadlineSupportsMillisWrap) {
    const uint32_t last_attempt = 0xFFFFFF00U;
    EXPECT_FALSE(reconnectDue(0x00003900U, last_attempt));
    EXPECT_TRUE(reconnectDue(0x00003A00U, last_attempt));
}

TEST(GpsValidationWifi, UploadQueuePreservesFifoOrder) {
    UploadQueue queue;
    ASSERT_TRUE(queue.push("one"));
    ASSERT_TRUE(queue.push("two"));
    ASSERT_STREQ(queue.front(), "one");
    queue.pop();
    ASSERT_STREQ(queue.front(), "two");
    queue.pop();
    EXPECT_EQ(queue.front(), nullptr);
}

TEST(GpsValidationWifi, UploadQueueAppliesBoundedBackpressure) {
    UploadQueue queue;
    for (size_t i = 0; i < UPLOAD_QUEUE_CAPACITY; ++i) {
        ASSERT_TRUE(queue.push("record"));
    }
    EXPECT_FALSE(queue.push("overflow"));
    EXPECT_EQ(queue.size(), UPLOAD_QUEUE_CAPACITY);
    EXPECT_EQ(queue.dropped(), 1U);
    EXPECT_STREQ(queue.front(), "record");
}

TEST(GpsValidationWifi, UploadQueueRejectsOversizedRecords) {
    UploadQueue queue;
    char oversized[UPLOAD_LINE_CAPACITY + 1] = {};
    memset(oversized, 'x', sizeof(oversized) - 1);
    EXPECT_FALSE(queue.push(oversized));
    EXPECT_EQ(queue.size(), 0U);
    EXPECT_EQ(queue.dropped(), 1U);
    EXPECT_FALSE(queue.push(nullptr));
    EXPECT_EQ(queue.dropped(), 1U);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
