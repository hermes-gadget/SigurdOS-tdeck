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
 * Unit tests for RegionMap-based region API.
 *
 * Tests: CRUD operations, RegionInfo listing, active/home/default scope,
 *        region name constraints, flood-allow flag, and API signatures.
 *
 * Note: Tests share global mock state (no fixtures). Each test should
 * be self-contained and not depend on the order of other tests.
 */
#include <gtest/gtest.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "mesh/regions.h"
#include "mesh/mesh_wrapper.h"

namespace {

// Helper: remove every occurrence of a region by name (handles duplicates)
static void removeAllRegionEntriesNamed(const char* name) {
    for (int i = 0; i < 32; i++) {
        if (!sigurdos::mesh::removeRegion(name)) return;
    }
}

// ── Lifecycle ───────────────────────────────────────────

TEST(RegionsTest, InitAndLoad) {
    // Smoke test: init/load/save should not crash
    sigurdos::mesh::regionsLoad();
    sigurdos::mesh::regionsSave();
    SUCCEED();
}

// ── CRUD ────────────────────────────────────────────────

TEST(RegionsTest, AddAndFindRegion) {
    removeAllRegionEntriesNamed("#london");
    auto* e = sigurdos::mesh::addRegion("#london", nullptr);
    ASSERT_NE(e, nullptr);
    EXPECT_STREQ(e->name, "#london");

    auto* found = sigurdos::mesh::findRegion("#london");
    EXPECT_EQ(found, e);
    removeAllRegionEntriesNamed("#london");
}

TEST(RegionsTest, FindRegionReturnsNullForMissing) {
    EXPECT_EQ(sigurdos::mesh::findRegion("#nonexistent9901"), nullptr);
}

TEST(RegionsTest, RemoveRegion) {
    removeAllRegionEntriesNamed("#paris");
    auto* e = sigurdos::mesh::addRegion("#paris", nullptr);
    ASSERT_NE(e, nullptr);

    EXPECT_TRUE(sigurdos::mesh::removeRegion("#paris"));
    EXPECT_EQ(sigurdos::mesh::findRegion("#paris"), nullptr);
}

TEST(RegionsTest, RemoveRegionReturnsFalseForMissing) {
    EXPECT_FALSE(sigurdos::mesh::removeRegion("#_missing_"));
}

TEST(RegionsTest, RemoveRegionReturnsFalseForEmptyName) {
    EXPECT_FALSE(sigurdos::mesh::removeRegion(""));
    EXPECT_FALSE(sigurdos::mesh::removeRegion(nullptr));
}

TEST(RegionsTest, AddRegionRejectsDuplicates) {
    removeAllRegionEntriesNamed("#codexdup");
    ASSERT_NE(sigurdos::mesh::addRegion("#codexdup", nullptr), nullptr);
    EXPECT_EQ(sigurdos::mesh::addRegion("#codexdup", nullptr), nullptr);
    removeAllRegionEntriesNamed("#codexdup");
}

TEST(RegionsTest, AddRegionRejectsNullName) {
    EXPECT_EQ(sigurdos::mesh::addRegion(nullptr, nullptr), nullptr);
}

TEST(RegionsTest, AddRegionRejectsEmptyName) {
    EXPECT_EQ(sigurdos::mesh::addRegion("", nullptr), nullptr);
}

TEST(RegionsTest, FindRegionPrefix) {
    removeAllRegionEntriesNamed("#london");
    removeAllRegionEntriesNamed("#losangeles");
    sigurdos::mesh::addRegion("#london", nullptr);
    sigurdos::mesh::addRegion("#losangeles", nullptr);

    auto* found = sigurdos::mesh::findRegionPrefix("#lo");
    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->name, "#london");

    EXPECT_EQ(sigurdos::mesh::findRegionPrefix("#z"), nullptr);
    removeAllRegionEntriesNamed("#london");
    removeAllRegionEntriesNamed("#losangeles");
}

// ── Region count ────────────────────────────────────────

TEST(RegionsTest, RegionCountChangesOnAddRemove) {
    int before = sigurdos::mesh::getRegionCount();
    sigurdos::mesh::addRegion("#_cnt_test_", nullptr);
    EXPECT_EQ(sigurdos::mesh::getRegionCount(), before + 1);
    sigurdos::mesh::removeRegion("#_cnt_test_");
    EXPECT_EQ(sigurdos::mesh::getRegionCount(), before);
}

// ── RegionInfo listing ──────────────────────────────────

