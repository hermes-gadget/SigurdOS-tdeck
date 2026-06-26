#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>

#include "mesh/message_store.h"

namespace {

sigurdos::mesh::StoredMessage makeMsg(const char* conversation,
                                      const char* sender,
                                      const char* text,
                                      uint32_t ts,
                                      bool self,
                                      bool channel)
{
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, conversation, sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, sender, sizeof(msg.sender) - 1);
    std::strncpy(msg.text, text, sizeof(msg.text) - 1);
    msg.timestamp = ts;
    msg.is_self = self;
    msg.is_channel = channel;
    msg.rssi = -70;
    msg.snr_quarters = 12;
    for (int i = 0; i < 6; i++) msg.sender_prefix[i] = (uint8_t)(0xA0 + i);
    return msg;
}

class MessageStoreTest : public ::testing::Test {
protected:
    char path[128]{};

    void SetUp() override {
        std::snprintf(path, sizeof(path), "/tmp/sigurdos_msg_store_%d.bin",
                      ::testing::UnitTest::GetInstance()->random_seed());
        sigurdos::mesh::messageStoreSetNativePath(path);
        std::remove(path);
        ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
        ASSERT_TRUE(sigurdos::mesh::messageStoreClear());
    }

    void TearDown() override {
        std::remove(path);
    }
};

TEST_F(MessageStoreTest, AppendLoadAndDedup) {
    auto msg = makeMsg("DM: Alice", "Alice", "hello", 42, false, false);

    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(msg));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(msg));
    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 1);

    sigurdos::mesh::StoredMessage out[4]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 4);
    ASSERT_EQ(n, 1);
    EXPECT_STREQ(out[0].conversation, "DM: Alice");
    EXPECT_STREQ(out[0].sender, "Alice");
    EXPECT_STREQ(out[0].text, "hello");
    EXPECT_EQ(out[0].timestamp, 42u);
    EXPECT_FALSE(out[0].is_self);
}

TEST_F(MessageStoreTest, PathLenRoundTrips) {
    auto msg = makeMsg("Public", "Alice", "Alice: hi", 100, false, true);
    msg.path_len = 0x83;  // distinct from the zero-init default
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(msg));

    sigurdos::mesh::StoredMessage out[2]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 2);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(out[0].path_len, 0x83);
}

TEST_F(MessageStoreTest, LoadRecentFiltersAndPreservesOrder) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "one", 1, false, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("#test", "Bob", "two", 2, false, true)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "self", "three", 3, true, false)));

    sigurdos::mesh::StoredMessage out[4]{};
    int n = sigurdos::mesh::messageStoreLoadRecent("DM: Alice", out, 4);
    ASSERT_EQ(n, 2);
    EXPECT_STREQ(out[0].text, "one");
    EXPECT_STREQ(out[1].text, "three");
}

TEST_F(MessageStoreTest, StoreRotatesToNewestRecords) {
    for (uint32_t i = 1; i <= 70; i++) {
        char text[24];
        std::snprintf(text, sizeof(text), "msg%lu", (unsigned long)i);
        EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
            makeMsg("DM: Alice", "Alice", text, i, false, false)));
    }

    EXPECT_EQ(sigurdos::mesh::messageStoreCount(), 64);
    sigurdos::mesh::StoredMessage out[64]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 64);
    ASSERT_EQ(n, 64);
    EXPECT_EQ(out[0].timestamp, 7u);
    EXPECT_EQ(out[63].timestamp, 70u);
}

TEST_F(MessageStoreTest, MarkAckedUpdatesStoredMessage) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "self", "sent", 77, true, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkAcked("DM: Alice", 77));

    sigurdos::mesh::StoredMessage out[2]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 2);
    ASSERT_EQ(n, 1);
    EXPECT_TRUE(out[0].acked);
}

TEST_F(MessageStoreTest, StoreIdIsMonotonic) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "first", 1, false, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "second", 2, false, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Bob", "Bob", "third", 3, false, false)));

    sigurdos::mesh::StoredMessage out[4]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 4);
    ASSERT_EQ(n, 3);
    EXPECT_EQ(out[0].store_id, 0u);
    EXPECT_EQ(out[1].store_id, 1u);
    EXPECT_EQ(out[2].store_id, 2u);
}

TEST_F(MessageStoreTest, MarkCompanionSentMarksOnlyOneRecord) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "one", 1, false, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "two", 2, false, false)));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Bob", "Bob", "three", 3, false, false)));

    // Mark only record with store_id=1
    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkCompanionSent(1));

    sigurdos::mesh::StoredMessage out[4]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 4);
    ASSERT_EQ(n, 3);
    // Record 0 (store_id=0): NOT marked
    EXPECT_FALSE(out[0].companion_sent);
    // Record 1 (store_id=1): marked
    EXPECT_TRUE(out[1].companion_sent);
    // Record 2 (store_id=2): NOT marked
    EXPECT_FALSE(out[2].companion_sent);
}

TEST_F(MessageStoreTest, MarkCompanionSentNotFoundReturnsFalse) {
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(
        makeMsg("DM: Alice", "Alice", "one", 1, false, false)));
    EXPECT_FALSE(sigurdos::mesh::messageStoreMarkCompanionSent(999));
}

TEST_F(MessageStoreTest, LoadUnsentOnlyReturnsUnmarkedRecords) {
    auto m1 = makeMsg("DM: Alice", "Alice", "one", 1, false, false);
    auto m2 = makeMsg("DM: Alice", "Alice", "two", 2, false, false);
    auto m3 = makeMsg("DM: Bob", "Bob", "three", 3, false, false);

    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(m1));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(m2));
    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(m3));

    // Mark record 1 as sent
    EXPECT_TRUE(sigurdos::mesh::messageStoreMarkCompanionSent(1));

    sigurdos::mesh::StoredMessage out[4]{};
    int n = sigurdos::mesh::messageStoreLoadUnsent(out, 4);
    ASSERT_EQ(n, 2);
    EXPECT_EQ(out[0].store_id, 0u);
    EXPECT_EQ(out[1].store_id, 2u);
}

TEST_F(MessageStoreTest, MetadataRoundTrips) {
    auto msg = makeMsg("DM: Alice", "Alice", "hello", 42, false, false);
    msg.txt_type = 2;  // COMPANION_TXT_SIGNED_PLAIN
    msg.extra_len = 4;
    msg.extra[0] = 0xDE;
    msg.extra[1] = 0xAD;
    msg.extra[2] = 0xBE;
    msg.extra[3] = 0xEF;
    msg.sender_prefix[0] = 0x11;
    msg.sender_prefix[1] = 0x22;
    msg.sender_prefix[2] = 0x33;
    msg.sender_prefix[3] = 0x44;
    msg.sender_prefix[4] = 0x55;
    msg.sender_prefix[5] = 0x66;

    EXPECT_TRUE(sigurdos::mesh::messageStoreAppend(msg));

    sigurdos::mesh::StoredMessage out[2]{};
    int n = sigurdos::mesh::messageStoreLoadAll(out, 2);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(out[0].txt_type, 2u);
    EXPECT_EQ(out[0].extra_len, 4u);
    EXPECT_EQ(out[0].extra[0], 0xDE);
    EXPECT_EQ(out[0].extra[1], 0xAD);
    EXPECT_EQ(out[0].extra[2], 0xBE);
    EXPECT_EQ(out[0].extra[3], 0xEF);
    EXPECT_EQ(out[0].sender_prefix[0], 0x11);
    EXPECT_EQ(out[0].sender_prefix[1], 0x22);
    EXPECT_EQ(out[0].sender_prefix[2], 0x33);
}

} // namespace
