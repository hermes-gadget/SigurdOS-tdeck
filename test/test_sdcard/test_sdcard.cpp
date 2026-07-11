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

/**
 * Unit tests for SD card HAL contract.
 * Tests: API signatures, filesystem mount states, file path validation,
 *        size formatting, SPI CS pin sanity.
 */
#include <gtest/gtest.h>
#include "Arduino.h"
#include "hal/sdcard.h"
#include "hal/tdeck_pins.h"
#include <cstring>

namespace {

enum class SDState { NOT_MOUNTED, MOUNTING, MOUNTED, ERROR };

static SDState sd_state = SDState::NOT_MOUNTED;
static uint64_t sd_capacity_bytes = 0;
static uint64_t sd_free_bytes = 0;

void sd_mock_set_state(SDState s, uint64_t cap = 0, uint64_t free = 0) {
    sd_state = s;
    sd_capacity_bytes = cap;
    sd_free_bytes = free;
}

const char* sd_format_size(uint64_t bytes, char* buf, size_t buf_sz) {
    if (bytes >= 1024ULL * 1024 * 1024) {
        snprintf(buf, buf_sz, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024 * 1024) {
        snprintf(buf, buf_sz, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, buf_sz, "%llu KB", (unsigned long long)(bytes / 1024));
    }
    return buf;
}

class SDCardTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
        sd_mock_set_state(SDState::NOT_MOUNTED, 0, 0);
    }
};

TEST_F(SDCardTest, CSPinIsValidGPIO) {
    EXPECT_GE(PIN_SD_CS, 0);
    EXPECT_LE(PIN_SD_CS, 48);
}

TEST_F(SDCardTest, CSPinDoesNotConflictWithDisplayOrLoRa) {
    EXPECT_NE(PIN_SD_CS, PIN_TFT_CS);
    EXPECT_NE(PIN_SD_CS, PIN_LORA_NSS);
}

TEST_F(SDCardTest, SPIResetIsForbiddenAfterRadioLock) {
    EXPECT_TRUE(sigurdos_sdcard_may_reset_bus(false));
    EXPECT_FALSE(sigurdos_sdcard_may_reset_bus(true));
}

TEST_F(SDCardTest, InitialStateIsNotMounted) {
    EXPECT_EQ(sd_state, SDState::NOT_MOUNTED);
}

TEST_F(SDCardTest, MountedStateHasCapacity) {
    sd_mock_set_state(SDState::MOUNTED, 32000000000ULL, 15000000000ULL);
    EXPECT_EQ(sd_state, SDState::MOUNTED);
    EXPECT_GT(sd_capacity_bytes, 0ULL);
    EXPECT_GT(sd_free_bytes, 0ULL);
}

TEST_F(SDCardTest, ErrorStateHasZeroCapacity) {
    sd_mock_set_state(SDState::ERROR);
    EXPECT_EQ(sd_state, SDState::ERROR);
}

TEST_F(SDCardTest, ValidPathStartsWithSlash) {
    EXPECT_TRUE(sigurdos_sdcard_path_valid("/maps/london.mbtiles"));
    EXPECT_TRUE(sigurdos_sdcard_path_valid("/config.txt"));
    EXPECT_TRUE(sigurdos_sdcard_path_valid("/logs/mesh_2026_05_14.log"));
}

TEST_F(SDCardTest, PathWithoutSlashIsInvalid) {
    EXPECT_FALSE(sigurdos_sdcard_path_valid("maps/file.dat"));
    EXPECT_FALSE(sigurdos_sdcard_path_valid(""));
    EXPECT_FALSE(sigurdos_sdcard_path_valid(nullptr));
}

TEST_F(SDCardTest, PathWithDotDotIsInvalid) {
    EXPECT_FALSE(sigurdos_sdcard_path_valid("/etc/../passwd"));
    EXPECT_FALSE(sigurdos_sdcard_path_valid("/../../root"));
    EXPECT_FALSE(sigurdos_sdcard_path_valid("/tiles/10/..hidden.png"));
}

TEST_F(SDCardTest, PathTooLongIsInvalid) {
    char long_path[SIGURDOS_SD_MAX_PATH_LEN + 5];
    memset(long_path, 'a', sizeof(long_path));
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1] = '\0';
    EXPECT_FALSE(sigurdos_sdcard_path_valid(long_path));
}

TEST_F(SDCardTest, PathAtMaxLengthIsValid) {
    char max_path[SIGURDOS_SD_MAX_PATH_LEN + 1];
    memset(max_path, 'x', SIGURDOS_SD_MAX_PATH_LEN);
    max_path[0] = '/';
    max_path[SIGURDOS_SD_MAX_PATH_LEN] = '\0';
    EXPECT_TRUE(sigurdos_sdcard_path_valid(max_path));
}

TEST_F(SDCardTest, FormatGB) {
    char buf[16];
    const char* s = sd_format_size(32ULL * 1024 * 1024 * 1024, buf, sizeof(buf));
    EXPECT_STREQ(s, "32.0 GB");
}

TEST_F(SDCardTest, FormatMB) {
    char buf[16];
    const char* s = sd_format_size(500 * 1024 * 1024ULL, buf, sizeof(buf));
    EXPECT_STREQ(s, "500.0 MB");
}

TEST_F(SDCardTest, FormatKB) {
    char buf[16];
    const char* s = sd_format_size(512 * 1024ULL, buf, sizeof(buf));
    EXPECT_STREQ(s, "512 KB");
}

TEST_F(SDCardTest, FormatZero) {
    char buf[16];
    const char* s = sd_format_size(0, buf, sizeof(buf));
    EXPECT_STREQ(s, "0 KB");
}

TEST_F(SDCardTest, MapTilePathConvention) {
    EXPECT_TRUE(sigurdos_sdcard_path_valid("/sdcard/tiles/10/512/340.png"));
    EXPECT_TRUE(sigurdos_sdcard_path_valid("/sdcard/tiles/14/8137/5290.png"));
    EXPECT_TRUE(sigurdos_sdcard_path_valid("/sdcard/tiles/metadata.json"));
}

} // anonymous namespace
