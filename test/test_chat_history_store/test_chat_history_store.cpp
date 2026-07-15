// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "hal/atomic_file.h"
#include "mesh/message_store.h"
#include "ui/chat_history_store.h"
#include "ui/chat_store_migration.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using sigurdos::ui::LegacyChatHistoryResult;
using sigurdos::ui::LegacyChatMessage;

static constexpr const char* PATH = "/tmp/sigurdos_chat_history_test.bin";
static constexpr const char* TEMP_PATH = "/tmp/sigurdos_chat_history_test.bin.tmp";
static constexpr const char* MESSAGE_PATH = "/tmp/sigurdos_unified_message_test.bin";

struct Channel {
    std::string name;
    std::vector<LegacyChatMessage> messages;
};

struct LoadedMessage {
    std::string channel;
    LegacyChatMessage message;
};

LegacyChatMessage message(const char* sender, const char* text,
                          uint32_t timestamp, bool is_self)
{
    LegacyChatMessage result{};
    std::strncpy(result.sender, sender, sizeof(result.sender) - 1);
    std::strncpy(result.text, text, sizeof(result.text) - 1);
    result.timestamp = timestamp;
    result.is_self = is_self;
    return result;
}

template <typename T>
void appendValue(std::vector<uint8_t>& out, const T& value)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

std::vector<uint8_t> encodeLegacyHistory(const std::vector<Channel>& channels)
{
    std::vector<uint8_t> out;
    const uint32_t magic = 0x536d534c;
    const uint8_t version = 1;
    const uint8_t channel_count = (uint8_t)channels.size();
    appendValue(out, magic);
    appendValue(out, version);
    appendValue(out, channel_count);
    for (const Channel& channel : channels) {
        char name[sigurdos::ui::LEGACY_CHAT_HISTORY_CHANNEL_NAME_LEN]{};
        std::strncpy(name, channel.name.c_str(), sizeof(name) - 1);
        out.insert(out.end(), name, name + sizeof(name));
        const uint8_t count = (uint8_t)channel.messages.size();
        appendValue(out, count);
        for (const LegacyChatMessage& item : channel.messages) {
            out.insert(out.end(), item.sender, item.sender + sizeof(item.sender));
            out.insert(out.end(), item.text, item.text + sizeof(item.text));
            appendValue(out, item.timestamp);
            const uint8_t self = item.is_self ? 1 : 0;
            appendValue(out, self);
        }
    }
    return out;
}

bool collectMessage(const char* channel, const LegacyChatMessage& item, void* raw)
{
    auto* loaded = static_cast<std::vector<LoadedMessage>*>(raw);
    loaded->push_back({channel, item});
    return true;
}

bool appendUnifiedMessage(const char* channel, const LegacyChatMessage& item, void*)
{
    return sigurdos::mesh::messageStoreAppend(
        sigurdos::ui::legacyChatMessageToStored(channel, item));
}

struct FailCtx {
    int calls = 0;
    int fail_on = 0;
};

bool failDuringMigration(const char*, const LegacyChatMessage&, void* raw)
{
    auto* ctx = static_cast<FailCtx*>(raw);
    ++ctx->calls;
    return ctx->calls != ctx->fail_on;
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
        sigurdos::ui::legacyChatHistorySetNativePath(PATH);
        sigurdos::mesh::messageStoreSetNativePath(MESSAGE_PATH);
        std::remove(PATH);
        std::remove(TEMP_PATH);
        std::remove(MESSAGE_PATH);
        std::remove((std::string(MESSAGE_PATH) + ".tmp").c_str());
        ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
        ASSERT_TRUE(sigurdos::mesh::messageStoreClear());
    }

    void TearDown() override
    {
        sigurdos::storage::atomicFileSetNativeFault(
            sigurdos::storage::AtomicFileNativeFault::None);
        std::remove(PATH);
        std::remove(TEMP_PATH);
        std::remove(MESSAGE_PATH);
        std::remove((std::string(MESSAGE_PATH) + ".tmp").c_str());
    }

    void save(const std::vector<Channel>& channels)
    {
        writeFile(PATH, encodeLegacyHistory(channels));
    }
};

TEST_F(ChatHistoryStoreTest, MigratesAllRecordsAndRemovesLegacyFile)
{
    save({
        {"Public", {message("Alice", "hello", 100, false),
                    message("Me", "hi", 101, true)}},
        {"DM: Bob", {message("Bob", "private", 102, false)}},
    });
    std::vector<LoadedMessage> loaded;
    int migrated = 0;
    EXPECT_EQ(sigurdos::ui::legacyChatHistoryMigrate(
                  collectMessage, &loaded, &migrated),
              LegacyChatHistoryResult::Migrated);
    ASSERT_EQ(loaded.size(), 3U);
    EXPECT_EQ(migrated, 3);
    EXPECT_EQ(loaded[0].channel, "Public");
    EXPECT_STREQ(loaded[0].message.text, "hello");
    EXPECT_TRUE(loaded[1].message.is_self);
    EXPECT_EQ(loaded[2].channel, "DM: Bob");
    EXPECT_TRUE(readFile(PATH).empty());
}

