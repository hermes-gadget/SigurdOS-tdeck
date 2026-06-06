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

} // namespace
