// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include "ui/system_action_policy.h"

using sigurdos::ui::system_reboot_allowed;

TEST(SystemActionPolicy, RebootRequiresEveryUpdaterToBeIdle)
{
    EXPECT_TRUE(system_reboot_allowed(false, false));
    EXPECT_FALSE(system_reboot_allowed(true, false));
    EXPECT_FALSE(system_reboot_allowed(false, true));
    EXPECT_FALSE(system_reboot_allowed(true, true));
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
