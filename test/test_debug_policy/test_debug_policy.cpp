// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "diagnostics/debug_text_policy.h"
#include "diagnostics/reset_policy.h"

TEST(DebugPolicyTest, CrashEvidenceIncludesEveryWatchdogReason) {
    for (int reason = 0; reason <= 10; ++reason) {
        const bool expected = reason == 4 || reason == 5 || reason == 6 ||
                              reason == 7 || reason == 9;
        EXPECT_EQ(sigurdos::diagnostics::reset_retains_crash_evidence(reason),
                  expected);
    }
}

TEST(DebugPolicyTest, TextCaptureIsOptInAndSensitiveTreesStayRedacted) {
    EXPECT_FALSE(sigurdos::diagnostics::debug_text_capture_allowed(false, false));
    EXPECT_FALSE(sigurdos::diagnostics::debug_text_capture_allowed(false, true));
    EXPECT_TRUE(sigurdos::diagnostics::debug_text_capture_allowed(true, false));
    EXPECT_FALSE(sigurdos::diagnostics::debug_text_capture_allowed(true, true));
}
