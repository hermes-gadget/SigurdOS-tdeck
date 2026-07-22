// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "hal/atomic_file.h"
#include "mesh/persistence_store.h"
#include "mocks/unique_temp_dir.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

static const auto PATH = sigurdos::test::processTempDir().file("identity_store.bin");
static const auto TEMP_PATH =
    sigurdos::test::processTempDir().file("identity_store.bin.tmp");

std::vector<uint8_t> bytes(uint8_t seed, size_t length = 64)
{
    std::vector<uint8_t> result(length);
    for (size_t i = 0; i < length; ++i) result[i] = (uint8_t)(seed + i);
    return result;
}

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

void writeFile(const char* path, const std::vector<uint8_t>& data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()),
              (std::streamsize)data.size());
}

class IdentityStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        sigurdos::storage::atomicFileSetNativeFault(
            sigurdos::storage::AtomicFileNativeFault::None);
        sigurdos::mesh::identityStoreSetNativePath(PATH);
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

    std::vector<uint8_t> load(size_t capacity = 128)
    {
        std::vector<uint8_t> out(capacity);
        size_t length = 0;
        if (!sigurdos::mesh::identityStoreLoad(out.data(), out.size(), &length)) {
            return {};
        }
        out.resize(length);
        return out;
    }
};

TEST_F(IdentityStoreTest, ChecksummedEnvelopeRoundTrips)
{
    const auto original = bytes(0x10, 96);
    ASSERT_TRUE(sigurdos::mesh::identityStoreSave(original.data(), original.size()));
    EXPECT_EQ(load(), original);
    EXPECT_GT(readFile(PATH).size(), original.size());
}

TEST_F(IdentityStoreTest, LegacyRawIdentityStillLoads)
{
    const auto legacy = bytes(0x20);
    writeFile(PATH, legacy);
    EXPECT_EQ(load(), legacy);
}

TEST_F(IdentityStoreTest, CorruptEnvelopeIsRejected)
{
    const auto original = bytes(0x30);
    ASSERT_TRUE(sigurdos::mesh::identityStoreSave(original.data(), original.size()));
    auto corrupt = readFile(PATH);
    ASSERT_GT(corrupt.size(), original.size());
    corrupt.back() ^= 0x80;
    writeFile(PATH, corrupt);
    EXPECT_TRUE(load().empty());
}

TEST_F(IdentityStoreTest, FailedReplacementPreservesLiveAndRecoversTemp)
{
    const auto old_identity = bytes(0x40);
    const auto new_identity = bytes(0x70);
    ASSERT_TRUE(sigurdos::mesh::identityStoreSave(
        old_identity.data(), old_identity.size()));

    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::Rename);
    EXPECT_FALSE(sigurdos::mesh::identityStoreSave(
        new_identity.data(), new_identity.size()));
    EXPECT_FALSE(readFile(TEMP_PATH).empty());

    sigurdos::storage::atomicFileSetNativeFault(
        sigurdos::storage::AtomicFileNativeFault::None);
    EXPECT_EQ(load(), new_identity);
    EXPECT_TRUE(readFile(TEMP_PATH).empty());
}

TEST_F(IdentityStoreTest, InvalidTempNeverReplacesLegacyLive)
{
    const auto legacy = bytes(0x55);
    writeFile(PATH, legacy);
    writeFile(TEMP_PATH, bytes(0x99, 17));
    EXPECT_EQ(load(), legacy);
    EXPECT_TRUE(readFile(TEMP_PATH).empty());
}

TEST_F(IdentityStoreTest, ClearRemovesLiveAndPendingTemp)
{
    writeFile(PATH, bytes(0x10));
    writeFile(TEMP_PATH, bytes(0x20));
    EXPECT_TRUE(sigurdos::mesh::identityStoreClear());
    EXPECT_TRUE(readFile(PATH).empty());
    EXPECT_TRUE(readFile(TEMP_PATH).empty());
}

} // namespace
