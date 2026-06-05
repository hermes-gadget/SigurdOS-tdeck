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

} // namespace
