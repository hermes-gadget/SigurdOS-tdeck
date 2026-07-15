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

#include "diagnostics/debug.h"
#include "diagnostics/debug_cfg.h"

namespace {

TEST(DebugTest, LevelBoundsAreDocumented) {
    EXPECT_EQ(sigurdos::debug::SIGURDOS_DEBUG_MIN_LEVEL, 1);
    EXPECT_EQ(sigurdos::debug::SIGURDOS_DEBUG_MAX_LEVEL, 3);
}

TEST(DebugTest, ClampLevelKeepsValidLevels) {
    EXPECT_EQ(sigurdos::debug::clamp_level(1), 1);
    EXPECT_EQ(sigurdos::debug::clamp_level(2), 2);
    EXPECT_EQ(sigurdos::debug::clamp_level(3), 3);
}

TEST(DebugTest, ClampLevelRejectsBelowMinimum) {
    EXPECT_EQ(sigurdos::debug::clamp_level(-1), 1);
    EXPECT_EQ(sigurdos::debug::clamp_level(0), 1);
}

TEST(DebugTest, ClampLevelRejectsAboveMaximum) {
    EXPECT_EQ(sigurdos::debug::clamp_level(4), 3);
    EXPECT_EQ(sigurdos::debug::clamp_level(255), 3);
}

TEST(DebugTest, NonDebugBuildStubsRemainDisabled) {
    EXPECT_EQ(sigurdos::debug::get_level(), 0);
    EXPECT_EQ(sigurdos::debug::feat_to_mask(), 0);
    EXPECT_FALSE(sigurdos::debug::feat_get_display());
    EXPECT_FALSE(sigurdos::debug::feat_get_mesh());
    EXPECT_FALSE(sigurdos::debug::feat_get_ui());
    EXPECT_FALSE(sigurdos::debug::feat_get_map());
    EXPECT_FALSE(sigurdos::debug::feat_get_diag());
}

TEST(DebugTest, SerialDebugCommandsDisabledByDefault) {
    EXPECT_EQ(SIGURDOS_SERIAL_DEBUG_COMMANDS, 0);
    EXPECT_EQ(SIGURDOS_REMOTE_TEST_ACTIVE, 0);
    EXPECT_EQ(SIGURDOS_SERIAL_DEBUG_COMMANDS_ACTIVE, 0);
}

} // namespace
