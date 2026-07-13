// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include "hal/display_deadline.h"
#include "ui/display_settings_helpers.h"
#include "ui/lv_timer_owner.h"

using sigurdos::ui::LvTimerOwner;
using sigurdos::ui::normalize_auto_off_timeout;

using sigurdos::hal::DisplayAutoOffDeadline;

TEST(UiTimerOwner, CancelsAttachedTimerExactlyOnce)
{
    lv_timer_t timer{};
    {
        LvTimerOwner owner;
        owner.attach(&timer);
        owner.cancel();
        owner.cancel();
    }
    EXPECT_EQ(timer.delete_count, 1);
}

TEST(UiTimerOwner, ReplacingTimerCancelsPreviousOwner)
{
    lv_timer_t first{};
    lv_timer_t second{};
    {
        LvTimerOwner owner;
        owner.attach(&first);
        owner.attach(&second);
        EXPECT_EQ(first.delete_count, 1);
        EXPECT_EQ(second.delete_count, 0);
    }
    EXPECT_EQ(second.delete_count, 1);
}

TEST(UiTimerOwner, CompletedTimerIsNotDeletedByDestructorAgain)
{
    lv_timer_t timer{};
    {
        LvTimerOwner owner;
        owner.attach(&timer);
        owner.complete(&timer);
        EXPECT_EQ(owner.get(), nullptr);
    }
    EXPECT_EQ(timer.delete_count, 1);
}

TEST(AutoOffTimeout, PreservesEverySupportedValue)
{
    EXPECT_EQ(normalize_auto_off_timeout(0), 0);
    EXPECT_EQ(normalize_auto_off_timeout(15), 15);
    EXPECT_EQ(normalize_auto_off_timeout(30), 30);
    EXPECT_EQ(normalize_auto_off_timeout(60), 60);
    EXPECT_EQ(normalize_auto_off_timeout(120), 120);
}

TEST(AutoOffTimeout, NormalizesUnsupportedValuesToThirtySeconds)
{
    EXPECT_EQ(normalize_auto_off_timeout(1), 30);
    EXPECT_EQ(normalize_auto_off_timeout(45), 30);
    EXPECT_EQ(normalize_auto_off_timeout(65535), 30);
}

TEST(AutoOffDeadline, FiresAtTheDeadlineNotBefore)
{
    DisplayAutoOffDeadline deadline;
    deadline.arm(1000, 30000);

    EXPECT_FALSE(deadline.reached(30999));
    EXPECT_TRUE(deadline.reached(31000));
    EXPECT_TRUE(deadline.reached(31001));
}

TEST(AutoOffDeadline, RemainsCorrectAcrossMillisWrap)
{
    DisplayAutoOffDeadline deadline;
    deadline.arm(0xFFFFFFF0U, 32);

    EXPECT_FALSE(deadline.reached(0xFFFFFFF5U));
    EXPECT_FALSE(deadline.reached(0x0000000FU));
    EXPECT_TRUE(deadline.reached(0x00000010U));
    EXPECT_TRUE(deadline.reached(0x00000011U));
}

TEST(AutoOffDeadline, ZeroTimeoutStaysDisabledAcrossWrap)
{
    DisplayAutoOffDeadline deadline;
    deadline.arm(0xFFFFFFF0U, 0);

    EXPECT_FALSE(deadline.enabled);
    EXPECT_FALSE(deadline.reached(0xFFFFFFF0U));
    EXPECT_FALSE(deadline.reached(0x00000000U));
    EXPECT_FALSE(deadline.reached(0x7FFFFFFFU));
}
