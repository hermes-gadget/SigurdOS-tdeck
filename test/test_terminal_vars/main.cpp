// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include <cstring>
#include "ui/terminal_var_policy.h"

using sigurdos::ui::terminal_var_rewrite;

TEST(TerminalVarPolicy, SetReplacesDuplicatesAndPreservesOtherLines)
{
    const char input[] = "a=1\ntarget=old\nb=2\ntarget=duplicate\n";
    char output[64]; size_t length = 0; bool found = false;
    ASSERT_TRUE(terminal_var_rewrite(input, sizeof(input) - 1, "target", "new",
                                     output, sizeof(output), &length, &found));
    EXPECT_TRUE(found);
    EXPECT_STREQ(output, "a=1\nb=2\ntarget=new\n");
    EXPECT_EQ(length, std::strlen(output));
}

TEST(TerminalVarPolicy, DeleteReportsPresenceAndOmitsMatchingLine)
{
    const char input[] = "a=1\ntarget=old\nb=2\n";
    char output[64]; size_t length = 0; bool found = false;
    ASSERT_TRUE(terminal_var_rewrite(input, sizeof(input) - 1, "target", nullptr,
                                     output, sizeof(output), &length, &found));
    EXPECT_TRUE(found);
    EXPECT_STREQ(output, "a=1\nb=2\n");
}

TEST(TerminalVarPolicy, RefusesOutputBeyondItsBound)
{
    const char input[] = "a=123456789\n";
    char output[8]; size_t length = 0; bool found = false;
    EXPECT_FALSE(terminal_var_rewrite(input, sizeof(input) - 1, "b", "2",
                                      output, sizeof(output), &length, &found));
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
