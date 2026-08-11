// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "Arduino.h"
#include "SPIFFS.h"
#include "esp_partition.h"

#include "hal/storage.h"

namespace {

class StorageWarmTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        arduino_mock::reset();
        sigurdos::storage_reset();
        SPIFFS.mock_reset();
        sigurdos::test::mock_spiffs_partition(false, false);
    }
};

TEST_F(StorageWarmTest, CancelBeforeStartIsNoOp)
{
    EXPECT_TRUE(sigurdos::storage_stop_warm());
    EXPECT_EQ(sigurdos::storage_warm_state(), sigurdos::StorageWarmState::Idle);
}

TEST_F(StorageWarmTest, WarmCompletesReady)
{
    SPIFFS.mock_set_mount_result(true);
    ASSERT_TRUE(sigurdos::storage_init());

    sigurdos::storage_warm_after_mount();

    EXPECT_EQ(sigurdos::storage_warm_state(), sigurdos::StorageWarmState::Ready);
}

TEST_F(StorageWarmTest, StartedWarmCanBeCancelledAndJoined)
{
    SPIFFS.mock_set_mount_result(true);
    ASSERT_TRUE(sigurdos::storage_init());

    sigurdos::storage_warm_after_mount();

    EXPECT_TRUE(sigurdos::storage_stop_warm());
    EXPECT_EQ(sigurdos::storage_warm_state(), sigurdos::StorageWarmState::Idle);
    EXPECT_FALSE(sigurdos::storage_available());
}

TEST_F(StorageWarmTest, DoubleCancelIsIdempotent)
{
    EXPECT_TRUE(sigurdos::storage_stop_warm());
    EXPECT_TRUE(sigurdos::storage_stop_warm());
    EXPECT_EQ(sigurdos::storage_warm_state(), sigurdos::StorageWarmState::Idle);
}

} // namespace
