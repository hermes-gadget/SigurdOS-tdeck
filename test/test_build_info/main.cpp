// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "diagnostics/build_info.cpp"

namespace {

TEST(BuildInfoTest, ProvidesFirmwareVersionAndSafeDefaults) {
    const auto& info = sigurdos::build::info();

    EXPECT_STREQ(info.firmware_version, SIGURDOS_VERSION);
    EXPECT_STREQ(info.git_sha, "unknown");
    EXPECT_FALSE(info.git_dirty);
    EXPECT_STREQ(info.meshcore_sha, "unknown");
    EXPECT_STREQ(info.build_env, "unknown");
    EXPECT_STREQ(info.partitions, "unknown");
    EXPECT_STREQ(info.board, "unknown");
    EXPECT_STREQ(info.mcu, "unknown");
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
