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
 * Unit tests for the channel quick-action menu logic.
 *
 * Covers: private-scope menu availability, UI-only action handling, private
 * scope validation, and random private key generation.
 */
#include <gtest/gtest.h>
#include <cstring>

#include "ui/channel_menu.h"

using sigurdos::ui::ChannelAction;
using sigurdos::ui::ChannelMenuItem;
using sigurdos::ui::channel_supports_private_scope;
using sigurdos::ui::channel_menu_build;
using sigurdos::ui::channel_menu_perform;
using sigurdos::ui::private_scope_name_valid;
using sigurdos::ui::private_scope_prepare;

namespace {

struct FakeRandom {
    uint8_t next = 1;
    int calls = 0;
};

bool fill_fake_random(uint8_t* output, size_t length, void* context) {
    auto* state = static_cast<FakeRandom*>(context);
    state->calls++;
    for (size_t i = 0; i < length; ++i) output[i] = state->next++;
    return true;
}

bool fill_zero_random(uint8_t* output, size_t length, void*) {
    std::memset(output, 0, length);
    return true;
}

static bool menu_has(const ChannelMenuItem* items, int n, ChannelAction a) {
    for (int i = 0; i < n; i++) {
        if (items[i].action == a) return true;
    }
    return false;
}

TEST(ChannelMenuTest, PrivateScopeAppliesToRealConversations) {
    EXPECT_TRUE(channel_supports_private_scope("#general"));
    EXPECT_TRUE(channel_supports_private_scope("DM: alice"));
    EXPECT_TRUE(channel_supports_private_scope("general"));
    EXPECT_FALSE(channel_supports_private_scope(""));
    EXPECT_FALSE(channel_supports_private_scope(nullptr));
}

TEST(ChannelMenuTest, ChannelMenuOffersPrivateScopeAndChatActions) {
    ChannelMenuItem items[8];
    int n = channel_menu_build("#general", items, 8);
    EXPECT_TRUE(menu_has(items, n, ChannelAction::ChooseScope));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::MarkRead));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::LeaveChannel));
}

TEST(ChannelMenuTest, PublicChannelCannotBeLeftFromMenu) {
    ChannelMenuItem items[8];
    int n = channel_menu_build("Public", items, 8);
    EXPECT_TRUE(menu_has(items, n, ChannelAction::ChooseScope));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::MarkRead));
    EXPECT_FALSE(menu_has(items, n, ChannelAction::LeaveChannel));
    EXPECT_FALSE(channel_menu_perform(ChannelAction::LeaveChannel, "Public", 0));
}

TEST(ChannelMenuTest, DmAlsoOffersPrivateScope) {
    ChannelMenuItem items[8];
    int n = channel_menu_build("DM: bob", items, 8);
    EXPECT_TRUE(menu_has(items, n, ChannelAction::ChooseScope));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::MarkRead));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::LeaveChannel));
}

TEST(ChannelMenuTest, EmptyConversationOmitsPrivateScope) {
    ChannelMenuItem items[8];
    int n = channel_menu_build("", items, 8);
    EXPECT_FALSE(menu_has(items, n, ChannelAction::ChooseScope));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::MarkRead));
    EXPECT_TRUE(menu_has(items, n, ChannelAction::LeaveChannel));
}

TEST(ChannelMenuTest, EveryItemHasANonEmptyLabel) {
    ChannelMenuItem items[8];
    int n = channel_menu_build("#general", items, 8);
    ASSERT_GT(n, 0);
    for (int i = 0; i < n; i++) {
        ASSERT_NE(items[i].label, nullptr);
        EXPECT_GT(std::strlen(items[i].label), 0u);
    }
}

