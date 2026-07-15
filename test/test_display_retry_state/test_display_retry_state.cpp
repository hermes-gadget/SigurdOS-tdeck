// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
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

#include <gtest/gtest.h>

#include "hal/display_retry_state.h"

namespace {

using sigurdos::hal::DISPLAY_RETRY_MAGIC;
using sigurdos::hal::DISPLAY_RETRY_PENDING;
using sigurdos::hal::DISPLAY_RETRY_VERSION;
using sigurdos::hal::DisplayRetryState;
using sigurdos::hal::MAX_DISPLAY_FAILURES;
using sigurdos::hal::display_retry_begin;
using sigurdos::hal::display_retry_clear;
using sigurdos::hal::display_retry_note_failure;
using sigurdos::hal::display_retry_should_halt;
using sigurdos::hal::display_retry_valid;

TEST(DisplayRetryStateTest, ArbitraryRtcBytesStartFresh) {
    DisplayRetryState state{0xFFFFFFFFu, 0xFF, 0xFF, 0xFF, 0xFF};

    EXPECT_FALSE(display_retry_begin(state, false));
    EXPECT_TRUE(display_retry_valid(state));
    EXPECT_EQ(state.magic, DISPLAY_RETRY_MAGIC);
    EXPECT_EQ(state.version, DISPLAY_RETRY_VERSION);
    EXPECT_EQ(state.failures, 0);
    EXPECT_EQ(state.retry_pending, 0);
}

TEST(DisplayRetryStateTest, OnlyDeliberateSoftwareRetryRetainsCounter) {
    DisplayRetryState state{};
    display_retry_clear(state);
    EXPECT_EQ(display_retry_note_failure(state), 1);
    EXPECT_EQ(state.retry_pending, DISPLAY_RETRY_PENDING);

    EXPECT_TRUE(display_retry_begin(state, true));
    EXPECT_EQ(state.failures, 1);
    EXPECT_EQ(state.retry_pending, 0);

    // Once consumed, an unrelated later software reset starts fresh.
    EXPECT_FALSE(display_retry_begin(state, true));
    EXPECT_EQ(state.failures, 0);
}

TEST(DisplayRetryStateTest, PowerAndBrownoutStyleResetsDiscardPendingRetry) {
    DisplayRetryState state{};
    display_retry_clear(state);
    display_retry_note_failure(state);

    EXPECT_FALSE(display_retry_begin(state, false));
    EXPECT_EQ(state.failures, 0);
    EXPECT_EQ(state.retry_pending, 0);
}

TEST(DisplayRetryStateTest, ThreeConsecutiveFailuresEnforceHalt) {
    DisplayRetryState state{};
    display_retry_clear(state);

    for (uint8_t attempt = 1; attempt <= MAX_DISPLAY_FAILURES; ++attempt) {
        EXPECT_EQ(display_retry_note_failure(state), attempt);
        EXPECT_EQ(display_retry_should_halt(state),
                  attempt == MAX_DISPLAY_FAILURES);
        if (attempt < MAX_DISPLAY_FAILURES) {
            EXPECT_TRUE(display_retry_begin(state, true));
        }
    }
}

TEST(DisplayRetryStateTest, CounterSaturatesAtMaximum) {
    DisplayRetryState state{};
    display_retry_clear(state);

    for (int i = 0; i < 10; ++i) display_retry_note_failure(state);

    EXPECT_EQ(state.failures, MAX_DISPLAY_FAILURES);
    EXPECT_TRUE(display_retry_should_halt(state));
}

TEST(DisplayRetryStateTest, SuccessfulDisplayInitClearsSequence) {
    DisplayRetryState state{};
    display_retry_clear(state);
    display_retry_note_failure(state);
    ASSERT_TRUE(display_retry_begin(state, true));

    display_retry_clear(state);

    EXPECT_EQ(state.failures, 0);
    EXPECT_EQ(state.retry_pending, 0);
    EXPECT_FALSE(display_retry_should_halt(state));
}

TEST(DisplayRetryStateTest, RejectsWrongMagicVersionAndOutOfRangeFields) {
    DisplayRetryState state{};
    display_retry_clear(state);

    state.magic ^= 1;
    EXPECT_FALSE(display_retry_valid(state));
    display_retry_clear(state);
    ++state.version;
    EXPECT_FALSE(display_retry_valid(state));
    display_retry_clear(state);
    state.failures = MAX_DISPLAY_FAILURES + 1;
    EXPECT_FALSE(display_retry_valid(state));
    display_retry_clear(state);
    state.retry_pending = 1;
    EXPECT_FALSE(display_retry_valid(state));
    display_retry_clear(state);
    state.reserved = 1;
    EXPECT_FALSE(display_retry_valid(state));
}

}  // namespace
