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
#include "hal/ota_security_epoch.h"

using sigurdos::ota::otaPinAccepts;
using sigurdos::ota::otaAccessPointInputsValid;
using sigurdos::ota::otaSessionExpired;
using sigurdos::ota::otaUpdateBeginSize;
using sigurdos::ota::OTA_UPDATE_SIZE_UNKNOWN;
using sigurdos::ota::OtaUploadSessionState;
using sigurdos::ota::otaUploadAcceptsChunk;
using sigurdos::ota::otaUploadCanFinish;

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

TEST(OtaAccessPoint, ValidatesSsidAndWpaPasswordLengths) {
    EXPECT_FALSE(otaAccessPointInputsValid(nullptr, "password"));
    EXPECT_FALSE(otaAccessPointInputsValid("", "password"));
    EXPECT_TRUE(otaAccessPointInputsValid("SigurdOS", nullptr));
    EXPECT_TRUE(otaAccessPointInputsValid("SigurdOS", ""));
    EXPECT_FALSE(otaAccessPointInputsValid("SigurdOS", "short"));
    EXPECT_TRUE(otaAccessPointInputsValid("SigurdOS", "12345678"));
    EXPECT_TRUE(otaAccessPointInputsValid(
        "12345678901234567890123456789012", "12345678"));
    EXPECT_FALSE(otaAccessPointInputsValid(
        "123456789012345678901234567890123", "12345678"));
    EXPECT_TRUE(otaAccessPointInputsValid(
        "SigurdOS",
        "123456789012345678901234567890123456789012345678901234567890123"));
    EXPECT_FALSE(otaAccessPointInputsValid(
        "SigurdOS",
        "1234567890123456789012345678901234567890123456789012345678901234"));
}

TEST(OtaAuth, SessionExpiryIsDeadlineAndWrapSafe) {
    EXPECT_FALSE(otaSessionExpired(100, 100 + 599999));
    EXPECT_TRUE(otaSessionExpired(100, 100 + 600000));
    EXPECT_FALSE(otaSessionExpired(0xFFFFFF00U, 0x00000010U));
}

TEST(OtaUpload, UnknownMultipartSizeUsesUpdateSentinel) {
    EXPECT_EQ(otaUpdateBeginSize(0), OTA_UPDATE_SIZE_UNKNOWN);
    EXPECT_EQ(otaUpdateBeginSize(123456), 123456u);
}

TEST(OtaUpload, WritesRequireAuthenticationAndSuccessfulBegin) {
    OtaUploadSessionState state{};
    EXPECT_FALSE(otaUploadAcceptsChunk(state));

    state.authenticated = true;
    EXPECT_FALSE(otaUploadAcceptsChunk(state));

    state.started = true;
    EXPECT_TRUE(otaUploadAcceptsChunk(state));

    state.failed = true;
    EXPECT_FALSE(otaUploadAcceptsChunk(state));
}

TEST(OtaUpload, FinishRequiresNonEmptyMatchingByteCount) {
    OtaUploadSessionState state{true, true, false, false, true, 0};
    EXPECT_FALSE(otaUploadCanFinish(state, 0));

    state.received = 4096;
    EXPECT_FALSE(otaUploadCanFinish(state, 2048));
    EXPECT_FALSE(otaUploadCanFinish(state, 8192));
    EXPECT_TRUE(otaUploadCanFinish(state, 4096));

    state.completed = true;
    EXPECT_FALSE(otaUploadCanFinish(state, 4096));
}

TEST(OtaEpoch, RejectsMalformedAndDowngradeImagesBeforeWriting) {
    uint8_t image[sigurdos::hal::SIGURDOS_OTA_EPOCH_MIN_BYTES]{};
    uint32_t incoming = 99;
    EXPECT_EQ(sigurdos::hal::otaCheckSecurityEpoch(
                  image, sizeof(image), 1, &incoming),
              sigurdos::hal::OtaEpochStatus::Malformed);
    EXPECT_EQ(incoming, 0U);

    image[0] = 0xE9;
    const size_t offset = sigurdos::hal::SIGURDOS_OTA_APP_DESC_OFFSET;
    const uint32_t magic = sigurdos::hal::SIGURDOS_OTA_APP_DESC_MAGIC;
    for (int i = 0; i < 4; ++i) image[offset + i] = uint8_t(magic >> (i * 8));
    image[offset + 4] = 1;

    EXPECT_EQ(sigurdos::hal::otaCheckSecurityEpoch(
                  image, sizeof(image), 2, &incoming),
              sigurdos::hal::OtaEpochStatus::Downgrade);
    EXPECT_EQ(incoming, 1U);
    EXPECT_EQ(sigurdos::hal::otaCheckSecurityEpoch(
                  image, sizeof(image), 1, &incoming),
              sigurdos::hal::OtaEpochStatus::Allowed);
}

TEST(OtaEpoch, AllowsOnlyEqualOrIncreasingMonotonicEpochs) {
    uint8_t image[sigurdos::hal::SIGURDOS_OTA_EPOCH_MIN_BYTES]{};
    image[0] = 0xE9;
    const size_t offset = sigurdos::hal::SIGURDOS_OTA_APP_DESC_OFFSET;
    const uint32_t magic = sigurdos::hal::SIGURDOS_OTA_APP_DESC_MAGIC;
    for (int i = 0; i < 4; ++i) image[offset + i] = uint8_t(magic >> (i * 8));
    image[offset + 4] = 3;
    EXPECT_EQ(sigurdos::hal::otaCheckSecurityEpoch(image, sizeof(image), 2),
              sigurdos::hal::OtaEpochStatus::Allowed);
    EXPECT_EQ(sigurdos::hal::otaCheckSecurityEpoch(image, sizeof(image), 3),
              sigurdos::hal::OtaEpochStatus::Allowed);
    EXPECT_EQ(sigurdos::hal::otaCheckSecurityEpoch(image, sizeof(image), 4),
              sigurdos::hal::OtaEpochStatus::Downgrade);
}