TEST(ChannelMenuTest, BuildRespectsMaxAndNullGuards) {
    ChannelMenuItem items[2];
    int n = channel_menu_build("#general", items, 2);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(channel_menu_build("#general", nullptr, 8), 0);
    EXPECT_EQ(channel_menu_build("#general", items, 0), 0);
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

TEST(ChannelMenuTest, PrivateScopeNameValidAcceptsBareAndDollarNames) {
    EXPECT_TRUE(private_scope_name_valid("eng-sw"));
    EXPECT_TRUE(private_scope_name_valid("$eng-sw"));
    EXPECT_TRUE(private_scope_name_valid("london"));
    EXPECT_TRUE(private_scope_name_valid("$a"));
    EXPECT_TRUE(private_scope_name_valid(""));
    EXPECT_TRUE(private_scope_name_valid(nullptr));
}

TEST(ChannelMenuTest, PrivateScopeNameValidRejectsPublicOrBadNames) {
    const char* reason = nullptr;
    EXPECT_FALSE(private_scope_name_valid("#eng-sw", &reason));
    ASSERT_NE(reason, nullptr);
    EXPECT_STREQ(reason, "Private scopes only");
    EXPECT_FALSE(private_scope_name_valid("$", &reason));
    EXPECT_FALSE(private_scope_name_valid("eng sw"));
    EXPECT_FALSE(private_scope_name_valid("-lead"));
    EXPECT_FALSE(private_scope_name_valid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

TEST(ChannelMenuTest, PrivateScopePrepareNormalizesBareNameToDollar) {
    char name[31];
    uint8_t key[16];
    FakeRandom random{};
    ASSERT_TRUE(private_scope_prepare("eng-sw", name, sizeof(name), key, nullptr,
                                      fill_fake_random, &random));
    EXPECT_STREQ(name, "$eng-sw");

    uint8_t zero[16]{};
    EXPECT_NE(std::memcmp(key, zero, sizeof(key)), 0);
}

TEST(ChannelMenuTest, PrivateScopePrepareAcceptsDollarPrefixedNameWithIndependentKeys) {
    char bare_name[31];
    uint8_t bare_key[16];
    char dollar_name[31];
    uint8_t dollar_key[16];

    FakeRandom random{};
    ASSERT_TRUE(private_scope_prepare("eng-sw", bare_name, sizeof(bare_name), bare_key,
                                      nullptr, fill_fake_random, &random));
    ASSERT_TRUE(private_scope_prepare("$eng-sw", dollar_name, sizeof(dollar_name), dollar_key,
                                      nullptr, fill_fake_random, &random));
    EXPECT_STREQ(dollar_name, "$eng-sw");
    EXPECT_NE(std::memcmp(bare_key, dollar_key, sizeof(bare_key)), 0);
}

TEST(ChannelMenuTest, PrivateScopePrepareDifferentNamesGetDifferentKeys) {
    char first_name[31];
    uint8_t first_key[16];
    char second_name[31];
    uint8_t second_key[16];

    FakeRandom random{};
    ASSERT_TRUE(private_scope_prepare("eng-sw", first_name, sizeof(first_name), first_key,
                                      nullptr, fill_fake_random, &random));
    ASSERT_TRUE(private_scope_prepare("ops", second_name, sizeof(second_name), second_key,
                                      nullptr, fill_fake_random, &random));
    EXPECT_NE(std::memcmp(first_key, second_key, sizeof(first_key)), 0);
}

TEST(ChannelMenuTest, PrivateScopePrepareEmptyClearsScope) {
    char name[31];
    uint8_t key[16];
    std::memset(name, 0x7f, sizeof(name));
    std::memset(key, 0x7f, sizeof(key));

    EXPECT_TRUE(private_scope_prepare("", name, sizeof(name), key));
    EXPECT_STREQ(name, "");
    uint8_t zero[16]{};
    EXPECT_EQ(std::memcmp(key, zero, sizeof(key)), 0);

    std::memset(name, 0x7f, sizeof(name));
    std::memset(key, 0x7f, sizeof(key));
    EXPECT_TRUE(private_scope_prepare(nullptr, name, sizeof(name), key));
    EXPECT_STREQ(name, "");
    EXPECT_EQ(std::memcmp(key, zero, sizeof(key)), 0);
}

TEST(ChannelMenuTest, PrivateScopePrepareRejectsInvalidNameWithoutChangingKey) {
    char name[31];
    uint8_t key[16];
    std::memset(name, 0x7f, sizeof(name));
    std::memset(key, 0x7f, sizeof(key));

    EXPECT_FALSE(private_scope_prepare("#public", name, sizeof(name), key));
    EXPECT_STREQ(name, "");
    uint8_t zero[16]{};
    EXPECT_EQ(std::memcmp(key, zero, sizeof(key)), 0);
}

TEST(ChannelMenuTest, PrivateScopePrepareRejectsTooSmallOutput) {
    char name[4];
    uint8_t key[16];
    EXPECT_FALSE(private_scope_prepare("eng-sw", name, sizeof(name), key));
}

TEST(ChannelMenuTest, PrivateScopePrepareRejectsMissingOrAllZeroEntropy) {
    char name[31];
    uint8_t key[16];
    const char* reason = nullptr;
    EXPECT_FALSE(private_scope_prepare("eng-sw", name, sizeof(name), key, &reason));
    EXPECT_STREQ(reason, "Secure random unavailable");
    EXPECT_FALSE(private_scope_prepare("eng-sw", name, sizeof(name), key, &reason,
                                       fill_zero_random, nullptr));
    EXPECT_STREQ(reason, "Secure random unavailable");
}

} // namespace
