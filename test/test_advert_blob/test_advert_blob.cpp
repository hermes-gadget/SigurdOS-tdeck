// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include "mesh/advert_blob.h"
#include <cstring>
#include <string>
#include <vector>

using namespace sigurdos::mesh;

TEST(AdvertBlob, AcceptsOnlyPinnedProtocolLengthRange)
{
    EXPECT_FALSE(advertBlobLengthValid(SIGURDOS_ADVERT_BLOB_MIN_LEN - 1));
    EXPECT_TRUE(advertBlobLengthValid(SIGURDOS_ADVERT_BLOB_MIN_LEN));
    EXPECT_TRUE(advertBlobLengthValid(SIGURDOS_ADVERT_BLOB_MAX_LEN));
    EXPECT_FALSE(advertBlobLengthValid(SIGURDOS_ADVERT_BLOB_MAX_LEN + 1));
}

TEST(AdvertBlob, RequiresStoredLengthToFitDestination)
{
    EXPECT_TRUE(advertBlobFitsOutput(SIGURDOS_ADVERT_BLOB_MAX_LEN,
                                    SIGURDOS_ADVERT_BLOB_MAX_LEN));
    EXPECT_FALSE(advertBlobFitsOutput(SIGURDOS_ADVERT_BLOB_MAX_LEN,
                                     SIGURDOS_ADVERT_BLOB_MAX_LEN - 1));
    EXPECT_FALSE(advertBlobFitsOutput(SIGURDOS_ADVERT_BLOB_MIN_LEN - 1, 4096));
}

TEST(AdvertBlob, UsesEightByteKeyPrefix)
{
    const uint8_t key[32] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    char path[48] = {};
    ASSERT_TRUE(makeAdvertBlobPath(key, sizeof(key), path, sizeof(path)));
    EXPECT_STREQ(path, "/blob_0001020304050607");
}

TEST(AdvertBlob, RetainsLegacyFourBytePathForMigration)
{
    const uint8_t key[32] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    char path[48] = {};
    ASSERT_TRUE(makeAdvertBlobPath(key, sizeof(key), path, sizeof(path), true));
    EXPECT_STREQ(path, "/blob_aabbccdd");
}

TEST(AdvertBlob, RejectsInvalidPathArgumentsAndSmallOutput)
{
    const uint8_t key[8] = {};
    char path[22] = {};
    EXPECT_FALSE(makeAdvertBlobPath(nullptr, 8, path, sizeof(path)));
    EXPECT_FALSE(makeAdvertBlobPath(key, 0, path, sizeof(path)));
    EXPECT_FALSE(makeAdvertBlobPath(key, 8, nullptr, sizeof(path)));
    EXPECT_FALSE(makeAdvertBlobPath(key, 8, path, sizeof(path) - 1));
}

namespace {

struct FakeBlobFileSystem {
    std::vector<std::string> existing;
    std::vector<std::string> removed;
    bool fail_legacy = false;

    bool exists(const char* path) const {
        for (const std::string& item : existing) {
            if (item == path) return true;
        }
        return false;
    }

    bool remove(const char* path) {
        removed.emplace_back(path);
        return !fail_legacy || removed.back() != "/blob_00010203";
    }
};

}  // namespace

TEST(AdvertBlob, DeletesCurrentAndLegacyPathsForKey)
{
    const uint8_t key[32] = {0, 1, 2, 3, 4, 5, 6, 7};
    FakeBlobFileSystem fs{
        {"/blob_0001020304050607", "/blob_00010203"}, {}, false};

    EXPECT_TRUE(deleteBlobByKey(fs, key, sizeof(key)));
    ASSERT_EQ(fs.removed.size(), 2U);
    EXPECT_EQ(fs.removed[0], "/blob_0001020304050607");
    EXPECT_EQ(fs.removed[1], "/blob_00010203");
}

TEST(AdvertBlob, ReportsBlobRemovalFailure)
{
    const uint8_t key[32] = {0, 1, 2, 3, 4, 5, 6, 7};
    FakeBlobFileSystem fs{{"/blob_00010203"}, {}, true};

    EXPECT_FALSE(deleteBlobByKey(fs, key, sizeof(key)));
}
