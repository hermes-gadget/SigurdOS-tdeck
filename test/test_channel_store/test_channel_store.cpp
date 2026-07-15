// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "mesh/persistence_store.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct FakeKv {
    std::map<std::string, std::vector<uint8_t>> values;
    int writes_before_failure = -1;

    bool canWrite()
    {
        if (writes_before_failure < 0) return true;
        if (writes_before_failure == 0) return false;
        --writes_before_failure;
        return true;
    }
};

bool has(void* raw, const char* key)
{
    return static_cast<FakeKv*>(raw)->values.count(key) != 0;
}

bool putRaw(FakeKv& fake, const char* key, const void* data, size_t length)
{
    if (!fake.canWrite()) return false;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    fake.values[key] = std::vector<uint8_t>(bytes, bytes + length);
    return true;
}

bool putU8(void* raw, const char* key, uint8_t value)
{
    return putRaw(*static_cast<FakeKv*>(raw), key, &value, sizeof(value));
}

bool putU32(void* raw, const char* key, uint32_t value)
{
    return putRaw(*static_cast<FakeKv*>(raw), key, &value, sizeof(value));
}

bool putString(void* raw, const char* key, const char* value)
{
    return putRaw(*static_cast<FakeKv*>(raw), key, value, std::strlen(value) + 1);
}

bool putBytes(void* raw, const char* key, const uint8_t* data, size_t length)
{
    return putRaw(*static_cast<FakeKv*>(raw), key, data, length);
}

uint8_t getU8(void* raw, const char* key, uint8_t fallback)
{
    FakeKv& fake = *static_cast<FakeKv*>(raw);
    auto it = fake.values.find(key);
    return it != fake.values.end() && it->second.size() == 1
        ? it->second[0] : fallback;
}

uint32_t getU32(void* raw, const char* key, uint32_t fallback)
{
    FakeKv& fake = *static_cast<FakeKv*>(raw);
    auto it = fake.values.find(key);
    if (it == fake.values.end() || it->second.size() != sizeof(uint32_t)) return fallback;
    uint32_t value = 0;
    std::memcpy(&value, it->second.data(), sizeof(value));
    return value;
}

size_t getString(void* raw, const char* key, char* out, size_t length)
{
    FakeKv& fake = *static_cast<FakeKv*>(raw);
    auto it = fake.values.find(key);
    if (it == fake.values.end() || it->second.empty() || !out || length == 0) return 0;
    const size_t copied = std::min(length - 1, it->second.size() - 1);
    std::memcpy(out, it->second.data(), copied);
    out[copied] = '\0';
    return copied;
}

size_t getBytes(void* raw, const char* key, uint8_t* out, size_t length)
{
    FakeKv& fake = *static_cast<FakeKv*>(raw);
    auto it = fake.values.find(key);
    if (it == fake.values.end() || it->second.size() != length) return 0;
    std::memcpy(out, it->second.data(), length);
    return length;
}

sigurdos::mesh::detail::ChannelStoreKv adapter(FakeKv& fake)
{
    return {&fake, has, putU8, putU32, putString, putBytes,
            getU8, getU32, getString, getBytes};
}

struct Channel {
    std::string name;
    uint8_t seed;
};

bool readChannel(int index, char* name_out, size_t name_len,
                 uint8_t* secret_out, size_t secret_len,
                 uint8_t* hash_out, size_t hash_len, void* raw)
{
    auto* channels = static_cast<std::vector<Channel>*>(raw);
    if (!channels || index < 0 || index >= (int)channels->size() ||
        name_len < 2 || secret_len != 32 || hash_len != 32) return false;
    std::strncpy(name_out, (*channels)[index].name.c_str(), name_len - 1);
    name_out[name_len - 1] = '\0';
    for (size_t i = 0; i < 32; ++i) {
        secret_out[i] = (uint8_t)((*channels)[index].seed + i);
        hash_out[i] = (uint8_t)((*channels)[index].seed + 0x40 + i);
    }
    return true;
}

struct LoadedChannel {
    std::string name;
    uint8_t secret_first;
    uint8_t hash_first;
};

bool collectChannel(const uint8_t* secret, size_t secret_len,
                    const uint8_t* hash, const char* name, void* raw)
{
    if (secret_len != 32 || !raw) return false;
    static_cast<std::vector<LoadedChannel>*>(raw)->push_back(
        {name, secret[0], hash[0]});
    return true;
}