TEST_F(ChatHistoryStoreTest, MissingLegacyFileIsNotAnError)
{
    std::vector<LoadedMessage> loaded;
    EXPECT_EQ(sigurdos::ui::legacyChatHistoryMigrate(collectMessage, &loaded),
              LegacyChatHistoryResult::NotFound);
}

TEST_F(ChatHistoryStoreTest, EmptyLegacyHistoryMigratesAndIsRemoved)
{
    save({});
    std::vector<LoadedMessage> loaded;
    EXPECT_EQ(sigurdos::ui::legacyChatHistoryMigrate(collectMessage, &loaded),
              LegacyChatHistoryResult::Migrated);
    EXPECT_TRUE(loaded.empty());
    EXPECT_TRUE(readFile(PATH).empty());
}

TEST_F(ChatHistoryStoreTest, CallbackFailureKeepsLegacyFileForRetry)
{
    save({{"Public", {message("A", "one", 1, false),
                       message("B", "two", 2, false)}}});
    FailCtx failure{0, 2};
    EXPECT_EQ(sigurdos::ui::legacyChatHistoryMigrate(
                  failDuringMigration, &failure),
              LegacyChatHistoryResult::Failed);
    EXPECT_FALSE(readFile(PATH).empty());

    std::vector<LoadedMessage> loaded;
    EXPECT_EQ(sigurdos::ui::legacyChatHistoryMigrate(collectMessage, &loaded),
              LegacyChatHistoryResult::Migrated);
    EXPECT_EQ(loaded.size(), 2U);
}

TEST_F(ChatHistoryStoreTest, ValidPendingSnapshotIsRecoveredBeforeMigration)
{
    save({{"Public", {message("A", "old", 1, false)}}});
    writeFile(TEMP_PATH, encodeLegacyHistory(
        {{"Public", {message("B", "new", 2, false)}}}));

    std::vector<LoadedMessage> loaded;
    EXPECT_EQ(sigurdos::ui::legacyChatHistoryMigrate(collectMessage, &loaded),
              LegacyChatHistoryResult::Migrated);
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_STREQ(loaded[0].message.text, "new");
}

TEST_F(ChatHistoryStoreTest, InvalidPendingSnapshotNeverReplacesLiveHistory)
{
    save({{"Public", {message("A", "live", 1, false)}}});
    writeFile(TEMP_PATH, {0x4c, 0x53, 0x6d, 0x53, 0x01, 0x01});

    std::vector<LoadedMessage> loaded;
    EXPECT_EQ(sigurdos::ui::legacyChatHistoryMigrate(collectMessage, &loaded),
              LegacyChatHistoryResult::Migrated);
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_STREQ(loaded[0].message.text, "live");
}

TEST_F(ChatHistoryStoreTest, InvalidLiveHistoryIsRetainedForDiagnosis)
{
    writeFile(PATH, {1, 2, 3});
    std::vector<LoadedMessage> loaded;
    EXPECT_EQ(sigurdos::ui::legacyChatHistoryMigrate(collectMessage, &loaded),
              LegacyChatHistoryResult::Failed);
    EXPECT_EQ(readFile(PATH), (std::vector<uint8_t>{1, 2, 3}));
}

TEST_F(ChatHistoryStoreTest, MigrationDeduplicatesIntoUnifiedStore)
{
    auto existing = sigurdos::ui::legacyChatMessageToStored(
        "Public", message("Alice", "hello", 100, false));
    for (size_t i = 0; i < sizeof(existing.sender_prefix); ++i) {
        existing.sender_prefix[i] = (uint8_t)(0xA0 + i);
    }
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(existing));
    save({
        {"Public", {message("Alice", "hello", 100, false)}},
        {"DM: Bob", {message("Me", "reply", 101, true)}},
    });

    EXPECT_EQ(sigurdos::ui::legacyChatHistoryMigrate(appendUnifiedMessage),
              LegacyChatHistoryResult::Migrated);
    ASSERT_EQ(sigurdos::mesh::messageStoreCount(), 2);
    sigurdos::mesh::StoredMessage stored[2]{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(stored, 2), 2);
    EXPECT_STREQ(stored[0].text, "Alice: hello");
    EXPECT_TRUE(stored[0].is_channel);
    EXPECT_STREQ(stored[1].conversation, "DM: Bob");
    EXPECT_TRUE(stored[1].is_self);
    EXPECT_TRUE(stored[1].companion_sent);
    EXPECT_EQ(stored[1].path_len, 0xFF);
}

} // namespace
