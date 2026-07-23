// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "ui/screens_common.h"

namespace {

class UiScreenLoadTest : public ::testing::Test {
protected:
    void SetUp() override { lvgl_mock::reset_screen_tracking(); }
};

TEST_F(UiScreenLoadTest, ProductionLoaderUsesOneSynchronousManualDeletePath)
{
    lv_obj_t home{};
    lv_obj_t chat{};

    sigurdos::ui::show_screen(&home);
    sigurdos::ui::show_screen(&chat);

    EXPECT_EQ(lvgl_mock::active_screen, &chat);
    EXPECT_EQ(lvgl_mock::last_deleted_screen, &home);
    EXPECT_EQ(lvgl_mock::screen_load_count, 2);
    EXPECT_EQ(lvgl_mock::screen_delete_count, 1);
    EXPECT_EQ(lvgl_mock::last_load_animation, LV_SCR_LOAD_ANIM_NONE);
    EXPECT_EQ(lvgl_mock::last_load_duration, 0);
    EXPECT_EQ(lvgl_mock::last_load_delay, 0);
    EXPECT_FALSE(lvgl_mock::last_load_auto_delete);
}

TEST_F(UiScreenLoadTest, FiftyChatHomeChannelCyclesDoNotMixLoadPoliciesOrLeakRoots)
{
    lv_obj_t roots[101]{};

    sigurdos::ui::show_screen(&roots[0]); // Home
    for (int cycle = 0; cycle < 50; ++cycle) {
        lv_obj_t* chat_or_channel = &roots[cycle * 2 + 1];
        lv_obj_t* home = &roots[cycle * 2 + 2];

        sigurdos::ui::show_screen(chat_or_channel);
        EXPECT_EQ(lvgl_mock::active_screen, chat_or_channel) << "cycle " << cycle;
        EXPECT_EQ(lvgl_mock::last_deleted_screen, &roots[cycle * 2]) << "cycle " << cycle;
        EXPECT_FALSE(lvgl_mock::last_load_auto_delete) << "cycle " << cycle;
        EXPECT_EQ(lvgl_mock::last_load_animation, LV_SCR_LOAD_ANIM_NONE) << "cycle " << cycle;

        sigurdos::ui::show_screen(home);
        EXPECT_EQ(lvgl_mock::active_screen, home) << "cycle " << cycle;
        EXPECT_EQ(lvgl_mock::last_deleted_screen, chat_or_channel) << "cycle " << cycle;
        EXPECT_FALSE(lvgl_mock::last_load_auto_delete) << "cycle " << cycle;
    }

    EXPECT_EQ(lvgl_mock::screen_load_count, 101);
    EXPECT_EQ(lvgl_mock::screen_delete_count, 100);
}

TEST_F(UiScreenLoadTest, ReloadingActiveRootDoesNotDeleteIt)
{
    lv_obj_t chat{};

    sigurdos::ui::show_screen(&chat);
    sigurdos::ui::show_screen(&chat);

    EXPECT_EQ(lvgl_mock::active_screen, &chat);
    EXPECT_EQ(lvgl_mock::screen_load_count, 2);
    EXPECT_EQ(lvgl_mock::screen_delete_count, 0);
}

} // namespace
