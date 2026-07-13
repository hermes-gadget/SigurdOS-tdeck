// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include "ui/list_window.h"

using namespace sigurdos::ui;

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(ListWindowTest, NewestPageIsBoundedAndOrdered)
{
    EXPECT_EQ(24, CHAT_LIST_RENDER_CAPACITY);
    EXPECT_EQ(7, PACKET_LIST_RENDER_CAPACITY);
    const ListWindow window = list_window_from_newest(200, 24, 0);
    EXPECT_EQ(176, window.start);
    EXPECT_EQ(200, window.end);
    EXPECT_EQ(24, window.count());
}

TEST(ListWindowTest, OlderAndPartialPagesClampSafely)
{
    ListWindow window = list_window_from_newest(50, 7, 1);
    EXPECT_EQ(36, window.start);
    EXPECT_EQ(43, window.end);

    window = list_window_from_newest(50, 7, 99);
    EXPECT_EQ(0, window.start);
    EXPECT_EQ(1, window.end);
    EXPECT_EQ(7, list_page_count(49, 7));
    EXPECT_EQ(8, list_page_count(50, 7));
}

TEST(ListWindowTest, NewArrivalOffsetPreservesWindowWhileReading)
{
    const ListWindow before = list_window_from_newest_offset(200, 24, 0);
    const ListWindow after = list_window_from_newest_offset(201, 24, 1);
    EXPECT_EQ(before.start, after.start);
    EXPECT_EQ(before.end, after.end);
    EXPECT_EQ(0, list_clamp_newest_offset(-5, 200));
    EXPECT_EQ(199, list_clamp_newest_offset(999, 200));
}

TEST(ListWindowTest, EmptyAndInvalidInputsProduceEmptyWindow)
{
    EXPECT_EQ(0, list_window_from_newest(0, 24, 0).count());
    EXPECT_EQ(0, list_window_from_newest(10, 0, 0).count());
    EXPECT_EQ(0, list_clamp_page(-1, 10, 4));
}

TEST(ListWindowTest, PageMathDoesNotGrowWithBackingStore)
{
    for (int total : {24, 50, 200, 350, 1000}) {
        const ListWindow window = list_window_from_newest(total, 24, 0);
        EXPECT_LE(window.count(), 24);
        EXPECT_EQ(total, window.end);
    }
}
