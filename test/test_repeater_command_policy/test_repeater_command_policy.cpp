// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "ui/repeater_command_policy.h"

TEST(RepeaterCommandPolicy, ReportsAcceptedCommand)
{
    char text[80];
    EXPECT_TRUE(sigurdos::ui::repeater_command_feedback(
        text, sizeof(text), true, "reboot", "Sent: %s"));
    EXPECT_STREQ(text, "Sent: reboot");
}

TEST(RepeaterCommandPolicy, ReportsRejectedCommand)
{
    char text[80];
    EXPECT_TRUE(sigurdos::ui::repeater_command_feedback(
        text, sizeof(text), false, "reboot", "Sent: %s"));
    EXPECT_STREQ(text, "Send failed: reboot");
}
