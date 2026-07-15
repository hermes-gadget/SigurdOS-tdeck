// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include <cstdio>

#include "ui/contact_paging.h"
#include "ui/contact_list_power.h"
#include "ui/finder_contact_policy.h"

namespace {

using sigurdos::ui::CONTACT_LIST_PAGE_SIZE;
using sigurdos::ui::contact_clamp_page;
using sigurdos::ui::contact_page_count;
using sigurdos::ui::contact_page_end;
using sigurdos::ui::contact_page_start;

TEST(ContactPagingTest, MaxContacts350UsesThirtyBoundedPages) {
    static_assert(MAX_CONTACTS == 350, "native_test must match firmware contact capacity");

    EXPECT_EQ(CONTACT_LIST_PAGE_SIZE, 12);
    EXPECT_EQ(contact_page_count(MAX_CONTACTS), 30);
    EXPECT_EQ(contact_page_start(0, MAX_CONTACTS), 0);
    EXPECT_EQ(contact_page_end(0, MAX_CONTACTS), 12);
    EXPECT_EQ(contact_page_start(29, MAX_CONTACTS), 348);
    EXPECT_EQ(contact_page_end(29, MAX_CONTACTS), 350);
}

TEST(ContactPagingTest, PageClampRejectsNegativeAndPastEndPages) {
    EXPECT_EQ(contact_clamp_page(-5, 350), 0);
    EXPECT_EQ(contact_clamp_page(0, 350), 0);
    EXPECT_EQ(contact_clamp_page(29, 350), 29);
    EXPECT_EQ(contact_clamp_page(30, 350), 29);
    EXPECT_EQ(contact_clamp_page(100, 350), 29);
}

TEST(ContactPagingTest, EmptyAndInvalidInputsStaySafe) {
    EXPECT_EQ(contact_page_count(0), 0);
    EXPECT_EQ(contact_page_count(-1), 0);
    EXPECT_EQ(contact_page_count(10, 0), 0);
    EXPECT_EQ(contact_clamp_page(4, 0), 0);
    EXPECT_EQ(contact_page_start(4, 0), 0);
    EXPECT_EQ(contact_page_end(4, 0), 0);
}

TEST(ContactPagingTest, PartialLastPageUsesActualTotal) {
    EXPECT_EQ(contact_page_count(13), 2);
    EXPECT_EQ(contact_page_start(1, 13), 12);
    EXPECT_EQ(contact_page_end(1, 13), 13);

    EXPECT_EQ(contact_page_count(24), 2);
    EXPECT_EQ(contact_page_start(1, 24), 12);
    EXPECT_EQ(contact_page_end(1, 24), 24);
}

TEST(ContactPowerTest, FiltersMatchFieldUseCases) {
    using sigurdos::ui::ContactFilterMode;
    using sigurdos::ui::contact_filter_accepts;
    EXPECT_TRUE(contact_filter_accepts(ContactFilterMode::All, 1, false, false));
    EXPECT_TRUE(contact_filter_accepts(ContactFilterMode::Favourites, 1, true, false));
    EXPECT_FALSE(contact_filter_accepts(ContactFilterMode::Favourites, 1, false, true));
    EXPECT_TRUE(contact_filter_accepts(ContactFilterMode::Infrastructure, 2, false, false));
    EXPECT_TRUE(contact_filter_accepts(ContactFilterMode::Infrastructure, 3, false, false));
    EXPECT_FALSE(contact_filter_accepts(ContactFilterMode::Infrastructure, 1, false, false));
    EXPECT_TRUE(contact_filter_accepts(ContactFilterMode::HasPath, 1, false, true));
}

TEST(ContactPowerTest, ManualContactValidationRequiresNameAndPublicKey) {
    const char* valid = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    EXPECT_TRUE(sigurdos::ui::contact_manual_fields_valid("Alice", valid));
    EXPECT_FALSE(sigurdos::ui::contact_manual_fields_valid("", valid));
    EXPECT_FALSE(sigurdos::ui::contact_manual_fields_valid("Alice", "1234"));
    EXPECT_FALSE(sigurdos::ui::contact_manual_fields_valid(
        "Alice", "0000000000000000000000000000000000000000000000000000000000000000"));
}

TEST(ContactPowerTest, SortModesUseStableTieBreakers) {
    using sigurdos::ui::ContactSortMode;
    using sigurdos::ui::contact_sort_compare;
    EXPECT_LT(contact_sort_compare(ContactSortMode::Alpha,
        "Alice", 3, 1, "Bob", 1, 99), 0);
    EXPECT_LT(contact_sort_compare(ContactSortMode::Recent,
        "Alice", 1, 100, "Bob", 1, 99), 0);
    EXPECT_GT(contact_sort_compare(ContactSortMode::Type,
        "Alice", 3, 100, "Bob", 1, 99), 0);
    EXPECT_LT(contact_sort_compare(ContactSortMode::Recent,
        "Alice", 1, 100, "Bob", 1, 100), 0);
}

TEST(ContactPowerTest, QualityBadgeUsesRssiAndSnr) {
    EXPECT_STREQ(sigurdos::ui::contact_quality_name(-80, 5.0f), "GOOD");
    EXPECT_STREQ(sigurdos::ui::contact_quality_name(-105, -4.0f), "FAIR");
    EXPECT_STREQ(sigurdos::ui::contact_quality_name(-120, -12.0f), "WEAK");
    EXPECT_STREQ(sigurdos::ui::contact_quality_name(0, 0.0f), "?");
}

struct FinderContactCandidate {
    char name[16]{};
    uint8_t type = 0;
    uint32_t last_seen = 0;
};

TEST(FinderContactPolicyTest, ScansBeyondFirst32AcrossMixedNodeTypes) {
    constexpr uint32_t now = 1000;
    FinderContactCandidate contacts[40]{};
    for (int i = 0; i < 40; i++) {
        std::snprintf(contacts[i].name, sizeof(contacts[i].name), "Node%02d", i);
        contacts[i].type = static_cast<uint8_t>((i % 3) + 1);
        contacts[i].last_seen = now - static_cast<uint32_t>(40 - i);
    }

    FinderContactCandidate selected[sigurdos::ui::FINDER_RECENT_CONTACT_LIMIT]{};
    const int count = sigurdos::ui::finder_select_recent_contacts(
        contacts, 40, selected, sigurdos::ui::FINDER_RECENT_CONTACT_LIMIT, now);

    ASSERT_EQ(count, sigurdos::ui::FINDER_RECENT_CONTACT_LIMIT);
    EXPECT_STREQ(selected[0].name, "Node39");
    EXPECT_STREQ(selected[31].name, "Node08");

    bool saw_type[4] = {};
    for (int i = 0; i < count; i++) saw_type[selected[i].type] = true;
    EXPECT_TRUE(saw_type[1]);
    EXPECT_TRUE(saw_type[2]);
    EXPECT_TRUE(saw_type[3]);
}

TEST(FinderContactPolicyTest, Includes120SecondBoundaryAndRejectsOlderContacts) {
    constexpr uint32_t now = 1000;
    FinderContactCandidate contacts[4] = {
        {"Newest", 1, now},
        {"Boundary", 2, now - 120},
        {"TooOld", 3, now - 121},
        {"Unknown", 1, 0},
    };
    FinderContactCandidate selected[4]{};

    const int count = sigurdos::ui::finder_select_recent_contacts(
        contacts, 4, selected, 4, now);

    ASSERT_EQ(count, 2);
    EXPECT_STREQ(selected[0].name, "Newest");
    EXPECT_STREQ(selected[1].name, "Boundary");
    EXPECT_EQ(sigurdos::ui::finder_contact_age(now, selected[1].last_seen), 120u);
}

} // namespace
