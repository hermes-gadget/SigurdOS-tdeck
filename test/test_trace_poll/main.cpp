// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include "ui/trace_poll_policy.h"

using namespace sigurdos::ui;

TEST(TracePollPolicy, AcceptsOnlyTheExpectedTag)
{
    EXPECT_EQ(trace_poll_decision(10, 100, true, 42, 42), TracePollDecision::Accept);
    EXPECT_EQ(trace_poll_decision(10, 100, true, 42, 41),
              TracePollDecision::DiscardUnrelated);
}

TEST(TracePollPolicy, TimesOutAtDeadlineAndHandlesMillisWrap)
{
    EXPECT_EQ(trace_poll_decision(99, 100, false, 1, 0), TracePollDecision::Pending);
    EXPECT_EQ(trace_poll_decision(100, 100, false, 1, 0), TracePollDecision::TimedOut);
    EXPECT_EQ(trace_poll_decision(20, UINT32_MAX - 10, false, 1, 0),
              TracePollDecision::TimedOut);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
