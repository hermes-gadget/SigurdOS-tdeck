// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

#include "ui/room_fetch_policy.h"

using sigurdos::ui::RoomFetchUiAction;
using sigurdos::ui::room_fetch_ui_action;

namespace {

std::string read_project_file(const char* path)
{
    const char* prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    for (const char* prefix : prefixes) {
        std::ifstream in(std::string(prefix) + path);
        if (in.good()) {
            return std::string(std::istreambuf_iterator<char>(in), {});
        }
    }
    return {};
}

TEST(RoomFetchUiPolicy, RejectedRequestKeepsDialogForCorrection)
{
    EXPECT_EQ(room_fetch_ui_action(false), RoomFetchUiAction::KeepDialog);
}

TEST(RoomFetchUiPolicy, AcceptedRequestConfirmsAndNavigates)
{
    EXPECT_EQ(room_fetch_ui_action(true), RoomFetchUiAction::ConfirmAndNavigate);
}

TEST(RoomFetchUiPolicy, AcceptedRequestReturnsBeforeTouchingDeletedDialog)
{
    const std::string source = read_project_file(
        "src/ui/screens/screen_contacts.cpp");
    ASSERT_FALSE(source.empty());

    const size_t callback_start = source.find(
        "lv_obj_add_event_cb(fetch_btn");
    const size_t navigate = source.find(
        "navigate_to(sigurdos::ui::Screen::Chat)", callback_start);
    const size_t terminal_return = source.find("return;", navigate);
    const size_t stale_target_access = source.find(
        "lv_event_get_target(le)", navigate);
    const size_t callback_end = source.find(
        "}, LV_EVENT_CLICKED, nullptr);", callback_start);

    ASSERT_NE(callback_start, std::string::npos);
    ASSERT_NE(navigate, std::string::npos);
    ASSERT_NE(terminal_return, std::string::npos);
    ASSERT_NE(callback_end, std::string::npos);
    EXPECT_LT(navigate, terminal_return);
    EXPECT_LT(terminal_return, callback_end);
    if (stale_target_access != std::string::npos &&
        stale_target_access < callback_end) {
        EXPECT_LT(terminal_return, stale_target_access);
    }
}

} // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
