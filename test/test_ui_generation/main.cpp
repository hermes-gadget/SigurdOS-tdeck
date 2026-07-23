// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include "ui/generation_owner.h"

using namespace sigurdos::ui;

TEST(UiGenerationOwner, MatchesOnlyTheExactOwnerAndGeneration)
{
    int first = 0;
    int second = 0;
    EXPECT_TRUE(ui_generation_matches(&first, 7, &first, 7));
    EXPECT_FALSE(ui_generation_matches(&first, 7, &second, 7));
    EXPECT_FALSE(ui_generation_matches(&first, 7, &first, 8));
    EXPECT_FALSE(ui_generation_matches(nullptr, 7, nullptr, 7));
}

TEST(UiGenerationOwner, SkipsZeroWhenTheCounterWraps)
{
    EXPECT_EQ(next_ui_generation(0), 1U);
    EXPECT_EQ(next_ui_generation(41), 42U);
    EXPECT_EQ(next_ui_generation(UINT32_MAX), 1U);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
