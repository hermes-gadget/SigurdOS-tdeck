// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include <cstdint>

#include "diagnostics/telemetry_drift.h"

namespace {

using sigurdos::telemetry::DriftDetector;

TEST(TelemetryDriftTest, DoesNotReportBeforeWindowBoundary)
{
    DriftDetector detector;
    detector.begin(10000, 100, 60000, 4096);
    detector.update(5000);

    EXPECT_FALSE(detector.window_elapsed(60099));
    EXPECT_FALSE(detector.should_report(60099));
}

TEST(TelemetryDriftTest, RecoveredTransientDoesNotReport)
{
    DriftDetector detector;
    detector.begin(10000, 0, 60000, 4096);
    detector.update(4000);
    detector.update(10000);

    EXPECT_TRUE(detector.window_elapsed(60000));
    EXPECT_FALSE(detector.should_report(60000));
}

TEST(TelemetryDriftTest, EndpointLossReportsAndRotationAgesBaseline)
{
    DriftDetector detector;
    detector.begin(10000, 0, 60000, 4096);
    detector.update(5000);

    EXPECT_TRUE(detector.should_report(60000));
    EXPECT_EQ(detector.delta(), 5000);
    EXPECT_STREQ(detector.trend_str(), "falling");

    detector.rotate(60000);
    EXPECT_EQ(detector.baseline, 5000);
    EXPECT_FALSE(detector.window_elapsed(60000));
}

TEST(TelemetryDriftTest, ElapsedComparisonIsMillisWrapSafe)
{
    DriftDetector detector;
    detector.begin(10000, UINT32_MAX - 999U, 60000, 4096);
    detector.update(5000);

    EXPECT_FALSE(detector.window_elapsed(58999));
    EXPECT_TRUE(detector.should_report(59000));
}

}  // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
