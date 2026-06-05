// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.


/**
 * Unit tests for Regions companion flood-scope feature.
 *
 * Tests: struct layout, API signatures, SPIFFS binary format,
 *        golden-vector key derivation, and constant values.
 *
 * Note: deriveRegionKey() uses mbedtls (ESP32-specific) and cannot be
 * linked in native tests. The golden vector is verified via hardcoded
 * expected output from sha256sum('#test').
 */
#include <gtest/gtest.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "mesh/regions.h"
#include "mesh/mesh_wrapper.h"

namespace {

// Golden vector: SHA256("#test")[0..15]
// Verified via: echo -n '#test' | sha256sum
// Full SHA256: 9cd8fcf22a47333b591d96a2b848b73f...
// First 16 bytes (hex): 9c d8 fc f2 2a 47 33 3b 59 1d 96 a2 b8 48 b7 3f
static const uint8_t GOLDEN_DERIVE_KEY_TEST[16] = {
    0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b,
    0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f
};

static void removeAllRegionEntriesNamed(const char* name) {
    for (int i = 0; i <= SIGURD_MAX_REGIONS; i++) {
        if (!sigurdos::mesh::removeRegion(name)) return;
    }
}

// ── Constants ───────────────────────────────────────────

TEST(RegionsTest, MaxRegionsIs8) {
    EXPECT_EQ(SIGURD_MAX_REGIONS, 8);
}

// ── Struct layout ───────────────────────────────────────

TEST(RegionsTest, SigurdRegionSize) {
    // name[31] + key[16] = 47 bytes; no padding expected (packed byte arrays)
    EXPECT_EQ(sizeof(sigurdos::mesh::SigurdRegion), 47);
}

TEST(RegionsTest, SigurdRegionFieldOffsets) {
    auto offset_of_name = offsetof(sigurdos::mesh::SigurdRegion, name);
    auto offset_of_key  = offsetof(sigurdos::mesh::SigurdRegion, key);

    EXPECT_EQ(offset_of_name, 0u);
    EXPECT_EQ(offset_of_key, 31u);
}

TEST(RegionsTest, SigurdRegionNameFitsTerminator) {
    // name[31] can store a 30-char name + null terminator
    sigurdos::mesh::SigurdRegion r;
    memset(&r, 0, sizeof(r));

    const char* name30 = "#abcdefghijklmnopqrstuvwxyz123"; // 30 chars
    EXPECT_EQ(strlen(name30), 30u);

    strncpy(r.name, name30, sizeof(r.name) - 1);
    r.name[sizeof(r.name) - 1] = '\0';

    EXPECT_STREQ(r.name, name30);
    EXPECT_EQ(r.name[30], '\0');
}

TEST(RegionsTest, SigurdRegionKeySize) {
    // key must be exactly 16 bytes
    EXPECT_EQ(sizeof(sigurdos::mesh::SigurdRegion::key), 16u);
}

TEST(RegionsTest, NormalizeRegionNameAutoPrefixesBarePublicNames) {
    char out[sizeof(sigurdos::mesh::SigurdRegion::name)] = {};

    ASSERT_TRUE(sigurdos::mesh::detail::normalizeRegionName("ops", out, sizeof(out)));
    EXPECT_STREQ(out, "#ops");

    ASSERT_TRUE(sigurdos::mesh::detail::normalizeRegionName("#ops", out, sizeof(out)));
    EXPECT_STREQ(out, "#ops");

    ASSERT_TRUE(sigurdos::mesh::detail::normalizeRegionName("$crew", out, sizeof(out)));
    EXPECT_STREQ(out, "$crew");
}

TEST(RegionsTest, NormalizeRegionNameRejectsNamesThatWouldTruncate) {
    char out[sizeof(sigurdos::mesh::SigurdRegion::name)] = {};

    const char* bare29 = "abcdefghijklmnopqrstuvwxyz123";
    ASSERT_EQ(strlen(bare29), 29u);
    ASSERT_TRUE(sigurdos::mesh::detail::normalizeRegionName(bare29, out, sizeof(out)));
    EXPECT_STREQ(out, "#abcdefghijklmnopqrstuvwxyz123");

    const char* bare30 = "abcdefghijklmnopqrstuvwxyz1234";
    ASSERT_EQ(strlen(bare30), 30u);
    EXPECT_FALSE(sigurdos::mesh::detail::normalizeRegionName(bare30, out, sizeof(out)));
    EXPECT_EQ(out[0], '\0');

    const char* prefixed31 = "#abcdefghijklmnopqrstuvwxyz1234";
    ASSERT_EQ(strlen(prefixed31), 31u);
    EXPECT_FALSE(sigurdos::mesh::detail::normalizeRegionName(prefixed31, out, sizeof(out)));
    EXPECT_EQ(out[0], '\0');
}

TEST(RegionsTest, RegionListContainsNameMatchesExactNormalizedName) {
    sigurdos::mesh::SigurdRegion list[2];
    memset(&list[0], 0, sizeof(list[0]));
    memset(&list[1], 0, sizeof(list[1]));
    strncpy(list[0].name, "#ops", sizeof(list[0].name) - 1);
    strncpy(list[1].name, "$crew", sizeof(list[1].name) - 1);

    EXPECT_TRUE(sigurdos::mesh::detail::regionListContainsName(list, 2, "#ops"));
    EXPECT_TRUE(sigurdos::mesh::detail::regionListContainsName(list, 2, "$crew"));
    EXPECT_FALSE(sigurdos::mesh::detail::regionListContainsName(list, 2, "ops"));
    EXPECT_FALSE(sigurdos::mesh::detail::regionListContainsName(list, 1, "$crew"));
}

// ── SPIFFS binary format (manual construction) ─────────

TEST(RegionsTest, SpiffsHeaderSize) {
    // regions.dat format: 4-byte count (uint32_t) + count * sizeof(SigurdRegion)
    // Minimum file size with 0 regions: 4 bytes (count=0)
    EXPECT_GE(sizeof(uint32_t), 4u);  // count field is 4 bytes
}

TEST(RegionsTest, SpiffsEmptyFileSize) {
    // Empty regions file: 4 bytes (count=0)
    size_t empty_file_sz = 4; // uint32_t count = 0
    EXPECT_EQ(empty_file_sz, 4u);
}

TEST(RegionsTest, SpiffsFileSizeWithOneRegion) {
    // 4 (count) + 1 * 47 (SigurdRegion) = 51 bytes
    size_t one_region_sz = 4 + sizeof(sigurdos::mesh::SigurdRegion);
    EXPECT_EQ(one_region_sz, 51u);
}

TEST(RegionsTest, SpiffsFileSizeWithEightRegions) {
    // 4 (count) + 8 * 47 = 380 bytes
    size_t full_sz = 4 + 8 * sizeof(sigurdos::mesh::SigurdRegion);
    EXPECT_EQ(full_sz, 380u);
}

TEST(RegionsTest, RegionFileLoadCountRejectsShortHeader) {
    EXPECT_EQ(sigurdos::mesh::detail::regionFileLoadCount(1, 3, 1), 0);
}

TEST(RegionsTest, RegionFileLoadCountRejectsZeroStoredCount) {
    EXPECT_EQ(sigurdos::mesh::detail::regionFileLoadCount(
                  0, sigurdos::mesh::detail::REGION_FILE_HEADER_SIZE, 1),
              0);
}

TEST(RegionsTest, RegionFileLoadCountRejectsTruncatedPayload) {
    const size_t one_region_file =
        sigurdos::mesh::detail::REGION_FILE_HEADER_SIZE +
        sizeof(sigurdos::mesh::SigurdRegion);

    EXPECT_EQ(sigurdos::mesh::detail::regionFileLoadCount(2, one_region_file, 2), 0);
}

TEST(RegionsTest, RegionFileLoadCountAllowsCallerLimitedCompleteFile) {
    const size_t four_region_file =
        sigurdos::mesh::detail::REGION_FILE_HEADER_SIZE +
        4 * sizeof(sigurdos::mesh::SigurdRegion);

    EXPECT_EQ(sigurdos::mesh::detail::regionFileLoadCount(4, four_region_file, 2), 2);
}

TEST(RegionsTest, RegionFileLoadCountRejectsStoredCountAboveProjectLimit) {
    const size_t oversized_file =
        sigurdos::mesh::detail::REGION_FILE_HEADER_SIZE +
        (SIGURD_MAX_REGIONS + 1) * sizeof(sigurdos::mesh::SigurdRegion);

    EXPECT_EQ(sigurdos::mesh::detail::regionFileLoadCount(
                  SIGURD_MAX_REGIONS + 1, oversized_file, SIGURD_MAX_REGIONS),
              0);
}

// ── Key derivation golden vector ───────────────────────

TEST(RegionsTest, DeriveKeyGoldenVectorDocumented) {
    // This test does NOT call deriveRegionKey() (requires mbedtls).
    // It documents the expected output so hardware tests can validate.
    // SHA256("#test") = 9cd8fcf22a47333b591d96a2b848b73f... (32 bytes)
    // First 16 bytes = golden vector above.

    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[0],  0x9c);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[1],  0xd8);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[2],  0xfc);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[3],  0xf2);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[4],  0x2a);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[5],  0x47);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[6],  0x33);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[7],  0x3b);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[8],  0x59);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[9],  0x1d);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[10], 0x96);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[11], 0xa2);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[12], 0xb8);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[13], 0x48);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[14], 0xb7);
    EXPECT_EQ(GOLDEN_DERIVE_KEY_TEST[15], 0x3f);
}

