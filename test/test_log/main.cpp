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

#include "Arduino.h"
#include "diagnostics/log.h"

TEST(LogMacrosTest, ErrorAndWarningAddLevelPrefixAndNewline) {
    Serial.mock_reset();

    SIG_LOGE("failure %d", 7);
    SIG_LOGW("warning");

    EXPECT_EQ("[E] failure 7\n[W] warning\n", Serial.mock_tx_output());
}

TEST(LogMacrosTest, DebugLogsCompileOutUnlessDebugBuild) {
    Serial.mock_reset();

    SIG_LOGD("debug %s", "message");

#if defined(SIGURDOS_DEBUG)
    EXPECT_EQ("[D] debug message\n", Serial.mock_tx_output());
#else
    EXPECT_TRUE(Serial.mock_tx_output().empty());
#endif
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
