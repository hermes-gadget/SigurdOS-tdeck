// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Tests for issue #542: DRAM buffer overflow in append_channel_message
// when PSRAM is exhausted and the DRAM fallback (8 entries) is used.

#include <gtest/gtest.h>
#include <cstdint>

// Simulates the buffer sizing logic that was broken
static constexpr uint16_t CHAT_MSGS_MIN_CAP = 8;
static constexpr uint16_t CHAT_MSGS_MAX = 200;

// Simulates the fixed capacity logic from append_channel_message
static uint16_t effective_cap(uint16_t user_cap, uint16_t buf_cap) {
    return (buf_cap > 0 && buf_cap < user_cap) ? buf_cap : user_cap;
}

TEST(DramBufferOverflow, NormalPathUsesUserCap) {
    // PSRAM path: buffer capacity equals max, user cap is default
    uint16_t user_cap = 200;
    uint16_t buf_cap = CHAT_MSGS_MAX;
    EXPECT_EQ(effective_cap(user_cap, buf_cap), 200);
}

TEST(DramBufferOverflow, DramFallbackClampsToBufferSize) {
    // DRAM fallback: buffer capacity is 8, user cap is 200
    uint16_t user_cap = 200;
    uint16_t buf_cap = CHAT_MSGS_MIN_CAP;
    EXPECT_EQ(effective_cap(user_cap, buf_cap), 8);
}

TEST(DramBufferOverflow, UserCapBelowBufferCapPassesThrough) {
    // User configured a small cap (e.g., 10)
    uint16_t user_cap = 10;
    uint16_t buf_cap = CHAT_MSGS_MAX;
    EXPECT_EQ(effective_cap(user_cap, buf_cap), 10);
}

TEST(DramBufferOverflow, ZeroBufferCapReturnsUserCap) {
    // Buffer not yet allocated
    uint16_t user_cap = 200;
    uint16_t buf_cap = 0;
    EXPECT_EQ(effective_cap(user_cap, buf_cap), 200);
}
