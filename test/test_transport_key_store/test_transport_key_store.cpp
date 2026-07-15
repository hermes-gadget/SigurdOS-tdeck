// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include <cstring>

#include "helpers/TransportKeyStore.h"

// Compile the pinned MeshCore implementation into this focused native test.
#include "../../lib/meshcore/src/helpers/TransportKeyStore.cpp"

namespace {

TransportKey keyWithByte(uint8_t value) {
    TransportKey key{};
    std::memset(key.key, value, sizeof(key.key));
    return key;
}

TEST(TransportKeyStoreTest, SavesLoadsAndReplacesPrivateRegionKeys) {
    TransportKeyStore store;
    TransportKey first = keyWithByte(0x11);
    TransportKey second = keyWithByte(0x22);
    ASSERT_TRUE(store.saveKeysFor(7, &first, 1));

    TransportKey loaded[2]{};
    ASSERT_EQ(store.loadKeysFor(7, loaded, 2), 1);
    EXPECT_EQ(std::memcmp(loaded[0].key, first.key, sizeof(first.key)), 0);

    ASSERT_TRUE(store.saveKeysFor(7, &second, 1));
    ASSERT_EQ(store.loadKeysFor(7, loaded, 2), 1);
    EXPECT_EQ(std::memcmp(loaded[0].key, second.key, sizeof(second.key)), 0);
}

TEST(TransportKeyStoreTest, PreservesOtherRegionsWhenOneIsReplaced) {
    TransportKeyStore store;
    TransportKey one = keyWithByte(0x31);
    TransportKey two = keyWithByte(0x32);
    TransportKey replacement = keyWithByte(0x41);
    ASSERT_TRUE(store.saveKeysFor(1, &one, 1));
    ASSERT_TRUE(store.saveKeysFor(2, &two, 1));
    ASSERT_TRUE(store.saveKeysFor(1, &replacement, 1));

    TransportKey loaded{};
    ASSERT_EQ(store.loadKeysFor(2, &loaded, 1), 1);
    EXPECT_EQ(std::memcmp(loaded.key, two.key, sizeof(two.key)), 0);
}

TEST(TransportKeyStoreTest, FullStoreFailureLeavesExistingKeysIntact) {
    TransportKeyStore store;
    for (uint16_t id = 1; id <= MAX_TKS_ENTRIES; ++id) {
        TransportKey key = keyWithByte(static_cast<uint8_t>(id));
        ASSERT_TRUE(store.saveKeysFor(id, &key, 1));
    }

    TransportKey overflow = keyWithByte(0xFF);
    EXPECT_FALSE(store.saveKeysFor(MAX_TKS_ENTRIES + 1, &overflow, 1));

    TransportKey loaded{};
    ASSERT_EQ(store.loadKeysFor(1, &loaded, 1), 1);
    EXPECT_EQ(loaded.key[0], 1);
    EXPECT_EQ(store.loadKeysFor(MAX_TKS_ENTRIES + 1, &loaded, 1), 0);
}

TEST(TransportKeyStoreTest, RemoveAndClearDeleteOnlyRequestedKeys) {
    TransportKeyStore store;
    TransportKey one = keyWithByte(0x51);
    TransportKey two = keyWithByte(0x52);
    ASSERT_TRUE(store.saveKeysFor(1, &one, 1));
    ASSERT_TRUE(store.saveKeysFor(2, &two, 1));
    ASSERT_TRUE(store.removeKeys(1));

    TransportKey loaded{};
    EXPECT_EQ(store.loadKeysFor(1, &loaded, 1), 0);
    EXPECT_EQ(store.loadKeysFor(2, &loaded, 1), 1);
    ASSERT_TRUE(store.clear());
    EXPECT_EQ(store.loadKeysFor(2, &loaded, 1), 0);
}

TEST(TransportKeyStoreTest, RejectsInvalidIdentifiersAndArguments) {
    TransportKeyStore store;
    TransportKey key = keyWithByte(0x61);
    EXPECT_FALSE(store.saveKeysFor(0, &key, 1));
    EXPECT_FALSE(store.saveKeysFor(1, nullptr, 1));
    EXPECT_FALSE(store.saveKeysFor(1, &key, MAX_TKS_ENTRIES + 1));
    EXPECT_FALSE(store.removeKeys(0));
}

} // namespace