// ── API function signatures (compile-time checks) ───────

TEST(RegionsTest, ListRegionsSignature) {
    using fn_t = int (*)(sigurdos::mesh::SigurdRegion*, int);
    (void)static_cast<fn_t>(sigurdos::mesh::listRegions);
    SUCCEED();
}

TEST(RegionsTest, AddRegionSignature) {
    using fn_t = bool (*)(const char*, const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::addRegion);
    SUCCEED();
}

TEST(RegionsTest, RemoveRegionSignature) {
    using fn_t = bool (*)(const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::removeRegion);
    SUCCEED();
}

TEST(RegionsTest, SetActiveRegionSignature) {
    using fn_t = bool (*)(const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::setActiveRegion);
    SUCCEED();
}

TEST(RegionsTest, GetActiveRegionSignature) {
    using fn_t = const char* (*)();
    (void)static_cast<fn_t>(sigurdos::mesh::getActiveRegion);
    SUCCEED();
}

TEST(RegionsTest, SetSendUnscopedOnceSignature) {
    using fn_t = void (*)(bool);
    (void)static_cast<fn_t>(sigurdos::mesh::setSendUnscopedOnce);
    SUCCEED();
}

TEST(RegionsTest, SyncRegionsFromChannelsSignature) {
    using fn_t = void (*)();
    (void)static_cast<fn_t>(sigurdos::mesh::syncRegionsFromChannels);
    SUCCEED();
}

