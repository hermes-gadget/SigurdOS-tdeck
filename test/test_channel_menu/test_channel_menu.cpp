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
 * Unit tests for the Alt+Space channel quick-action menu logic.
 *
 * Covers: which channels expose region actions, the menu item list per
 * channel kind, and the region-sequencing performed by each action
 * (a region must exist before it can be set active / home / default).
 *
 * Note: tests share global mock region state (no fixtures), so each test
 * cleans up the names it creates.
 */
#include <gtest/gtest.h>
#include <cstring>

#include "ui/channel_menu.h"
#include "mesh/mesh_wrapper.h"
#include "mesh/regions.h"

using sigurdos::ui::ChannelAction;
using sigurdos::ui::ChannelMenuItem;
using sigurdos::ui::channel_supports_regions;
using sigurdos::ui::channel_menu_build;
using sigurdos::ui::channel_menu_perform;
using sigurdos::ui::scope_name_valid;
using sigurdos::ui::channel_scope_apply;

namespace {

static void removeAllRegionEntriesNamed(const char* name) {
    for (int i = 0; i < 32; i++) {
        if (!sigurdos::mesh::removeRegion(name)) return;
    }
}

static bool menu_has(const ChannelMenuItem* items, int n, ChannelAction a) {
    for (int i = 0; i < n; i++) {
        if (items[i].action == a) return true;
    }
    return false;
}

// ── channel_supports_regions ────────────────────────────

TEST(ChannelMenuTest, SupportsRegionsOnlyForHashtagChannels) {
    EXPECT_TRUE(channel_supports_regions("#general"));
    EXPECT_TRUE(channel_supports_regions("#a"));
    EXPECT_FALSE(channel_supports_regions("DM: alice"));
    EXPECT_FALSE(channel_supports_regions("general"));
    EXPECT_FALSE(channel_supports_regions(""));
    EXPECT_FALSE(channel_supports_regions(nullptr));
}

// ── channel_menu_build ──────────────────────────────────

TEST(ChannelMenuTest, HashtagChannelOffersRegionAndChannelActions) {
    ChannelMenuItem items[8];
    int n = channel_menu_build("#general", items, 8);
    EXPECT_TRUE(menu_has(items, n, ChannelAction::ChooseScope));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::SetHomeRegion));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::SetDefaultScope));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::MarkRead));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::LeaveChannel));
}

TEST(ChannelMenuTest, DmOffersChannelActionsButNoRegionActions) {
    ChannelMenuItem items[8];
    int n = channel_menu_build("DM: bob", items, 8);
    EXPECT_FALSE(menu_has(items, n, ChannelAction::ChooseScope));
    EXPECT_FALSE(menu_has(items, n, ChannelAction::SetHomeRegion));
    EXPECT_FALSE(menu_has(items, n, ChannelAction::SetDefaultScope));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::MarkRead));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::LeaveChannel));
}

TEST(ChannelMenuTest, EveryItemHasANonEmptyLabel) {
    ChannelMenuItem items[8];
    int n = channel_menu_build("#general", items, 8);
    ASSERT_GT(n, 0);
    for (int i = 0; i < n; i++) {
        ASSERT_NE(items[i].label, nullptr);
        EXPECT_GT(strlen(items[i].label), 0u);
    }
}

TEST(ChannelMenuTest, BuildRespectsMaxAndNullGuards) {
    ChannelMenuItem items[2];
    int n = channel_menu_build("#general", items, 2);
    EXPECT_EQ(n, 2);  // truncated to the buffer
    EXPECT_EQ(channel_menu_build("#general", nullptr, 8), 0);
    EXPECT_EQ(channel_menu_build("#general", items, 0), 0);
}

// ── channel_menu_perform — region sequencing ────────────

TEST(ChannelMenuTest, SetActiveRegionCreatesRegionAndScopes) {
    removeAllRegionEntriesNamed("#scopeme");
    sigurdos::mesh::setActiveRegion("");

    EXPECT_TRUE(channel_menu_perform(ChannelAction::SetActiveRegion, "#scopeme", 0));

    // The region must have been auto-created so the scope actually binds.
    EXPECT_NE(sigurdos::mesh::findRegion("#scopeme"), nullptr);
    EXPECT_STREQ(sigurdos::mesh::getActiveRegion(), "#scopeme");

    removeAllRegionEntriesNamed("#scopeme");
    sigurdos::mesh::setActiveRegion("");
}

TEST(ChannelMenuTest, ClearActiveRegionGoesUnscoped) {
    sigurdos::mesh::setActiveRegion("#whatever");
    EXPECT_TRUE(channel_menu_perform(ChannelAction::ClearActiveRegion, "#general", 0));
    EXPECT_STREQ(sigurdos::mesh::getActiveRegion(), "");
}

TEST(ChannelMenuTest, SetHomeRegionCreatesRegionFirst) {
    removeAllRegionEntriesNamed("#homely");
    EXPECT_TRUE(channel_menu_perform(ChannelAction::SetHomeRegion, "#homely", 0));
    // setHomeRegion would fail on a missing region — proves addRegion ran.
    EXPECT_NE(sigurdos::mesh::findRegion("#homely"), nullptr);
    removeAllRegionEntriesNamed("#homely");
}