TEST(RegionsTest, ListRegionsReturnsAddedOnes) {
    removeAllRegionEntriesNamed("#alpha");
    removeAllRegionEntriesNamed("#beta");
    sigurdos::mesh::addRegion("#alpha", nullptr);
    sigurdos::mesh::addRegion("#beta", nullptr);

    sigurdos::mesh::RegionInfo list[5];
    int n = sigurdos::mesh::listRegions(list, 5);
    EXPECT_GE(n, 2);
    // Should contain both added names
    bool found_alpha = false, found_beta = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].name, "#alpha") == 0) found_alpha = true;
        if (strcmp(list[i].name, "#beta") == 0) found_beta = true;
    }
    EXPECT_TRUE(found_alpha);
    EXPECT_TRUE(found_beta);
    removeAllRegionEntriesNamed("#alpha");
    removeAllRegionEntriesNamed("#beta");
}

TEST(RegionsTest, ListRegionsRespectsMax) {
    sigurdos::mesh::RegionInfo list[1];
    int n = sigurdos::mesh::listRegions(list, 0);
    EXPECT_EQ(n, 0);
}

TEST(RegionsTest, ListRegionsNullPtrReturnsZero) {
    EXPECT_EQ(sigurdos::mesh::listRegions(nullptr, 5), 0);
}

// ── RegionInfo struct layout ────────────────────────────

TEST(RegionsTest, RegionInfoNameCanHold30CharsPlusNull) {
    // RegionInfo::name is 31 bytes: 30 chars + null terminator
    sigurdos::mesh::RegionInfo info;
    memset(&info, 0, sizeof(info));
    EXPECT_EQ(sizeof(info.name), 31u);

    const char* name29 = "#abcdefghijklmnopqrstuvwxyz12"; // 29 chars + null fits
    ASSERT_EQ(strlen(name29), 29u);
    strncpy(info.name, name29, sizeof(info.name) - 1);
    info.name[sizeof(info.name) - 1] = '\0';
    EXPECT_STREQ(info.name, name29);

    const char* name30 = "#abcdefghijklmnopqrstuvwxyz123"; // 30 chars
    ASSERT_EQ(strlen(name30), 30u);
    strncpy(info.name, name30, sizeof(info.name) - 1);
    info.name[sizeof(info.name) - 1] = '\0';
    EXPECT_STREQ(info.name, name30);
    EXPECT_EQ(info.name[30], '\0');
}

// ── Active region ───────────────────────────────────────

TEST(RegionsTest, ActiveRegionCanBeSetAndGet) {
    sigurdos::mesh::setActiveRegionName("#london");
    EXPECT_STREQ(sigurdos::mesh::getActiveRegion(), "#london");
    sigurdos::mesh::setActiveRegionName("");
    EXPECT_STREQ(sigurdos::mesh::getActiveRegion(), "");
}

// ── Home region ─────────────────────────────────────────

TEST(RegionsTest, HomeRegionSig) {
    EXPECT_EQ(sigurdos::mesh::getHomeRegionName(), nullptr);
    EXPECT_TRUE(sigurdos::mesh::setHomeRegion("#home"));
}

// ── Default scope ───────────────────────────────────────

TEST(RegionsTest, DefaultScopeSig) {
    EXPECT_EQ(sigurdos::mesh::getDefaultScopeName(), nullptr);
    EXPECT_TRUE(sigurdos::mesh::setDefaultScope("#global"));
}

// ── Flood flags ─────────────────────────────────────────

TEST(RegionsTest, RegionAllowsFloodByDefault) {
    EXPECT_TRUE(sigurdos::mesh::regionAllowsFlood("#nonexistent"));
}

TEST(RegionsTest, SetRegionFloodAllowed) {
    EXPECT_TRUE(sigurdos::mesh::setRegionFloodAllowed("#any", false));
}

// ── Region name listing ─────────────────────────────────

