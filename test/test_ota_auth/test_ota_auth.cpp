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
//
// Regression coverage for the WiFi OTA upload authentication predicate (#687).
// The OTA flash endpoint must never accept an upload unless a non-zero device
// PIN is configured and the submitted PIN matches it.

#include <gtest/gtest.h>

#include "hal/wifi_ota.h"

using sigurdos::ota::otaPinAccepts;
using sigurdos::ota::otaSessionExpired;

// ── No PIN configured (the factory default) is never authenticated ──────────
TEST(OtaAuth, RejectsWhenNoPinConfigured) {
    // device_pin == 0 means "no PIN set". Any submission must be rejected,
    // including a literal "0", an empty field, or a null pointer.
    EXPECT_FALSE(otaPinAccepts(0, "0"));
    EXPECT_FALSE(otaPinAccepts(0, "1234"));
    EXPECT_FALSE(otaPinAccepts(0, ""));
    EXPECT_FALSE(otaPinAccepts(0, nullptr));
}

// ── A configured PIN requires an exact match ────────────────────────────────
TEST(OtaAuth, AcceptsOnlyMatchingPin) {
    EXPECT_TRUE(otaPinAccepts(1234, "1234"));
    EXPECT_FALSE(otaPinAccepts(1234, "1235"));
    EXPECT_FALSE(otaPinAccepts(1234, "123"));
    EXPECT_FALSE(otaPinAccepts(1234, "12340"));
}

// ── Empty / missing submission against a configured PIN is rejected ─────────
TEST(OtaAuth, RejectsEmptyOrNullSubmission) {
    EXPECT_FALSE(otaPinAccepts(1234, ""));
    EXPECT_FALSE(otaPinAccepts(1234, nullptr));
}

// ── Leading zeros and large PIN values within uint32 range ──────────────────
TEST(OtaAuth, HandlesLargeAndPaddedValues) {
    // Leading-zero submission still parses to the numeric PIN.
    EXPECT_TRUE(otaPinAccepts(42, "0042"));
    // PIN near the top of the uint32 range matches exactly.
    EXPECT_TRUE(otaPinAccepts(4000000000u, "4000000000"));
    EXPECT_FALSE(otaPinAccepts(4000000000u, "4000000001"));
}

TEST(OtaAuth, RejectsSuffixesSignsWhitespaceAndOverflow) {
    EXPECT_FALSE(otaPinAccepts(1234, "1234junk"));
    EXPECT_FALSE(otaPinAccepts(1234, "+1234"));
    EXPECT_FALSE(otaPinAccepts(1234, " 1234"));
    EXPECT_FALSE(otaPinAccepts(1, "4294967297"));
}

TEST(OtaAuth, SessionExpiryIsDeadlineAndWrapSafe) {
    EXPECT_FALSE(otaSessionExpired(100, 100 + 599999));
    EXPECT_TRUE(otaSessionExpired(100, 100 + 600000));
    EXPECT_FALSE(otaSessionExpired(0xFFFFFF00U, 0x00000010U));
}
