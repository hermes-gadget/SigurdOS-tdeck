// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>

#include "mesh/time_state.h"

namespace {

using sigurdos::mesh::TimeSource;
using sigurdos::mesh::TimeSyncTracker;

struct EpochCase {
    int year, month, day, hour, minute, second;
    uint32_t expected;
};

class EpochTest : public ::testing::TestWithParam<EpochCase> {};

TEST_P(EpochTest, ConvertsUtcUsingProductionArithmetic)
{
    const auto& c = GetParam();
    EXPECT_EQ(sigurdos::mesh::utcToEpoch(
                  c.year, c.month, c.day, c.hour, c.minute, c.second),
              c.expected);
}

INSTANTIATE_TEST_SUITE_P(EpochTable, EpochTest,
    ::testing::Values(
        EpochCase{1970, 1, 1, 0, 0, 0, 0U},
        EpochCase{2000, 1, 1, 0, 0, 0, 946684800U},
        EpochCase{2020, 3, 1, 0, 0, 0, 1583020800U},
        EpochCase{2024, 2, 29, 12, 0, 0, 1709208000U},
        EpochCase{2026, 7, 10, 12, 0, 0, 1783684800U},
        EpochCase{2030, 12, 31, 23, 59, 59, 1924991999U}
    ));

TEST(TimeSyncTrackerTest, StartsUnknownAndResetsCleanly)
{
    TimeSyncTracker tracker;
    EXPECT_EQ(tracker.status(1000).source, TimeSource::Unknown);

    tracker.record(TimeSource::GPS, 900);
    tracker.reset();

    const auto status = tracker.status(1000);
    EXPECT_EQ(status.source, TimeSource::Unknown);
    EXPECT_EQ(status.synced_at, 0U);
    EXPECT_EQ(status.age_seconds, 0U);
}

TEST(TimeSyncTrackerTest, ReportsSourceAndElapsedAge)
{
    TimeSyncTracker tracker;
    tracker.record(TimeSource::Companion, 1000);

    const auto status = tracker.status(1125);
    EXPECT_EQ(status.source, TimeSource::Companion);
    EXPECT_EQ(status.synced_at, 1000U);
    EXPECT_EQ(status.age_seconds, 125U);
}

TEST(TimeSyncTrackerTest, ClockMovingBackwardsDoesNotUnderflowAge)
{
    TimeSyncTracker tracker;
    tracker.record(TimeSource::Manual, 2000);
    EXPECT_EQ(tracker.status(1900).age_seconds, 0U);
}

TEST(TimeSyncTrackerTest, FormatsSourceAndAgeForSettings)
{
    char out[48];
    sigurdos::mesh::formatTimeSyncStatus(
        {TimeSource::GPS, 1000, 125}, out, sizeof(out));
    EXPECT_STREQ(out, "GPS, 2m ago");

    sigurdos::mesh::formatTimeSyncStatus(
        {TimeSource::Unknown, 0, 0}, out, sizeof(out));
    EXPECT_STREQ(out, "Unknown");
}

} // namespace
