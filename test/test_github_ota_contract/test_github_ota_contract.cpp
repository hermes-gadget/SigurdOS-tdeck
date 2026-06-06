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

#include "hal/github_ota.h"

namespace {

using sigurdos::github_ota::GitHubOTAState;
using sigurdos::github_ota::GitHubOTAStatus;

TEST(GitHubOTAContractTest, StateValuesStayStableForUiProgress) {
    EXPECT_EQ(static_cast<int>(GitHubOTAState::Idle), 0);
    EXPECT_EQ(static_cast<int>(GitHubOTAState::Connecting), 1);
    EXPECT_EQ(static_cast<int>(GitHubOTAState::FetchingRelease), 2);
    EXPECT_EQ(static_cast<int>(GitHubOTAState::Downloading), 3);
    EXPECT_EQ(static_cast<int>(GitHubOTAState::Writing), 4);
    EXPECT_EQ(static_cast<int>(GitHubOTAState::Success), 5);
    EXPECT_EQ(static_cast<int>(GitHubOTAState::Failed), 6);
}

TEST(GitHubOTAContractTest, StatusDefaultsToIdleAndEmptyMessages) {
    const GitHubOTAStatus status{};

    EXPECT_EQ(status.state, GitHubOTAState::Idle);
    EXPECT_EQ(status.progress_pct, 0);
    EXPECT_EQ(status.status_msg[0], '\0');
    EXPECT_EQ(status.error_msg[0], '\0');
}

TEST(GitHubOTAContractTest, StatusBuffersKeepUiSafeCapacities) {
    EXPECT_EQ(sizeof(GitHubOTAStatus::status_msg), 80u);
    EXPECT_EQ(sizeof(GitHubOTAStatus::error_msg), 128u);
}

TEST(GitHubOTAContractTest, PublicApiSignaturesStayStable) {
    using bool_fn = bool (*)();
    using void_fn = void (*)();
    using status_fn = const GitHubOTAStatus& (*)();
    using label_fn = const char* (*)();

    (void)static_cast<bool_fn>(sigurdos::github_ota::startGitHubUpdate);
    (void)static_cast<void_fn>(sigurdos::github_ota::loop);
    (void)static_cast<bool_fn>(sigurdos::github_ota::isActive);
    (void)static_cast<status_fn>(sigurdos::github_ota::getStatus);
    (void)static_cast<void_fn>(sigurdos::github_ota::cancel);
    (void)static_cast<label_fn>(sigurdos::github_ota::getDownloadLabel);
    SUCCEED();
}

} // namespace
