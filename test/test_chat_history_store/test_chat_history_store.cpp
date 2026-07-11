// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "hal/atomic_file.h"
#include "ui/chat_history_store.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using sigurdos::ui::PersistedChatMessage;

static constexpr const char* PATH = "/tmp/sigurdos_chat_history_test.bin";
static constexpr const char* TEMP_PATH = "/tmp/sigurdos_chat_history_test.bin.tmp";

struct Channel {
    std::string name;
    std::vector<PersistedChatMessage> messages;
};

struct LoadedMessage {
    std::string channel;
    PersistedChatMessage message;
};

PersistedChatMessage message(const char* sender, const char* text,
                             uint32_t timestamp, bool is_self)
{
    PersistedChatMessage result{};
    std::strncpy(result.sender, sender, sizeof(result.sender) - 1);
    std::strncpy(result.text, text, sizeof(result.text) - 1);
    result.timestamp = timestamp;
    result.is_self = is_self;
    return result;
}

bool readChannel(int index, char* name_out, size_t name_len,
                 uint8_t* count_out, void* raw)
{
    auto* channels = static_cast<std::vector<Channel>*>(raw);
    if (!channels || index < 0 || index >= (int)channels->size() ||
        !name_out || name_len == 0 || !count_out) return false;
    std::strncpy(name_out, (*channels)[index].name.c_str(), name_len - 1);
    name_out[name_len - 1] = '\0';
    *count_out = (uint8_t)(*channels)[index].messages.size();
    return true;
}

bool readMessage(int channel, int index, PersistedChatMessage* out, void* raw)
{
    auto* channels = static_cast<std::vector<Channel>*>(raw);
    if (!channels || !out || channel < 0 || channel >= (int)channels->size() ||
        index < 0 || index >= (int)(*channels)[channel].messages.size()) return false;
    *out = (*channels)[channel].messages[index];
    return true;
}

bool collectMessage(const char* channel, const PersistedChatMessage& item, void* raw)
{
    auto* loaded = static_cast<std::vector<LoadedMessage>*>(raw);
    loaded->push_back({channel, item});
    return true;
}

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

void writeFile(const char* path, const std::vector<uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              (std::streamsize)bytes.size());
}

class ChatHistoryStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        sigurdos::storage::atomicFileSetNativeFault(
            sigurdos::storage::AtomicFileNativeFault::None);
        sigurdos::ui::chatHistorySetNativePath(PATH);
        std::remove(PATH);
        std::remove(TEMP_PATH);
    }

    void TearDown() override
    {
        sigurdos::storage::atomicFileSetNativeFault(
            sigurdos::storage::AtomicFileNativeFault::None);
        std::remove(PATH);
        std::remove(TEMP_PATH);
    }

    void save(std::vector<Channel>& channels)
    {
        ASSERT_TRUE(sigurdos::ui::chatHistorySave(
            (int)channels.size(), readChannel, readMessage, &channels));
    }

    std::vector<LoadedMessage> load()
    {
        std::vector<LoadedMessage> loaded;
        const int count = sigurdos::ui::chatHistoryLoad(collectMessage, &loaded);
        EXPECT_EQ(count, (int)loaded.size());
        return loaded;
    }
};

TEST_F(ChatHistoryStoreTest, RoundTripsChannelsAndMessages)
{
    std::vector<Channel> channels{
        {"Public", {message("Alice", "hello", 100, false),
                     message("Me", "hi", 101, true)}},
        {"DM: Bob", {message("Bob", "private", 102, false)}},
    };
    save(channels);

    const auto loaded = load();
    ASSERT_EQ(loaded.size(), 3U);
    EXPECT_EQ(loaded[0].channel, "Public");
    EXPECT_STREQ(loaded[0].message.sender, "Alice");
    EXPECT_STREQ(loaded[0].message.text, "hello");
    EXPECT_FALSE(loaded[0].message.is_self);
    EXPECT_EQ(loaded[1].message.timestamp, 101U);
    EXPECT_TRUE(loaded[1].message.is_self);
    EXPECT_EQ(loaded[2].channel, "DM: Bob");
}

TEST_F(ChatHistoryStoreTest, EmptyHistoryIsAValidReplacement)
{
    std::vector<Channel> channels;
    save(channels);
    EXPECT_TRUE(load().empty());
    EXPECT_EQ(readFile(PATH).size(), 6U);
}

TEST_F(ChatHistoryStoreTest, WriteFailurePreservesLiveHistory)
{
    std::vector<Channel> old_channels{{"Public", {message("A", "old", 1, false)}}};
    std::vector<Channel> new_channels{{"Public", {message("B", "new", 2, false)}}};
    save(old_channels);
    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::Write);
    EXPECT_FALSE(sigurdos::ui::chatHistorySave(
        1, readChannel, readMessage, &new_channels));
    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::None);

    const auto loaded = load();
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_STREQ(loaded[0].message.text, "old");
}

TEST_F(ChatHistoryStoreTest, RenameFailureRecoversValidatedReplacement)
{
    std::vector<Channel> old_channels{{"Public", {message("A", "old", 1, false)}}};
    std::vector<Channel> new_channels{{"Public", {message("B", "new", 2, false)}}};
    save(old_channels);
    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::Rename);
    EXPECT_FALSE(sigurdos::ui::chatHistorySave(
        1, readChannel, readMessage, &new_channels));
    EXPECT_FALSE(readFile(TEMP_PATH).empty());
    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::None);

    const auto loaded = load();
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_STREQ(loaded[0].message.text, "new");
    EXPECT_TRUE(readFile(TEMP_PATH).empty());
}

TEST_F(ChatHistoryStoreTest, InvalidTempNeverReplacesLiveHistory)
{
    std::vector<Channel> channels{{"Public", {message("A", "live", 1, false)}}};
    save(channels);
    writeFile(TEMP_PATH, {0x4c, 0x53, 0x6d, 0x53, 0x01, 0x01});

    const auto loaded = load();
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_STREQ(loaded[0].message.text, "live");
    EXPECT_TRUE(readFile(TEMP_PATH).empty());
}

TEST_F(ChatHistoryStoreTest, ClearRemovesLiveAndPendingFiles)
{
    writeFile(PATH, {1, 2, 3});
    writeFile(TEMP_PATH, {4, 5, 6});
    EXPECT_TRUE(sigurdos::ui::chatHistoryClear());
    EXPECT_TRUE(readFile(PATH).empty());
    EXPECT_TRUE(readFile(TEMP_PATH).empty());
}

} // namespace
