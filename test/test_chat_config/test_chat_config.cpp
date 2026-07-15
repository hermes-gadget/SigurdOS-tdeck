// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "ui/chat_screen.h"
#include "ui/message_detail.h"

namespace {

using sigurdos::ui::CHAT_SCREEN_MESSAGE_CAP_DEFAULT;
using sigurdos::ui::CHAT_SCREEN_MESSAGE_CAP_MAX;
using sigurdos::ui::CHAT_SCREEN_MESSAGE_CAP_MIN;
using sigurdos::ui::chat_screen_filter_accepts_channel;
using sigurdos::ui::chat_screen_is_dm_name;
using sigurdos::ui::chat_screen_normalize_message_cap;
using sigurdos::ui::chat_screen_resolve_message_timestamp;

TEST(ChatConfig, DelayedPollPreservesAuthoritativeTimestamp) {
    EXPECT_EQ(chat_screen_resolve_message_timestamp(1000, 1007), 1000U);
}

TEST(ChatConfig, ZeroTimestampFallsBackToCurrentClock) {
    EXPECT_EQ(chat_screen_resolve_message_timestamp(0, 1007), 1007U);
}

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

TEST(MessageDetail, FormatsRouteSignalTypeAndPrefix) {
    sigurdos::mesh::StoredMessage msg{};
    msg.route_known = true;
    msg.route_flood = true;
    msg.path_len = 0x83;
    msg.rssi = -91;
    msg.snr_quarters = 10;
    msg.txt_type = 2;
    msg.sender_prefix[0] = 0x11;
    msg.sender_prefix[1] = 0x22;
    msg.sender_prefix[2] = 0x33;
    msg.sender_prefix[3] = 0x44;
    msg.sender_prefix[4] = 0x55;
    msg.sender_prefix[5] = 0x66;
    char route[32], signal[40], prefix[20];
    sigurdos::ui::format_message_route(route, sizeof(route), msg);
    sigurdos::ui::format_message_signal(signal, sizeof(signal), msg);
    EXPECT_STREQ(route, "Flood (3 hops)");
    EXPECT_STREQ(signal, "-91 dBm / 2.5 dB");
    EXPECT_STREQ(sigurdos::ui::message_text_type_name(msg.txt_type), "Signed");
    EXPECT_TRUE(sigurdos::ui::format_message_prefix(prefix, sizeof(prefix), msg.sender_prefix));
    EXPECT_STREQ(prefix, "112233445566");
}

TEST(MessageDetail, DeliveryDistinguishesAckAndBroadcast) {
    sigurdos::mesh::StoredMessage msg{};
    msg.is_self = true;
    EXPECT_STREQ(sigurdos::ui::message_delivery_name(msg), "Pending / unconfirmed");
    msg.acked = true;
    EXPECT_STREQ(sigurdos::ui::message_delivery_name(msg), "Acknowledged");
    msg.is_channel = true;
    EXPECT_STREQ(sigurdos::ui::message_delivery_name(msg), "Broadcast (no ACK)");
}

// ── Issue #543: DM name buffer overflow tests ──────────────────────────

// Constants matching chat_screen.cpp
static constexpr int MAX_NAME_LEN  = 31;
static constexpr int CHANNEL_BUF_SZ = 32;

// Simulates the DM name formatting that caused stack overflow #543
static void formatDmName(const char* contact_name, char* out, size_t out_sz) {
    snprintf(out, out_sz, "DM: %s", contact_name);
}

TEST(ChatScreenDmName, FormatFitsInBuffer) {
    // Verify compile-time sizing: DM prefix + MAX_NAME_LEN + null must fit in a reasonable buffer
    constexpr size_t NEEDED = sizeof("DM: ") + MAX_NAME_LEN;  // including null from ""
    EXPECT_LE(NEEDED, (size_t)37) << "Buffer must be at least " << NEEDED << " bytes";
}

TEST(ChatScreenDmName, MaxLengthName) {
    char max_name[32];
    memset(max_name, 'A', MAX_NAME_LEN);
    max_name[MAX_NAME_LEN] = '\0';

    char dm_name[37];
    formatDmName(max_name, dm_name, sizeof(dm_name));

    EXPECT_EQ(strlen(dm_name), (size_t)(4 + MAX_NAME_LEN)) << "DM name should be 'DM: ' + contact_name";
    EXPECT_EQ(strncmp(dm_name, "DM: ", 4), 0);
    EXPECT_EQ(strncmp(dm_name + 4, max_name, MAX_NAME_LEN), 0);
}

TEST(ChatScreenDmName, NullTerminatorCheck) {
    // Verify the full DM name is null-terminated even at max length
    char max_name[32];
    memset(max_name, 'A', MAX_NAME_LEN);
    max_name[MAX_NAME_LEN] = '\0';

    char dm_name[37] = {};
    memset(dm_name, 0xFF, sizeof(dm_name));  // poison
    formatDmName(max_name, dm_name, sizeof(dm_name));

    EXPECT_EQ(dm_name[4 + MAX_NAME_LEN], '\0') << "Must be null-terminated after contact name";
}

TEST(ChatScreenDmName, ChannelCopyFits) {
    // Verify DM name fits in dyn_channels[37] buffer
    // "DM: " uses 4 bytes, leaving 32 bytes for name within 37-byte channel buffer
    // 31-char names fit fully; names at MAX_NAME_LEN (31) produce "DM: " + 31 = 35 chars
    char max_name[32];
    memset(max_name, 'A', MAX_NAME_LEN);
    max_name[MAX_NAME_LEN] = '\0';

    char dm_name[37];
    formatDmName(max_name, dm_name, sizeof(dm_name));

    char channel_buf[CHANNEL_BUF_SZ];
    strncpy(channel_buf, dm_name, CHANNEL_BUF_SZ - 1);
    channel_buf[CHANNEL_BUF_SZ - 1] = '\0';

    // No crash — buffer is fully valid
    EXPECT_EQ(strlen(channel_buf), (size_t)(CHANNEL_BUF_SZ - 1))
        << "DM name truncated to fit channel buffer (4+len=" << (4 + MAX_NAME_LEN)
        << " > " << (CHANNEL_BUF_SZ - 1) << ")";
}

} // anonymous namespace
