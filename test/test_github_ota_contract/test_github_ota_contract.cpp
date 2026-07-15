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
#include <cstring>

#include "hal/github_ota.h"
#include "hal/github_ota_plan.h"

namespace {

using sigurdos::github_ota::GitHubOTAState;
using sigurdos::github_ota::GitHubOTAStatus;
using sigurdos::github_ota::githubOtaFlashSessionActive;
using sigurdos::github_ota::branchNeedsReleaseApi;
using sigurdos::github_ota::buildReleaseDownloadUrl;
using sigurdos::github_ota::copyFallbackDownloadUrl;
using sigurdos::github_ota::selectReleaseTagFromJson;

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

TEST(GitHubOTAContractTest, OnlyDownloadAndWriteStatesOwnAFlashSession) {
    EXPECT_FALSE(githubOtaFlashSessionActive(GitHubOTAState::Idle));
    EXPECT_FALSE(githubOtaFlashSessionActive(GitHubOTAState::Connecting));
    EXPECT_FALSE(githubOtaFlashSessionActive(GitHubOTAState::FetchingRelease));
    EXPECT_TRUE(githubOtaFlashSessionActive(GitHubOTAState::Downloading));
    EXPECT_TRUE(githubOtaFlashSessionActive(GitHubOTAState::Writing));
    EXPECT_FALSE(githubOtaFlashSessionActive(GitHubOTAState::Success));
    EXPECT_FALSE(githubOtaFlashSessionActive(GitHubOTAState::Failed));
}

TEST(GitHubOTAPlanTest, LatestAndEmptyBranchUseFallbackWithoutApi) {
    // Without allow_prerelease, empty/unknown/latest branches need no API
    EXPECT_FALSE(branchNeedsReleaseApi(nullptr, false));
    EXPECT_FALSE(branchNeedsReleaseApi("", false));
    EXPECT_FALSE(branchNeedsReleaseApi("latest", false));
    EXPECT_TRUE(branchNeedsReleaseApi("main", false));
    EXPECT_TRUE(branchNeedsReleaseApi("dev", false));
    // With allow_prerelease, "latest" also needs the API
    EXPECT_TRUE(branchNeedsReleaseApi("latest", true));
    // Named branches always need API regardless of prerelease flag
    EXPECT_TRUE(branchNeedsReleaseApi("main", true));
    EXPECT_TRUE(branchNeedsReleaseApi("dev", true));
}

TEST(GitHubOTAPlanTest, BuildsReleaseAndFallbackUrlsWithinBuffer) {
    char url[256] = "";
    buildReleaseDownloadUrl("v2.0.0", url, sizeof(url));
    EXPECT_STREQ(url,
        "https://github.com/hermes-gadget/SigurdOS-tdeck"
        "/releases/download/v2.0.0/firmware.bin");

    char fallback[256] = "";
    copyFallbackDownloadUrl(fallback, sizeof(fallback));
    EXPECT_STREQ(fallback,
        "https://github.com/hermes-gadget/SigurdOS-tdeck"
        "/releases/latest/download/firmware.bin");
}

TEST(GitHubOTAPlanTest, ReleaseUrlIsAlwaysTerminatedWhenBufferIsSmall) {
    char url[18];
    for (char& c : url) c = '?';

    buildReleaseDownloadUrl("v2.0.0", url, sizeof(url));

    EXPECT_EQ(url[sizeof(url) - 1], '\0');
    EXPECT_STRNE(url, "");
}

TEST(GitHubOTAPlanTest, SelectsFirstMatchingNonPrereleaseForBranch) {
    const char* json = R"([
      {"tag_name":"dev-pre","target_commitish":"dev","prerelease":true},
      {"tag_name":"main-stable","target_commitish":"main","prerelease":false},
      {"tag_name":"dev-stable","target_commitish":"dev","prerelease":false}
    ])";
    char tag[32] = "";

    ASSERT_TRUE(selectReleaseTagFromJson(json, "dev", false, tag, sizeof(tag)));
    EXPECT_STREQ(tag, "dev-stable");
}

