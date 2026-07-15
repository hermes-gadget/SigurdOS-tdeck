// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include "mesh/advert_blob.h"
#include <cstring>

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
