// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Characterization tests for the per-channel message buffer extracted from
// chat_screen.cpp (ARCH-001, issue #820). These pin the legacy semantics:
// PSRAM-first allocation with DRAM fallback (#542), FIFO eviction at the
// effective cap, oldest-first trimming, and buffer handoff for the channel
// remap.

#include <gtest/gtest.h>
#include "ui/chat_message_buffer.h"

#include <esp_heap_caps.h>
#include <cstring>
#include <string>

namespace {

using sigurdos::ui::ChannelMessage;
using sigurdos::ui::ChatMessageBuffer;

constexpr uint16_t FULL_CAP = 20;
constexpr uint16_t FALLBACK_CAP = 4;

class ChatMessageBufferTest : public ::testing::Test {
protected:
    void SetUp() override { heap_caps_mock_fail_spiram() = 0; }
    void TearDown() override { heap_caps_mock_fail_spiram() = 0; }

    ChannelMessage* append(ChatMessageBuffer& buf, const char* text,
                           uint16_t user_cap = FULL_CAP)
    {
        return buf.append("alice", text, 1000, false, user_cap,
                          FULL_CAP, FALLBACK_CAP);
    }
};

TEST_F(ChatMessageBufferTest, AllocatesLazilyAtFullCapacity)
{
    ChatMessageBuffer buf;
    EXPECT_FALSE(buf.allocated());
    EXPECT_EQ(buf.count(), 0);

    ASSERT_NE(append(buf, "hello"), nullptr);
    EXPECT_TRUE(buf.allocated());
    EXPECT_EQ(buf.capacity(), FULL_CAP);
    EXPECT_EQ(buf.count(), 1);
    EXPECT_STREQ(buf.at(0).text, "hello");
    EXPECT_STREQ(buf.at(0).sender, "alice");
    EXPECT_FALSE(buf.at(0).acked);
}

TEST_F(ChatMessageBufferTest, FallsBackToReducedDramCapacity)
{
    // #542: PSRAM exhausted — the buffer must come from DRAM at the reduced
    // capacity, and appends must respect that capacity, not the user cap.
    heap_caps_mock_fail_spiram() = 1;
    ChatMessageBuffer buf;
    ASSERT_NE(append(buf, "m0"), nullptr);
    EXPECT_EQ(buf.capacity(), FALLBACK_CAP);

    for (int i = 1; i < FALLBACK_CAP + 3; i++) {
        char text[8];
        snprintf(text, sizeof(text), "m%d", i);
        ASSERT_NE(append(buf, text), nullptr);
    }
    EXPECT_EQ(buf.count(), FALLBACK_CAP);
    // Oldest were evicted; newest survive in order.
    EXPECT_STREQ(buf.at(0).text, "m3");
    EXPECT_STREQ(buf.at(FALLBACK_CAP - 1).text, "m6");
}

TEST_F(ChatMessageBufferTest, EvictsOldestAtUserCap)
{
    ChatMessageBuffer buf;
    for (int i = 0; i < 8; i++) {
        char text[8];
        snprintf(text, sizeof(text), "m%d", i);
        ASSERT_NE(append(buf, text, 5), nullptr);
    }
    EXPECT_EQ(buf.count(), 5);
    EXPECT_STREQ(buf.at(0).text, "m3");
    EXPECT_STREQ(buf.at(4).text, "m7");
}

TEST_F(ChatMessageBufferTest, TrimDropsOldestKeepsNewest)
{
    ChatMessageBuffer buf;
    for (int i = 0; i < 10; i++) {
        char text[8];
        snprintf(text, sizeof(text), "m%d", i);
        ASSERT_NE(append(buf, text), nullptr);
    }
    buf.trim(4);
    EXPECT_EQ(buf.count(), 4);
    EXPECT_STREQ(buf.at(0).text, "m6");
    EXPECT_STREQ(buf.at(3).text, "m9");

    buf.trim(0);                 // legacy: cap 0 is a no-op
    EXPECT_EQ(buf.count(), 4);
}

TEST_F(ChatMessageBufferTest, TruncatesAndNullTerminatesFields)
{
    ChatMessageBuffer buf;
    const std::string long_sender(64, 's');
    const std::string long_text(400, 't');
    ChannelMessage* msg = buf.append(long_sender.c_str(), long_text.c_str(),
                                     42, true, FULL_CAP, FULL_CAP, FALLBACK_CAP);
    ASSERT_NE(msg, nullptr);
    EXPECT_EQ(strlen(msg->sender), sizeof(msg->sender) - 1);
    EXPECT_EQ(strlen(msg->text), sizeof(msg->text) - 1);
    EXPECT_EQ(msg->timestamp, 42u);
    EXPECT_TRUE(msg->is_self);

    ChannelMessage* null_msg = buf.append(nullptr, nullptr, 1, false,
                                          FULL_CAP, FULL_CAP, FALLBACK_CAP);
    ASSERT_NE(null_msg, nullptr);
    EXPECT_STREQ(null_msg->sender, "");
    EXPECT_STREQ(null_msg->text, "");
}

TEST_F(ChatMessageBufferTest, MarksNewestMessageAcked)
{
    ChatMessageBuffer buf;
    buf.markLastAcked();                       // no-op when empty
    ASSERT_NE(append(buf, "one"), nullptr);
    ASSERT_NE(append(buf, "two"), nullptr);
    buf.markLastAcked();
    EXPECT_FALSE(buf.at(0).acked);
    EXPECT_TRUE(buf.at(1).acked);
}

TEST_F(ChatMessageBufferTest, TakeFromMovesBufferAndEmptiesSource)
{
    ChatMessageBuffer src;
    ASSERT_NE(append(src, "kept"), nullptr);

    ChatMessageBuffer dst;
    ASSERT_NE(append(dst, "discarded"), nullptr);

    dst.takeFrom(src);
    EXPECT_FALSE(src.allocated());
    EXPECT_EQ(src.count(), 0);
    ASSERT_TRUE(dst.allocated());
    ASSERT_EQ(dst.count(), 1);
    EXPECT_STREQ(dst.at(0).text, "kept");

    dst.takeFrom(dst);                         // self-move is a no-op
    ASSERT_EQ(dst.count(), 1);
    EXPECT_STREQ(dst.at(0).text, "kept");
}

TEST_F(ChatMessageBufferTest, ReleaseResetsToUnallocated)
{
    ChatMessageBuffer buf;
    ASSERT_NE(append(buf, "gone"), nullptr);
    buf.release();
    EXPECT_FALSE(buf.allocated());
    EXPECT_EQ(buf.count(), 0);
    EXPECT_EQ(buf.capacity(), 0);

    // Reusable after release.
    ASSERT_NE(append(buf, "back"), nullptr);
    EXPECT_EQ(buf.count(), 1);
}

TEST_F(ChatMessageBufferTest, TotalAllocationFailureRejectsAppend)
{
    heap_caps_mock_fail_spiram() = 1;
    ChatMessageBuffer buf;
    // Fallback capacity of zero models DRAM exhaustion as well.
    EXPECT_EQ(buf.append("a", "b", 1, false, FULL_CAP, FULL_CAP, 0), nullptr);
    EXPECT_FALSE(buf.allocated());
}

} // namespace