// regions.h API signatures
TEST(RegionsTest, LoadRegionsSignature) {
    using fn_t = int (*)(sigurdos::mesh::SigurdRegion*, int);
    (void)static_cast<fn_t>(sigurdos::mesh::loadRegions);
    SUCCEED();
}

TEST(RegionsTest, SaveRegionsSignature) {
    using fn_t = bool (*)(const sigurdos::mesh::SigurdRegion*, int);
    (void)static_cast<fn_t>(sigurdos::mesh::saveRegions);
    SUCCEED();
}

TEST(RegionsTest, DeriveRegionKeySignature) {
    using fn_t = bool (*)(const char*, uint8_t*);
    (void)static_cast<fn_t>(sigurdos::mesh::deriveRegionKey);
    SUCCEED();
}

// ── Binary format round-trip (manual buffer) ────────────

TEST(RegionsTest, ManualBinaryRoundTrip) {
    // Manually construct a regions.dat buffer with 2 regions,
    // parse it back, and verify fields match.
    sigurdos::mesh::SigurdRegion regions[2];
    memset(&regions[0], 0, sizeof(regions[0]));
    memset(&regions[1], 0, sizeof(regions[1]));

    strncpy(regions[0].name, "#london", sizeof(regions[0].name) - 1);
    memcpy(regions[0].key, GOLDEN_DERIVE_KEY_TEST, 16);

    strncpy(regions[1].name, "$crew", sizeof(regions[1].name) - 1);
    memset(regions[1].key, 0xAB, 16);

    // Serialize to buffer (simulating SPIFFS file format)
    uint8_t buf[4 + 2 * sizeof(sigurdos::mesh::SigurdRegion)];
    uint32_t count = 2;
    memcpy(buf, &count, 4);
    memcpy(buf + 4, &regions[0], sizeof(sigurdos::mesh::SigurdRegion));
    memcpy(buf + 4 + sizeof(sigurdos::mesh::SigurdRegion), &regions[1],
           sizeof(sigurdos::mesh::SigurdRegion));

    // Deserialize and verify
    uint32_t loaded_count;
    memcpy(&loaded_count, buf, 4);
    EXPECT_EQ(loaded_count, 2u);

    sigurdos::mesh::SigurdRegion loaded[2];
    memcpy(&loaded[0], buf + 4, sizeof(sigurdos::mesh::SigurdRegion));
    memcpy(&loaded[1], buf + 4 + sizeof(sigurdos::mesh::SigurdRegion),
           sizeof(sigurdos::mesh::SigurdRegion));

    EXPECT_STREQ(loaded[0].name, "#london");
    EXPECT_EQ(memcmp(loaded[0].key, GOLDEN_DERIVE_KEY_TEST, 16), 0);
    EXPECT_STREQ(loaded[1].name, "$crew");
    EXPECT_EQ(loaded[1].key[0], 0xAB);
    EXPECT_EQ(loaded[1].key[15], 0xAB);
}

