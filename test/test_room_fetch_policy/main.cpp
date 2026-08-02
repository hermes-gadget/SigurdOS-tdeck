// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "ui/room_fetch_policy.h"

using sigurdos::ui::RoomFetchUiAction;
using sigurdos::ui::room_fetch_ui_action;

TEST(RoomFetchUiPolicy, RejectedRequestKeepsDialogForCorrection)
{
    EXPECT_EQ(room_fetch_ui_action(false), RoomFetchUiAction::KeepDialog);
}

TEST(RoomFetchUiPolicy, AcceptedRequestConfirmsAndNavigates)
{
    EXPECT_EQ(room_fetch_ui_action(true), RoomFetchUiAction::ConfirmAndNavigate);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
