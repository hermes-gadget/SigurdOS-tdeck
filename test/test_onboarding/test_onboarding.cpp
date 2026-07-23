// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>

#include "ui/onboarding_screen.h"

namespace {

using sigurdos::ui::onboarding_date_valid;
using sigurdos::ui::onboarding_clock_valid;
using sigurdos::ui::onboarding_days_in_month;
using sigurdos::ui::onboarding_is_leap_year;
using sigurdos::ui::onboarding_time_valid;

TEST(OnboardingValidation, LeapYearRulesCoverCenturyCases) {
    EXPECT_TRUE(onboarding_is_leap_year(2024));
    EXPECT_FALSE(onboarding_is_leap_year(2025));
    EXPECT_FALSE(onboarding_is_leap_year(2100));
    EXPECT_TRUE(onboarding_is_leap_year(2400));
}

TEST(OnboardingValidation, DaysInMonthHandlesMonthBounds) {
    EXPECT_EQ(onboarding_days_in_month(2025, 0), 0);
    EXPECT_EQ(onboarding_days_in_month(2025, 1), 31);
    EXPECT_EQ(onboarding_days_in_month(2025, 2), 28);
    EXPECT_EQ(onboarding_days_in_month(2024, 2), 29);
    EXPECT_EQ(onboarding_days_in_month(2025, 4), 30);
    EXPECT_EQ(onboarding_days_in_month(2025, 12), 31);
    EXPECT_EQ(onboarding_days_in_month(2025, 13), 0);
}

TEST(OnboardingValidation, DateAcceptsValidBoundaries) {
    EXPECT_TRUE(onboarding_date_valid(2020, 1, 1));
    EXPECT_TRUE(onboarding_date_valid(2025, 12, 31));
    EXPECT_TRUE(onboarding_date_valid(2024, 2, 29));
}

TEST(OnboardingValidation, DateRejectsInvalidValues) {
    EXPECT_FALSE(onboarding_date_valid(2019, 12, 31));
    EXPECT_FALSE(onboarding_date_valid(2025, 0, 1));
    EXPECT_FALSE(onboarding_date_valid(2025, 13, 1));
    EXPECT_FALSE(onboarding_date_valid(2025, 1, 0));
    EXPECT_FALSE(onboarding_date_valid(2025, 4, 31));
    EXPECT_FALSE(onboarding_date_valid(2025, 2, 29));
}

TEST(OnboardingValidation, TimeAcceptsValidBoundaries) {
    EXPECT_TRUE(onboarding_time_valid(0, 0));
    EXPECT_TRUE(onboarding_time_valid(12, 34));
    EXPECT_TRUE(onboarding_time_valid(23, 59));
}

TEST(OnboardingValidation, TimeRejectsInvalidValues) {
    EXPECT_FALSE(onboarding_time_valid(-1, 0));
    EXPECT_FALSE(onboarding_time_valid(24, 0));
    EXPECT_FALSE(onboarding_time_valid(12, -1));
    EXPECT_FALSE(onboarding_time_valid(12, 60));
}

TEST(OnboardingValidation, ClockValidityStartsAtDocumentedJune2026Epoch) {
    EXPECT_FALSE(onboarding_clock_valid(1780271999U));
    EXPECT_TRUE(onboarding_clock_valid(1780272000U));
}

} // anonymous namespace