TEST(ChannelMenuTest, SetDefaultScopeCreatesRegionFirst) {
    removeAllRegionEntriesNamed("#defscope");
    EXPECT_TRUE(channel_menu_perform(ChannelAction::SetDefaultScope, "#defscope", 0));
    EXPECT_NE(sigurdos::mesh::findRegion("#defscope"), nullptr);
    removeAllRegionEntriesNamed("#defscope");
}

// ── channel_menu_perform — rejections / UI-only ─────────

TEST(ChannelMenuTest, RegionActionsRejectNonHashtagChannels) {
    sigurdos::mesh::setActiveRegion("");
    EXPECT_FALSE(channel_menu_perform(ChannelAction::SetActiveRegion, "DM: alice", 0));
    EXPECT_FALSE(channel_menu_perform(ChannelAction::SetHomeRegion, "DM: alice", 0));
    EXPECT_FALSE(channel_menu_perform(ChannelAction::SetDefaultScope, "general", 0));
    // No scope change leaked through.
    EXPECT_STREQ(sigurdos::mesh::getActiveRegion(), "");
}

TEST(ChannelMenuTest, LeaveChannelNeedsValidIndex) {
    EXPECT_TRUE(channel_menu_perform(ChannelAction::LeaveChannel, "#general", 3));
    EXPECT_FALSE(channel_menu_perform(ChannelAction::LeaveChannel, "#general", -1));
}

TEST(ChannelMenuTest, ChooseScopeMarkReadAndNoneAreCallerHandled) {
    EXPECT_FALSE(channel_menu_perform(ChannelAction::ChooseScope, "#general", 0));
    EXPECT_FALSE(channel_menu_perform(ChannelAction::MarkRead, "#general", 0));
    EXPECT_FALSE(channel_menu_perform(ChannelAction::None, "#general", 0));
}

// ── scope_name_valid ────────────────────────────────────

TEST(ChannelMenuTest, ScopeNameValidAcceptsBareAndHashNames) {
    EXPECT_TRUE(scope_name_valid("eng-sw"));   // bare name — the common case
    EXPECT_TRUE(scope_name_valid("#eng-sw"));  // leading # is optional
    EXPECT_TRUE(scope_name_valid("london"));
    EXPECT_TRUE(scope_name_valid("#a"));
    // Empty/null is the "Public (unscoped)" sentinel.
    EXPECT_TRUE(scope_name_valid(""));
    EXPECT_TRUE(scope_name_valid(nullptr));
}

TEST(ChannelMenuTest, ScopeNameValidRejectsBadNames) {
    const char* reason = nullptr;
    EXPECT_FALSE(scope_name_valid("#", &reason));         // empty body
    EXPECT_NE(reason, nullptr);
    EXPECT_FALSE(scope_name_valid("eng sw"));             // space in body
    EXPECT_FALSE(scope_name_valid("-lead"));              // leading hyphen
    EXPECT_FALSE(scope_name_valid("$ops"));               // '$' is not a name char
    // 30-char body exceeds the 29-char limit (stored as "#" + body in 31B).
    EXPECT_FALSE(scope_name_valid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

// ── channel_scope_apply ─────────────────────────────────

TEST(ChannelMenuTest, ChannelScopeApplyNormalisesBareNameToHash) {
    removeAllRegionEntriesNamed("#eng-sw");
    sigurdos::mesh::setActiveRegion("");

    // User types a bare name; it becomes the "#eng-sw" scope.
    EXPECT_TRUE(channel_scope_apply("eng-sw"));
    EXPECT_NE(sigurdos::mesh::findRegion("#eng-sw"), nullptr);
    EXPECT_STREQ(sigurdos::mesh::getActiveRegion(), "#eng-sw");

    removeAllRegionEntriesNamed("#eng-sw");
    sigurdos::mesh::setActiveRegion("");
}

TEST(ChannelMenuTest, ChannelScopeApplyAcceptsHashPrefixedName) {
    removeAllRegionEntriesNamed("#custom-scope");
    sigurdos::mesh::setActiveRegion("");

    EXPECT_TRUE(channel_scope_apply("#custom-scope"));
    EXPECT_NE(sigurdos::mesh::findRegion("#custom-scope"), nullptr);
    EXPECT_STREQ(sigurdos::mesh::getActiveRegion(), "#custom-scope");

    removeAllRegionEntriesNamed("#custom-scope");
    sigurdos::mesh::setActiveRegion("");
}

TEST(ChannelMenuTest, ChannelScopeApplyEmptyGoesPublic) {
    sigurdos::mesh::setActiveRegion("#something");
    EXPECT_TRUE(channel_scope_apply(""));
    EXPECT_STREQ(sigurdos::mesh::getActiveRegion(), "");
    EXPECT_TRUE(channel_scope_apply(nullptr));
    EXPECT_STREQ(sigurdos::mesh::getActiveRegion(), "");
}

TEST(ChannelMenuTest, ChannelScopeApplyRejectsInvalidName) {
    sigurdos::mesh::setActiveRegion("");
    EXPECT_FALSE(channel_scope_apply("bad name"));  // space
    // A rejected name must not change the active scope.
    EXPECT_STREQ(sigurdos::mesh::getActiveRegion(), "");
}

} // namespace
