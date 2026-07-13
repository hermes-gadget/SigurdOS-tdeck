// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>

#include "ui/contact_paging.h"
#include "ui/contact_list_power.h"

namespace {

using sigurdos::ui::CONTACT_LIST_PAGE_SIZE;
using sigurdos::ui::contact_clamp_page;
using sigurdos::ui::contact_page_count;
using sigurdos::ui::contact_page_end;
using sigurdos::ui::contact_page_start;

TEST(ContactPagingTest, MaxContacts350UsesElevenBoundedPages) {
    static_assert(MAX_CONTACTS == 350, "native_test must match firmware contact capacity");

    EXPECT_EQ(CONTACT_LIST_PAGE_SIZE, 32);
    EXPECT_EQ(contact_page_count(MAX_CONTACTS), 11);
    EXPECT_EQ(contact_page_start(0, MAX_CONTACTS), 0);
    EXPECT_EQ(contact_page_end(0, MAX_CONTACTS), 32);
    EXPECT_EQ(contact_page_start(10, MAX_CONTACTS), 320);
    EXPECT_EQ(contact_page_end(10, MAX_CONTACTS), 350);
}

TEST(ContactPagingTest, PageClampRejectsNegativeAndPastEndPages) {
    EXPECT_EQ(contact_clamp_page(-5, 350), 0);
    EXPECT_EQ(contact_clamp_page(0, 350), 0);
    EXPECT_EQ(contact_clamp_page(10, 350), 10);
    EXPECT_EQ(contact_clamp_page(11, 350), 10);
    EXPECT_EQ(contact_clamp_page(100, 350), 10);
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
    EXPECT_EQ(contact_page_count(33), 2);
    EXPECT_EQ(contact_page_start(1, 33), 32);
    EXPECT_EQ(contact_page_end(1, 33), 33);

    EXPECT_EQ(contact_page_count(64), 2);
    EXPECT_EQ(contact_page_start(1, 64), 32);
    EXPECT_EQ(contact_page_end(1, 64), 64);
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

} // namespace