TEST(RegionsTest, ListRegionNamesCommaSeparated) {
    removeAllRegionEntriesNamed("#p_alpha");
    removeAllRegionEntriesNamed("#p_beta");
    sigurdos::mesh::addRegion("#p_alpha", nullptr);
    sigurdos::mesh::addRegion("#p_beta", nullptr);

    char buf[64];
    int n = sigurdos::mesh::listRegionNames(buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_NE(strstr(buf, "#p_alpha"), nullptr);
    EXPECT_NE(strstr(buf, "#p_beta"), nullptr);
}

// ── Export ──────────────────────────────────────────────

TEST(RegionsTest, ExportRegionsDoesNotCrash) {
    char buf[128];
    size_t n = sigurdos::mesh::exportRegions(buf, sizeof(buf));
    (void)n;
    SUCCEED();
}

// ── API function signatures (compile-time checks) ───────

TEST(RegionsTest, AddRegionSignature) {
    using fn_t = ::RegionEntry* (*)(const char*, const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::addRegion);
    SUCCEED();
}

TEST(RegionsTest, RemoveRegionSignature) {
    using fn_t = bool (*)(const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::removeRegion);
    SUCCEED();
}

TEST(RegionsTest, FindRegionSignature) {
    using fn_t = ::RegionEntry* (*)(const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::findRegion);
    SUCCEED();
}

TEST(RegionsTest, FindRegionPrefixSignature) {
    using fn_t = ::RegionEntry* (*)(const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::findRegionPrefix);
    SUCCEED();
}

TEST(RegionsTest, ListRegionsSignature) {
    using fn_t = int (*)(sigurdos::mesh::RegionInfo*, int);
    (void)static_cast<fn_t>(sigurdos::mesh::listRegions);
    SUCCEED();
}

TEST(RegionsTest, GetRegionCountSignature) {
    using fn_t = int (*)();
    (void)static_cast<fn_t>(sigurdos::mesh::getRegionCount);
    SUCCEED();
}

TEST(RegionsTest, GetActiveRegionSignature) {
    using fn_t = const char* (*)();
    (void)static_cast<fn_t>(sigurdos::mesh::getActiveRegion);
    SUCCEED();
}

TEST(RegionsTest, SetActiveRegionNameSignature) {
    using fn_t = bool (*)(const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::setActiveRegionName);
    SUCCEED();
}

TEST(RegionsTest, SyncRegionsFromChannelsSignature) {
    using fn_t = void (*)();
    (void)static_cast<fn_t>(sigurdos::mesh::syncRegionsFromChannels);
    SUCCEED();
}

TEST(RegionsTest, RegionsLoadSignature) {
    using fn_t = bool (*)();
    (void)static_cast<fn_t>(sigurdos::mesh::regionsLoad);
    SUCCEED();
}

TEST(RegionsTest, RegionsSaveSignature) {
    using fn_t = bool (*)();
    (void)static_cast<fn_t>(sigurdos::mesh::regionsSave);
    SUCCEED();
}

TEST(RegionsTest, RegionAllowsFloodSignature) {
    using fn_t = bool (*)(const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::regionAllowsFlood);
    SUCCEED();
}

TEST(RegionsTest, SetRegionFloodAllowedSignature) {
    using fn_t = bool (*)(const char*, bool);
    (void)static_cast<fn_t>(sigurdos::mesh::setRegionFloodAllowed);
    SUCCEED();
}

TEST(RegionsTest, GetHomeRegionNameSignature) {
    using fn_t = const char* (*)();
    (void)static_cast<fn_t>(sigurdos::mesh::getHomeRegionName);
    SUCCEED();
}

TEST(RegionsTest, SetHomeRegionSignature) {
    using fn_t = bool (*)(const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::setHomeRegion);
    SUCCEED();
}

TEST(RegionsTest, GetDefaultScopeNameSignature) {
    using fn_t = const char* (*)();
    (void)static_cast<fn_t>(sigurdos::mesh::getDefaultScopeName);
    SUCCEED();
}

TEST(RegionsTest, SetDefaultScopeSignature) {
    using fn_t = bool (*)(const char*);
    (void)static_cast<fn_t>(sigurdos::mesh::setDefaultScope);
    SUCCEED();
}

TEST(RegionsTest, RegionDeniesFloodSignature) {
    using fn_t = ::RegionEntry* (*)(::mesh::Packet*);
    (void)static_cast<fn_t>(sigurdos::mesh::regionDeniesFlood);
    SUCCEED();
}

TEST(RegionsTest, GetRegionMapSignature) {
    using fn_t = RegionMap* (*)();
    (void)static_cast<fn_t>(sigurdos::mesh::getRegionMap);
    SUCCEED();
}

TEST(RegionsTest, ListRegionNamesSignature) {
    using fn_t = int (*)(char*, int, uint8_t, bool);
    (void)static_cast<fn_t>(sigurdos::mesh::listRegionNames);
    SUCCEED();
}

TEST(RegionsTest, ExportRegionsSignature) {
    using fn_t = size_t (*)(char*, size_t);
    (void)static_cast<fn_t>(sigurdos::mesh::exportRegions);
    SUCCEED();
}

} // namespace