TEST(GitHubOTAPlanTest, AllowsPrereleaseWhenConfigured) {
    const char* json = R"([
      {"tag_name":"dev-pre","target_commitish":"dev","prerelease":true},
      {"tag_name":"dev-stable","target_commitish":"dev","prerelease":false}
    ])";
    char tag[32] = "";

    ASSERT_TRUE(selectReleaseTagFromJson(json, "dev", true, tag, sizeof(tag)));
    EXPECT_STREQ(tag, "dev-pre");
}

TEST(GitHubOTAPlanTest, SkipsNestedUnknownReleaseFields) {
    const char* json = R"([
      {
        "assets":[{"name":"firmware.bin","size":1234}],
        "body":{"notes":["ignored"]},
        "tag_name":"main-stable",
        "target_commitish":"main",
        "prerelease":false
      }
    ])";
    char tag[32] = "";

    ASSERT_TRUE(selectReleaseTagFromJson(json, "main", false, tag, sizeof(tag)));
    EXPECT_STREQ(tag, "main-stable");
}

TEST(GitHubOTAPlanTest, NoMatchingReleaseIsNegativeCase) {
    const char* json = R"([
      {"tag_name":"dev-only","target_commitish":"dev","prerelease":false}
    ])";
    char tag[32] = "unchanged";

    EXPECT_FALSE(selectReleaseTagFromJson(json, "main", false, tag, sizeof(tag)));
    EXPECT_STREQ(tag, "");
}

TEST(GitHubOTAPlanTest, MalformedJsonIsNegativeCase) {
    char tag[32] = "unchanged";

    EXPECT_FALSE(selectReleaseTagFromJson("[{\"tag_name\":\"bad\"", "main",
                                          false, tag, sizeof(tag)));
    EXPECT_STREQ(tag, "");
}

TEST(GitHubOTAPlanTest, SelectsLatestReleaseWhenBranchIsLatestWithPrerelease) {
    const char* json = R"([
      {"tag_name":"dev-pre","target_commitish":"dev","prerelease":true},
      {"tag_name":"main-stable","target_commitish":"main","prerelease":false}
    ])";
    char tag[32] = "";

    // "latest" with allow_prerelease=true should pick the first (newest) matching release
    ASSERT_TRUE(selectReleaseTagFromJson(json, "latest", true, tag, sizeof(tag)));
    EXPECT_STREQ(tag, "dev-pre");
}

TEST(GitHubOTAPlanTest, SkipsPrereleasesWhenLatestWithoutPrerelease) {
    const char* json = R"([
      {"tag_name":"dev-pre","target_commitish":"dev","prerelease":true},
      {"tag_name":"main-pre","target_commitish":"main","prerelease":true}
    ])";
    char tag[32] = "";

    // "latest" with allow_prerelease=false shouldn't be called through selectReleaseTagFromJson
    // (branchNeedsReleaseApi filter handles it), but verify the function behaves safely
    EXPECT_FALSE(selectReleaseTagFromJson(json, "latest", false, tag, sizeof(tag)));
}

TEST(GitHubOTAPlanTest, InvalidArgumentsAreNegativeCases) {
    char tag[32] = "unchanged";

    EXPECT_FALSE(selectReleaseTagFromJson(nullptr, "main", false, tag, sizeof(tag)));
    EXPECT_STREQ(tag, "");
    std::strncpy(tag, "unchanged", sizeof(tag));
    EXPECT_FALSE(selectReleaseTagFromJson("[]", nullptr, false, tag, sizeof(tag)));
    EXPECT_STREQ(tag, "");
    std::strncpy(tag, "unchanged", sizeof(tag));
    EXPECT_FALSE(selectReleaseTagFromJson("[]", "latest", false, tag, sizeof(tag)));
    EXPECT_STREQ(tag, "");
    EXPECT_FALSE(selectReleaseTagFromJson("[]", "main", false, nullptr, sizeof(tag)));
    EXPECT_FALSE(selectReleaseTagFromJson("[]", "main", false, tag, 0));
}

} // namespace
