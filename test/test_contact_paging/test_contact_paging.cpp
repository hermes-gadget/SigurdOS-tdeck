// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>

#include "ui/contact_paging.h"

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

} // namespace
