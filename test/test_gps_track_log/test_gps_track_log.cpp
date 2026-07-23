// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "app/gps_track_log.h"

#include <cstdio>
#include <string>
#include <unistd.h>

namespace {

class GpsTrackLogTest : public ::testing::Test {
protected:
    std::string path;

    void SetUp() override
    {
        path = "/tmp/sigurdos_gps_track_" +
            std::to_string(static_cast<long long>(getpid())) + ".bin";
        sigurdos::app::gpsTrackSetPathForTest(path.c_str());
        std::remove(path.c_str());
        std::remove((path + ".tmp").c_str());
        sigurdos::app::gpsTrackResetServiceForTest();
    }

    void TearDown() override
    {
        std::remove(path.c_str());
        std::remove((path + ".tmp").c_str());
    }
};

TEST_F(GpsTrackLogTest, InitializesEmptyAndAppendsPersistentPoints)
{
    ASSERT_TRUE(sigurdos::app::gpsTrackInit());
    ASSERT_TRUE(sigurdos::app::gpsTrackAppend(51.5000, -0.1200, 1000));
    ASSERT_TRUE(sigurdos::app::gpsTrackAppend(51.5010, -0.1200, 1060));

    sigurdos::app::GpsTrackPoint points[4]{};
    ASSERT_EQ(2u, sigurdos::app::gpsTrackLoadRecent(points, 4));
    EXPECT_NEAR(51.5000, sigurdos::app::gpsTrackLatitude(points[0]), 0.0000001);
    EXPECT_NEAR(51.5010, sigurdos::app::gpsTrackLatitude(points[1]), 0.0000001);

    sigurdos::app::GpsTrackStats stats{};
    ASSERT_TRUE(sigurdos::app::gpsTrackGetStats(&stats));
    EXPECT_EQ(2u, stats.waypoint_count);
    EXPECT_EQ(60u, stats.duration_s);
    EXPECT_NEAR(111.2, stats.distance_m, 1.0);
}

TEST_F(GpsTrackLogTest, ServiceHonorsIntervalAndRequiresValidFix)
{
    EXPECT_FALSE(sigurdos::app::gpsTrackService(
        true, 15, false, 51.5, -0.12, 1000, 100));
    EXPECT_TRUE(sigurdos::app::gpsTrackService(
        true, 15, true, 51.5, -0.12, 1000, 100));
    EXPECT_FALSE(sigurdos::app::gpsTrackService(
        true, 15, true, 51.6, -0.12, 1005, 15099));
    EXPECT_TRUE(sigurdos::app::gpsTrackService(
        true, 15, true, 51.6, -0.12, 1015, 15100));

    sigurdos::app::GpsTrackStats stats{};
    ASSERT_TRUE(sigurdos::app::gpsTrackGetStats(&stats));
    EXPECT_EQ(2u, stats.waypoint_count);
}

TEST_F(GpsTrackLogTest, DisablingServiceAllowsImmediateRestart)
{
    ASSERT_TRUE(sigurdos::app::gpsTrackService(
        true, 60, true, 51.5, -0.12, 1000, 1000));
    EXPECT_FALSE(sigurdos::app::gpsTrackService(
        false, 60, true, 51.5, -0.12, 1001, 1100));
    EXPECT_TRUE(sigurdos::app::gpsTrackService(
        true, 60, true, 51.5, -0.12, 1002, 1200));
}

TEST_F(GpsTrackLogTest, ServiceUsesMarkedUptimeWhenWallClockIsUnavailable)
{
    ASSERT_TRUE(sigurdos::app::gpsTrackService(
        true, 1, true, 51.5, -0.12, 0, 1000));
    ASSERT_TRUE(sigurdos::app::gpsTrackService(
        true, 1, true, 51.5, -0.12, 0, 3000));
    sigurdos::app::GpsTrackStats stats{};
    ASSERT_TRUE(sigurdos::app::gpsTrackGetStats(&stats));
    EXPECT_EQ(2u, stats.duration_s);
    EXPECT_NE(0u, stats.started_at & 0x80000000u);
}

TEST_F(GpsTrackLogTest, RecentLoadTrimAndClearKeepBoundedNewestData)
{
    for (uint32_t i = 0; i < 10; ++i) {
        ASSERT_TRUE(sigurdos::app::gpsTrackAppend(
            51.0 + i * 0.001, -0.1, 1000 + i));
    }
    sigurdos::app::GpsTrackPoint recent[3]{};
    ASSERT_EQ(3u, sigurdos::app::gpsTrackLoadRecent(recent, 3));
    EXPECT_EQ(1007u, recent[0].timestamp);
    EXPECT_EQ(1009u, recent[2].timestamp);

    ASSERT_TRUE(sigurdos::app::gpsTrackTrim(4));
    sigurdos::app::GpsTrackStats stats{};
    ASSERT_TRUE(sigurdos::app::gpsTrackGetStats(&stats));
    EXPECT_EQ(4u, stats.waypoint_count);
    EXPECT_EQ(1006u, stats.started_at);

    ASSERT_TRUE(sigurdos::app::gpsTrackClear());
    ASSERT_TRUE(sigurdos::app::gpsTrackGetStats(&stats));
    EXPECT_EQ(0u, stats.waypoint_count);
}

TEST(GpsTrackMathTest, RejectsInvalidCoordinatesAndClampsIntervals)
{
    EXPECT_FALSE(sigurdos::app::gpsTrackCoordinateValid(91.0, 0.0));
    EXPECT_FALSE(sigurdos::app::gpsTrackCoordinateValid(0.0, 181.0));
    EXPECT_EQ(1u, sigurdos::app::gpsTrackClampInterval(0));
    EXPECT_EQ(3600u, sigurdos::app::gpsTrackClampInterval(9000));
}

TEST(GpsTrackMathTest, RecordingDemandUsesFastestActiveGpsInterval)
{
    EXPECT_EQ(0u, sigurdos::app::gpsTrackGpsDemandInterval(
                      false, 5, false, 15));
    EXPECT_EQ(5u, sigurdos::app::gpsTrackGpsDemandInterval(
                      true, 5, false, 15));
    EXPECT_EQ(15u, sigurdos::app::gpsTrackGpsDemandInterval(
                       false, 5, true, 15));
    EXPECT_EQ(15u, sigurdos::app::gpsTrackGpsDemandInterval(
                       true, 0, true, 15));
    EXPECT_EQ(5u, sigurdos::app::gpsTrackGpsDemandInterval(
                      true, 5, true, 15));
}

}  // namespace
