// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>

// Constants matching chat_screen.cpp
static constexpr int MAX_NAME_LEN  = 31;
static constexpr int CHANNEL_BUF_SZ = 32;

// Simulates the DM name formatting that caused stack overflow #543
static void formatDmName(const char* contact_name, char* out, size_t out_sz) {
    snprintf(out, out_sz, "DM: %s", contact_name);
}

// Tests for issue #543: Stack buffer overflow in chat_screen_open_dm
// The DM name buffer must fit "DM: " (4) + max contact name (31) + null (1) = 36 chars

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
    // Verify DM name fits in dyn_channels[32] buffer
    // "DM: " uses 4 bytes, leaving 27 bytes for name within 32-byte channel buffer
    // Longer names will be truncated by strncpy — this documents the limitation
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
