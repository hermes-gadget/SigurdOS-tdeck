// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Native tests for the safe SPIFFS storage init helper.
// These tests validate the API contract, return values, and
// idempotency.  The full hardware path (partition erased
// detection + auto-format) is validated on-device.

#include <gtest/gtest.h>

// Pull in our mocks BEFORE the storage header
#include "Arduino.h"
#include "SPIFFS.h"
#include "esp_partition.h"

#include "hal/storage.h"
#include "validation/gps_validation_storage.h"

namespace {

class StorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
        sigurdos::storage_reset();
        SPIFFS.mock_reset();
        // Default: no SPIFFS partition present (safe default)
        sigurdos::test::mock_spiffs_partition(false, false);
    }
};

// ── Initial state ──────────────────────────────────────

TEST_F(StorageTest, StorageAvailableFalseBeforeInit) {
    EXPECT_FALSE(sigurdos::storage_available());
}

// ── Happy path: SPIFFS mounts successfully ──────────────

TEST_F(StorageTest, InitSucceedsWhenMountSucceeds) {
    SPIFFS.mock_set_mount_result(true);

    EXPECT_TRUE(sigurdos::storage_init());
    EXPECT_TRUE(sigurdos::storage_available());
}

// ── Idempotency ─────────────────────────────────────────

TEST_F(StorageTest, InitIsIdempotent) {
    SPIFFS.mock_set_mount_result(true);

    EXPECT_TRUE(sigurdos::storage_init());
    EXPECT_TRUE(sigurdos::storage_init());  // second call
    EXPECT_TRUE(sigurdos::storage_available());
}

// ── Failure: mount fails, no partition ──────────────────

TEST_F(StorageTest, InitFailsWhenMountFailsAndNoPartition) {
    // mount fails + no SPIFFS partition found
    EXPECT_FALSE(sigurdos::storage_init());
    EXPECT_FALSE(sigurdos::storage_available());
}

// ── Mount failure is sticky (idempotent failure) ─────────

TEST_F(StorageTest, FailureIsSticky) {
    EXPECT_FALSE(sigurdos::storage_init());
    EXPECT_FALSE(sigurdos::storage_init());  // second call
    EXPECT_FALSE(sigurdos::storage_available());
}

// ── Erased partition recovery ────────────────────────

TEST_F(StorageTest, FormatsErasedPartitionOnMountFailure) {
    sigurdos::test::mock_spiffs_partition(true, true);  // present + erased
    SPIFFS.mock_set_mount_result(false);  // first mount fails
    SPIFFS.mock_set_format_result(true);  // format succeeds

    EXPECT_TRUE(sigurdos::storage_init());
    EXPECT_TRUE(SPIFFS.mock_was_formatted());
    EXPECT_TRUE(sigurdos::storage_available());
}

TEST_F(StorageTest, DoesNotFormatNonErasedPartition) {
    sigurdos::test::mock_spiffs_partition(true, false);  // present + not erased
    SPIFFS.mock_set_mount_result(false);

    EXPECT_FALSE(sigurdos::storage_init());
    EXPECT_FALSE(SPIFFS.mock_was_formatted());
    EXPECT_FALSE(sigurdos::storage_available());
}

TEST_F(StorageTest, DoesNotFormatWhenOnlyPrefixIsErased) {
    sigurdos::test::mock_spiffs_partition(true, true);
    sigurdos::test::mock_spiffs_programmed_byte(
        MOCK_SPIFFS_PARTITION_SIZE - 1);
    SPIFFS.mock_set_mount_result(false);

    EXPECT_FALSE(sigurdos::storage_init());
    EXPECT_FALSE(SPIFFS.mock_was_formatted());
    EXPECT_FALSE(sigurdos::storage_available());
}

TEST_F(StorageTest, DoesNotFormatWhenPartitionReadFails) {
    sigurdos::test::mock_spiffs_partition(true, true);
    sigurdos::test::mock_spiffs_read_error(1024);
    SPIFFS.mock_set_mount_result(false);

    EXPECT_FALSE(sigurdos::storage_init());
    EXPECT_FALSE(SPIFFS.mock_was_formatted());
    EXPECT_FALSE(sigurdos::storage_available());
}

TEST_F(StorageTest, ReportsFormatFailure) {
    sigurdos::test::mock_spiffs_partition(true, true);
    SPIFFS.mock_set_mount_result(false);
    SPIFFS.mock_set_format_result(false);

    EXPECT_FALSE(sigurdos::storage_init());
    EXPECT_FALSE(SPIFFS.mock_was_formatted());
    EXPECT_FALSE(sigurdos::storage_available());
}

TEST_F(StorageTest, ReportsRemountFailureAfterFormat) {
    sigurdos::test::mock_spiffs_partition(true, true);
    SPIFFS.mock_set_mount_result(false);
    SPIFFS.mock_set_format_result(true);
    SPIFFS.mock_set_post_format_mount_result(false);

    EXPECT_FALSE(sigurdos::storage_init());
    EXPECT_TRUE(SPIFFS.mock_was_formatted());
    EXPECT_FALSE(sigurdos::storage_available());
}

TEST_F(StorageTest, GpsValidationUsesFailClosedProductionPolicy) {
    sigurdos::test::mock_spiffs_partition(true, true);
    sigurdos::test::mock_spiffs_programmed_byte(8192);
    SPIFFS.mock_set_mount_result(false);

    EXPECT_FALSE(sigurdos::gps_validation::init_storage());
    EXPECT_FALSE(SPIFFS.mock_was_formatted());
    EXPECT_FALSE(sigurdos::storage_available());
}

} // namespace
