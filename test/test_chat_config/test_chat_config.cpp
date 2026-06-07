// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include <cstdint>

#include "ui/chat_screen.h"

namespace {

using sigurdos::ui::CHAT_SCREEN_MESSAGE_CAP_DEFAULT;
using sigurdos::ui::CHAT_SCREEN_MESSAGE_CAP_MAX;
using sigurdos::ui::CHAT_SCREEN_MESSAGE_CAP_MIN;
using sigurdos::ui::chat_screen_filter_accepts_channel;
using sigurdos::ui::chat_screen_is_dm_name;
using sigurdos::ui::chat_screen_normalize_message_cap;

TEST(ChatConfig, ZeroUsesDefaultMessageCap) {
    EXPECT_EQ(chat_screen_normalize_message_cap(0), CHAT_SCREEN_MESSAGE_CAP_DEFAULT);
}

TEST(ChatConfig, ValuesBelowMinimumClampUp) {
    EXPECT_EQ(chat_screen_normalize_message_cap(1), CHAT_SCREEN_MESSAGE_CAP_MIN);
    EXPECT_EQ(chat_screen_normalize_message_cap(CHAT_SCREEN_MESSAGE_CAP_MIN - 1),
              CHAT_SCREEN_MESSAGE_CAP_MIN);
}

TEST(ChatConfig, MinimumAndMaximumAreAccepted) {
    EXPECT_EQ(chat_screen_normalize_message_cap(CHAT_SCREEN_MESSAGE_CAP_MIN),
              CHAT_SCREEN_MESSAGE_CAP_MIN);
    EXPECT_EQ(chat_screen_normalize_message_cap(CHAT_SCREEN_MESSAGE_CAP_MAX),
              CHAT_SCREEN_MESSAGE_CAP_MAX);
}

TEST(ChatConfig, MiddleValuesPassThrough) {
    EXPECT_EQ(chat_screen_normalize_message_cap(64), static_cast<uint16_t>(64));
}

TEST(ChatConfig, ValuesAboveMaximumClampDown) {
    EXPECT_EQ(chat_screen_normalize_message_cap(CHAT_SCREEN_MESSAGE_CAP_MAX + 1),
              CHAT_SCREEN_MESSAGE_CAP_MAX);
    EXPECT_EQ(chat_screen_normalize_message_cap(UINT16_MAX), CHAT_SCREEN_MESSAGE_CAP_MAX);
}

TEST(ChatConfig, DmNameDetectionUsesConversationPrefix) {
    EXPECT_TRUE(chat_screen_is_dm_name("DM: Alice"));
    EXPECT_TRUE(chat_screen_is_dm_name("DM:"));
    EXPECT_FALSE(chat_screen_is_dm_name("Public"));
    EXPECT_FALSE(chat_screen_is_dm_name("#general"));
    EXPECT_FALSE(chat_screen_is_dm_name(nullptr));
}

TEST(ChatConfig, ChannelFilterKeepsPublicAndHashtagChannels) {
    EXPECT_TRUE(chat_screen_filter_accepts_channel(1, "Public"));
    EXPECT_TRUE(chat_screen_filter_accepts_channel(1, "#general"));
    EXPECT_FALSE(chat_screen_filter_accepts_channel(1, "DM: Alice"));
    EXPECT_FALSE(chat_screen_filter_accepts_channel(1, ""));
    EXPECT_FALSE(chat_screen_filter_accepts_channel(1, nullptr));
}

TEST(ChatConfig, DmFilterKeepsOnlyDmConversations) {
    EXPECT_TRUE(chat_screen_filter_accepts_channel(2, "DM: Alice"));
    EXPECT_FALSE(chat_screen_filter_accepts_channel(2, "Public"));
    EXPECT_FALSE(chat_screen_filter_accepts_channel(2, "#general"));
}

} // anonymous namespace