// ── Edge cases ──────────────────────────────────────────

TEST(RegionsTest, EmptyRegionStructIsAllZeros) {
    sigurdos::mesh::SigurdRegion r;
    memset(&r, 0, sizeof(r));

    EXPECT_EQ(r.name[0], '\0');
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(r.key[i], 0u);
    }
}

TEST(RegionsTest, NullTerminatorPreservedInName) {
    // verify that name is always null-terminated after strncpy
    sigurdos::mesh::SigurdRegion r;
    memset(&r, 0xFF, sizeof(r));  // fill with garbage

    const char* short_name = "#nyc";
    strncpy(r.name, short_name, sizeof(r.name) - 1);
    r.name[sizeof(r.name) - 1] = '\0';

    EXPECT_STREQ(r.name, "#nyc");
    EXPECT_EQ(r.name[30], '\0');
}

TEST(RegionsTest, AddRegionRejectsDuplicateNormalizedNames) {
    const char* normalized = "#codexdup";
    removeAllRegionEntriesNamed(normalized);

    ASSERT_TRUE(sigurdos::mesh::addRegion("codexdup", nullptr));
    EXPECT_FALSE(sigurdos::mesh::addRegion("#codexdup", nullptr));

    sigurdos::mesh::SigurdRegion list[SIGURD_MAX_REGIONS];
    int n = sigurdos::mesh::listRegions(list, SIGURD_MAX_REGIONS);
    int matches = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].name, normalized) == 0) matches++;
    }
    EXPECT_EQ(matches, 1);

    removeAllRegionEntriesNamed(normalized);
}

TEST(RegionsTest, AddRegionRejectsNamesThatWouldTruncate) {
    const char* bare30 = "abcdefghijklmnopqrstuvwxyz1234";
    const char* prefixed31 = "#abcdefghijklmnopqrstuvwxyz1234";

    EXPECT_FALSE(sigurdos::mesh::addRegion(bare30, nullptr));
    EXPECT_FALSE(sigurdos::mesh::addRegion(prefixed31, nullptr));
}

} // namespace
