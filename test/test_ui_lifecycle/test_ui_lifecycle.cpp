// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include "ui/display_settings_helpers.h"
#include "ui/lv_timer_owner.h"

using sigurdos::ui::LvTimerOwner;
using sigurdos::ui::normalize_auto_off_timeout;

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