std::vector<LoadedChannel> load(FakeKv& fake)
{
    auto kv = adapter(fake);
    std::vector<LoadedChannel> loaded;
    const int count = sigurdos::mesh::detail::channelStoreLoadTransactional(
        kv, collectChannel, &loaded);
    EXPECT_EQ(count, (int)loaded.size());
    return loaded;
}

void save(FakeKv& fake, std::vector<Channel>& channels)
{
    auto kv = adapter(fake);
    ASSERT_TRUE(sigurdos::mesh::detail::channelStoreSaveTransactional(
        kv, (int)channels.size(), readChannel, &channels));
}

TEST(ChannelStoreTransaction, RoundTripsCommittedBank)
{
    FakeKv fake;
    std::vector<Channel> channels{{"Public", 0x10}, {"#test", 0x30}};
    save(fake, channels);

    ASSERT_EQ(getU8(&fake, "ch_active", 0xFF), 0);
    const auto loaded = load(fake);
    ASSERT_EQ(loaded.size(), 2U);
    EXPECT_EQ(loaded[0].name, "Public");
    EXPECT_EQ(loaded[0].secret_first, 0x10);
    EXPECT_EQ(loaded[1].name, "#test");
    EXPECT_EQ(loaded[1].hash_first, 0x70);
}

TEST(ChannelStoreTransaction, EveryInterruptedWriteKeepsPreviousBankActive)
{
    std::vector<Channel> old_channels{{"Old", 0x10}};
    std::vector<Channel> new_channels{{"New", 0x20}};
    FakeKv committed;
    save(committed, old_channels);

    // Invalidate marker, three channel fields, count, CRC, commit, active.
    for (int successful_writes = 0; successful_writes < 8; ++successful_writes) {
        FakeKv interrupted = committed;
        interrupted.writes_before_failure = successful_writes;
        auto kv = adapter(interrupted);
        EXPECT_FALSE(sigurdos::mesh::detail::channelStoreSaveTransactional(
            kv, 1, readChannel, &new_channels));
        const auto loaded = load(interrupted);
        ASSERT_EQ(loaded.size(), 1U);
        EXPECT_EQ(loaded[0].name, "Old");
    }
}

TEST(ChannelStoreTransaction, CorruptActiveBankFallsBackToPreviousCommit)
{
    FakeKv fake;
    std::vector<Channel> old_channels{{"Old", 0x10}};
    std::vector<Channel> new_channels{{"New", 0x20}};
    save(fake, old_channels); // bank 0
    save(fake, new_channels); // bank 1
    fake.values["c1_crc"][0] ^= 0x80;

    const auto loaded = load(fake);
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_EQ(loaded[0].name, "Old");
}

TEST(ChannelStoreTransaction, LegacyKeysRemainReadable)
{
    FakeKv fake;
    putU8(&fake, "ch_cnt", 1);
    putString(&fake, "ch_0_name", "Legacy");
    uint8_t secret[32];
    uint8_t hash[32];
    for (size_t i = 0; i < 32; ++i) {
        secret[i] = (uint8_t)i;
        hash[i] = (uint8_t)(0x80 + i);
    }
    putBytes(&fake, "ch_0_sec", secret, sizeof(secret));
    putBytes(&fake, "ch_0_hash", hash, sizeof(hash));

    const auto loaded = load(fake);
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_EQ(loaded[0].name, "Legacy");
    EXPECT_EQ(loaded[0].hash_first, 0x80);
}

TEST(ChannelStoreTransaction, ValidationCompletesBeforeAnyLoadCallback)
{
    FakeKv fake;
    std::vector<Channel> channels{{"One", 0x10}, {"Two", 0x20}};
    save(fake, channels);
    fake.values["c0_01_s"].resize(4);

    const auto loaded = load(fake);
    EXPECT_TRUE(loaded.empty());
}

TEST(ChannelStoreTransaction, RejectsOutOfRangeCount)
{
    FakeKv fake;
    std::vector<Channel> channels;
    auto kv = adapter(fake);
    EXPECT_FALSE(sigurdos::mesh::detail::channelStoreSaveTransactional(
        kv, 17, readChannel, &channels));
    EXPECT_FALSE(has(&fake, "ch_active"));
}

} // namespace
