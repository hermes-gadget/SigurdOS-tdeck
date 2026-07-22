#include <gtest/gtest.h>

#include "hal/wifi_ota.h"
#include "validation/gps_validation_wifi.h"
#include "validation/gps_validation_status.h"

using sigurdos::wifi_sta::Status;
using sigurdos::wifi_sta::advanceConnectingStatus;
using sigurdos::gps_validation::ConnectPollResult;
using sigurdos::gps_validation::UPLOAD_LINE_CAPACITY;
using sigurdos::gps_validation::UPLOAD_QUEUE_CAPACITY;
using sigurdos::gps_validation::UploadQueue;
using sigurdos::gps_validation::pollConnect;
using sigurdos::gps_validation::reconnectDue;
using sigurdos::gps_validation::advanceWifiState;
using sigurdos::gps_validation::advanceWriteOffset;
using sigurdos::gps_validation::boundedWriteSize;
using sigurdos::gps_validation::parseHttpStatusLine;
using sigurdos::gps_validation::responseExpired;
using sigurdos::gps_validation::WifiState;

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

TEST(GpsValidationWifi, WifiTransitionsCoverLossTimeoutAndReconnect) {
    EXPECT_EQ(advanceWifiState(WifiState::Connecting, true, 1, false),
              WifiState::Connected);
    EXPECT_EQ(advanceWifiState(WifiState::Connecting, false, 15001, false),
              WifiState::Disconnected);
    EXPECT_EQ(advanceWifiState(WifiState::Connected, false, 0, false),
              WifiState::Disconnected);
    EXPECT_EQ(advanceWifiState(WifiState::Disconnected, false, 0, true),
              WifiState::Connecting);
}

TEST(GpsValidationWifi, PartialWritesAreBoundedAndRejectInvalidProgress) {
    EXPECT_EQ(boundedWriteSize(150, 0, 64), 64U);
    EXPECT_EQ(boundedWriteSize(150, 128, 64), 22U);
    EXPECT_EQ(boundedWriteSize(150, 150, 64), 0U);
    size_t offset = 0;
    EXPECT_TRUE(advanceWriteOffset(10, 4, &offset));
    EXPECT_EQ(offset, 4U);
    EXPECT_TRUE(advanceWriteOffset(10, 6, &offset));
    EXPECT_EQ(offset, 10U);
    EXPECT_FALSE(advanceWriteOffset(10, 1, &offset));
    EXPECT_FALSE(advanceWriteOffset(10, 0, &offset));
}

TEST(GpsValidationWifi, HttpStatusParsingIsStrict) {
    EXPECT_EQ(parseHttpStatusLine("HTTP/1.1 204 No Content"), 204);
    EXPECT_EQ(parseHttpStatusLine("HTTP/1.0 500"), 500);
    EXPECT_EQ(parseHttpStatusLine("garbage"), 0);
    EXPECT_EQ(parseHttpStatusLine("HTTP/1.1 99 nope"), 0);
    EXPECT_EQ(parseHttpStatusLine("HTTP/1.1 200x"), 0);
}

TEST(GpsValidationWifi, ResponseCleanupHandlesDisconnectTimeoutAndWrap) {
    EXPECT_TRUE(responseExpired(false, 0, 1, 0));
    EXPECT_FALSE(responseExpired(false, 1, 1, 0));
    EXPECT_FALSE(responseExpired(true, 0, 2000, 0));
    EXPECT_TRUE(responseExpired(true, 0, 2001, 0));
    EXPECT_TRUE(responseExpired(true, 0, 0x000007E1U, 0xFFFFFFF0U));
}

TEST(GpsValidationStatus, ValidZeroAxesStillReportLocation) {
    sigurdos::gps_validation::StatusFields fields{};
    fields.has_fix = true;
    fields.latitude = 0.0f;
    fields.longitude = 12.5f;
    char out[256];
    ASSERT_TRUE(sigurdos::gps_validation::buildStatus(out, sizeof(out), fields, true));
    EXPECT_NE(strstr(out, "|loc=1"), nullptr);
    EXPECT_NE(strstr(out, "|lat=0.000000|lon=12.500000"), nullptr);

    fields.latitude = 51.5f;
    fields.longitude = 0.0f;
    ASSERT_TRUE(sigurdos::gps_validation::buildStatus(out, sizeof(out), fields, true));
    EXPECT_NE(strstr(out, "|loc=1"), nullptr);

    fields.latitude = 0.0f;
    ASSERT_TRUE(sigurdos::gps_validation::buildStatus(out, sizeof(out), fields, true));
    EXPECT_NE(strstr(out, "|loc=1"), nullptr);
}

TEST(GpsValidationStatus, NoFixPrivacyAndTruncationAreExplicit) {
    sigurdos::gps_validation::StatusFields fields{};
    char out[256];
    ASSERT_TRUE(sigurdos::gps_validation::buildStatus(out, sizeof(out), fields, true));
    EXPECT_NE(strstr(out, "|loc=0"), nullptr);
    EXPECT_EQ(strstr(out, "|lat="), nullptr);

    fields.has_fix = true;
    fields.latitude = 1.0f;
    fields.longitude = 2.0f;
    ASSERT_TRUE(sigurdos::gps_validation::buildStatus(out, sizeof(out), fields, false));
    EXPECT_EQ(strstr(out, "|lat="), nullptr);

    char tiny[12];
    EXPECT_FALSE(sigurdos::gps_validation::buildStatus(tiny, sizeof(tiny), fields, true));
    EXPECT_EQ(tiny[sizeof(tiny) - 1], '\0');
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
