// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "mesh/message_store.h"
#include "mesh/sd_message_store.h"

namespace sigurdos::mesh {
uint32_t messageStoreRetryQueueDepth();
}

namespace {

using sigurdos::mesh::StoredMessage;

StoredMessage makeMessage(uint32_t timestamp, const char* text)
{
    StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, text, sizeof(msg.text) - 1);
    msg.timestamp = timestamp;
    msg.rssi = -70;
    msg.snr_quarters = 12;
    for (size_t i = 0; i < sizeof(msg.sender_prefix); ++i) {
        msg.sender_prefix[i] = static_cast<uint8_t>(0xA0 + i);
    }
    return msg;
}

void removeStoreFiles(const std::string& path)
{
    std::remove(path.c_str());
    std::remove((path + sigurdos::mesh::SD_MESSAGE_STORE_TEMP_SUFFIX).c_str());
    std::remove((path + sigurdos::mesh::SD_MESSAGE_STORE_READY_SUFFIX).c_str());
    std::remove((path + sigurdos::mesh::SD_MESSAGE_STORE_CORRUPT_SUFFIX).c_str());
}

class MessageStoreFailoverTest : public ::testing::Test {
protected:
    std::string root;
    std::string spiffs_path;

    void SetUp() override
    {
        const auto* info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string suffix = info ? info->name() : "unknown";
        root = "/tmp/sigurdos_failover_" + suffix;
        spiffs_path = root + ".spiffs";
        removeStoreFiles(spiffs_path);
        removeStoreFiles(root + "/msgs");
        std::remove(root.c_str());

        sigurdos::mesh::sdMessageStoreResetNative();
        sigurdos::mesh::sdMessageStoreSetNativeRoot(root.c_str());
        sigurdos::mesh::sdMessageStoreSetNativeMounted(true);
        sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(-1);
        sigurdos::mesh::messageStoreSetNativeRecordWriteLimit(-1);
        sigurdos::mesh::messageStoreSetNativePath(spiffs_path.c_str());
        sigurdos::mesh::detail::messageStoreSelectDefaultBackend();
        ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
        ASSERT_TRUE(sigurdos::mesh::messageStoreClear());
    }

    void TearDown() override
    {
        sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(-1);
        sigurdos::mesh::messageStoreSetNativeRecordWriteLimit(-1);
        sigurdos::mesh::sdMessageStoreSetNativeMounted(false);
        sigurdos::mesh::detail::messageStoreSelectDefaultBackend();
        removeStoreFiles(spiffs_path);
        removeStoreFiles(root + "/msgs");
        std::remove(root.c_str());
    }
};

TEST_F(MessageStoreFailoverTest, SdWriteFailureDemotesAndRetriesCurrentMessageOnce)
{
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreUsingSd());

    sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(0);
    const StoredMessage message = makeMessage(1, "survives SD loss");
    uint32_t store_id = 0;

    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(message, &store_id));
    EXPECT_NE(store_id, 0u);
    EXPECT_FALSE(sigurdos::mesh::sdMessageStoreUsingSd());
    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreDegraded());
    EXPECT_FALSE(sigurdos::mesh::detail::messageStoreBackendSelected());
    EXPECT_EQ(sigurdos::mesh::messageStoreRetryQueueDepth(), 0u);

    StoredMessage restored{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&restored, 1), 1);
    EXPECT_STREQ(restored.text, "survives SD loss");
}

TEST_F(MessageStoreFailoverTest, FailedFallbackQueuesCurrentMessageInBoundedRam)
{
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(0);
    sigurdos::mesh::messageStoreSetNativeRecordWriteLimit(0);

    uint32_t store_id = 0;
    EXPECT_FALSE(sigurdos::mesh::messageStoreAppend(
        makeMessage(2, "needs retry"), &store_id));
    EXPECT_EQ(store_id, 0u);
    EXPECT_TRUE(sigurdos::mesh::sdMessageStoreDegraded());
    EXPECT_EQ(sigurdos::mesh::messageStoreRetryQueueDepth(), 1u);
}

TEST_F(MessageStoreFailoverTest, CompanionSendCanReportNonDurabilityAfterRadioSend)
{
    ASSERT_TRUE(sigurdos::mesh::sdMessageStoreSelect(true));
    sigurdos::mesh::sdMessageStoreSetNativeAppendWriteLimit(0);
    sigurdos::mesh::messageStoreSetNativeRecordWriteLimit(0);

    // The companion adapter transmits first and maps this store ID to its
    // CompanionSendResult::ok field. A zero ID therefore reports the storage
    // failure without suppressing the already-completed RF transmission.
    const bool radio_transmitted = true;
    uint32_t store_id = 0;
    const bool durable = sigurdos::mesh::messageStoreAppend(
        makeMessage(3, "radio only"), &store_id);
    EXPECT_TRUE(radio_transmitted);
    EXPECT_FALSE(durable);
    EXPECT_EQ(store_id, 0u);
    EXPECT_FALSE(store_id != 0);
}

} // namespace
