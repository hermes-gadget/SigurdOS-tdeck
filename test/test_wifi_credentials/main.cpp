// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include <cstring>
#include "ui/wifi_credentials_policy.h"

using namespace sigurdos::ui;

TEST(WifiCredentialsPolicy, FailureDoesNotOverwriteKnownGoodCredentials)
{
    WifiCredentialStage staged{};
    ASSERT_TRUE(wifi_credentials_stage(staged, "candidate", "wrong-password"));
    char ssid[33] = "known-good";
    char password[64] = "known-secret";
    EXPECT_FALSE(wifi_credentials_commit(staged, false, ssid, sizeof(ssid),
                                         password, sizeof(password)));
    EXPECT_STREQ(ssid, "known-good");
    EXPECT_STREQ(password, "known-secret");
}

TEST(WifiCredentialsPolicy, ConnectedAttemptCommitsStagedCredentials)
{
    WifiCredentialStage staged{};
    ASSERT_TRUE(wifi_credentials_stage(staged, "candidate", "new-secret"));
    char ssid[33] = "old";
    char password[64] = "old-secret";
    EXPECT_TRUE(wifi_credentials_commit(staged, true, ssid, sizeof(ssid),
                                        password, sizeof(password)));
    EXPECT_STREQ(ssid, "candidate");
    EXPECT_STREQ(password, "new-secret");
}

TEST(WifiCredentialsPolicy, StagingIsBoundedAndRejectsInvalidInputs)
{
    WifiCredentialStage staged{};
    EXPECT_FALSE(wifi_credentials_stage(staged, nullptr, "pw"));
    EXPECT_FALSE(wifi_credentials_stage(staged, "", "pw"));
    EXPECT_FALSE(wifi_credentials_stage(staged, "ssid", nullptr));
    ASSERT_TRUE(wifi_credentials_stage(
        staged, "123456789012345678901234567890123456", 
        "123456789012345678901234567890123456789012345678901234567890123456"));
    EXPECT_EQ(std::strlen(staged.ssid), 32U);
    EXPECT_EQ(std::strlen(staged.password), 63U);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
