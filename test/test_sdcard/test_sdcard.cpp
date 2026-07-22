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
#include "hal/sdcard_replace.h"
#include "hal/spi_shared.h"
#include "hal/tdeck_pins.h"
#include <cstring>
#include <map>
#include <string>
#include <vector>

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

TEST_F(SDCardTest, SharedBusResetUsesOneBoundedEndBeginLifecycle) {
    SPIClass& spi = sigurdos_shared_spi();
    const int initial_begins = spi.begin_count;
    const int initial_ends = spi.end_count;

    EXPECT_TRUE(sigurdos_shared_spi_reset(40, 38, 41, 39));
    EXPECT_TRUE(sigurdos_shared_spi_reset(40, 38, 41, 39));
    EXPECT_EQ(spi.begin_count, initial_begins + 2);
    EXPECT_EQ(spi.end_count, initial_ends + 1);

    sigurdos_shared_spi_begin(40, 38, 41, 9);
    EXPECT_EQ(spi.begin_count, initial_begins + 2);
    EXPECT_EQ(spi.end_count, initial_ends + 1);
}

using sigurdos::sdcard::detail::RenameResult;
using sigurdos::sdcard::detail::ReplaceOps;
using sigurdos::sdcard::detail::ReplaceStage;

struct FakeSdFs {
    struct Handle {
        std::string path;
        bool open = false;
    } handle;

    std::map<std::string, std::vector<uint8_t>> files;
    bool card_present = true;
    bool fail_open = false;
    bool short_write = false;
    bool fail_sync = false;
    bool fail_size = false;
    bool fail_close = false;
    bool fail_remove = false;
    int fail_rename_call = 0;
    int rename_calls = 0;
    int reset_stage = -1;
    int remove_card_stage = -1;

    static FakeSdFs* self(void* raw) {
        return static_cast<FakeSdFs*>(raw);
    }

    static bool exists(void* raw, const char* path) {
        auto* fs = self(raw);
        return fs->card_present && fs->files.count(path) != 0;
    }

    static bool remove(void* raw, const char* path) {
        auto* fs = self(raw);
        if (!fs->card_present || fs->fail_remove) return false;
        fs->files.erase(path);
        return true;
    }

    static void* open(void* raw, const char* path) {
        auto* fs = self(raw);
        if (!fs->card_present || fs->fail_open) return nullptr;
        fs->files[path].clear();
        fs->handle.path = path;
        fs->handle.open = true;
        return &fs->handle;
    }

    static size_t write(void* raw, void* handle, const uint8_t* data, size_t length) {
        auto* fs = self(raw);
        if (!fs->card_present || handle != &fs->handle || !fs->handle.open) return 0;
        const size_t written = fs->short_write && length > 0 ? length - 1 : length;
        fs->files[fs->handle.path].assign(data, data + written);
        return written;
    }

    static bool sync(void* raw, void* handle) {
        auto* fs = self(raw);
        return fs->card_present && handle == &fs->handle && fs->handle.open &&
            !fs->fail_sync;
    }

    static bool size(void* raw, void* handle, size_t* out) {
        auto* fs = self(raw);
        if (!fs->card_present || fs->fail_size || !out ||
            handle != &fs->handle || !fs->handle.open) {
            return false;
        }
        *out = fs->files[fs->handle.path].size();
        return true;
    }

    static bool close(void* raw, void* handle) {
        auto* fs = self(raw);
        if (handle != &fs->handle) return false;
        fs->handle.open = false;
        return fs->card_present && !fs->fail_close;
    }

    static RenameResult rename(void* raw, const char* from, const char* to) {
        auto* fs = self(raw);
        fs->rename_calls++;
        if (!fs->card_present || fs->fail_rename_call == fs->rename_calls) {
            return RenameResult::Failure;
        }
        auto source = fs->files.find(from);
        if (source == fs->files.end()) return RenameResult::Failure;
        if (fs->files.count(to) != 0) return RenameResult::DestinationExists;
        fs->files[to] = source->second;
        fs->files.erase(source);
        return RenameResult::Success;
    }

    static bool checkpoint(void* raw, ReplaceStage stage) {
        auto* fs = self(raw);
        if (fs->remove_card_stage == static_cast<int>(stage)) {
            fs->card_present = false;
        }
        return fs->reset_stage != static_cast<int>(stage);
    }

    ReplaceOps ops() {
        return {this, exists, remove, open, write, sync, size, close,
                rename, checkpoint};
    }
};

constexpr const char* LIVE_PATH = "/sdcard/config.bin";
constexpr const char* TEMP_PATH = "/sdcard/config.bin.tmp";
constexpr const char* READY_PATH = "/sdcard/config.bin.ready";
const std::vector<uint8_t> OLD_DATA = {'o', 'l', 'd'};
const std::vector<uint8_t> NEW_DATA = {'n', 'e', 'w'};

bool replace_fake(FakeSdFs& fs, const std::vector<uint8_t>& data) {
    return sigurdos::sdcard::detail::replaceFile(
        LIVE_PATH, TEMP_PATH, READY_PATH, data.data(), data.size(), fs.ops());
}

bool recover_fake(FakeSdFs& fs) {
    return sigurdos::sdcard::detail::recoverPending(
        LIVE_PATH, TEMP_PATH, READY_PATH, fs.ops());
}

class SDReplaceTest : public ::testing::Test {
protected:
    FakeSdFs fs;

    void SetUp() override {
        fs.files[LIVE_PATH] = OLD_DATA;
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

TEST_F(SDReplaceTest, SuccessfulReplacementSyncsThenPromotesTemp) {
    EXPECT_TRUE(replace_fake(fs, NEW_DATA));
    EXPECT_EQ(fs.files[LIVE_PATH], NEW_DATA);
    EXPECT_EQ(fs.files.count(TEMP_PATH), 0u);
    EXPECT_EQ(fs.files.count(READY_PATH), 0u);
    EXPECT_EQ(fs.rename_calls, 3);  // validate, FAT conflict, then promotion
}

TEST_F(SDReplaceTest, ZeroLengthReplacementRemainsSupported) {
    const std::vector<uint8_t> empty;
    EXPECT_TRUE(replace_fake(fs, empty));
    EXPECT_TRUE(fs.files[LIVE_PATH].empty());
    EXPECT_EQ(fs.files.count(TEMP_PATH), 0u);
    EXPECT_EQ(fs.files.count(READY_PATH), 0u);
}

TEST_F(SDReplaceTest, OpenFailurePreservesLiveFile) {
    fs.fail_open = true;
    EXPECT_FALSE(replace_fake(fs, NEW_DATA));
    EXPECT_EQ(fs.files[LIVE_PATH], OLD_DATA);
}

TEST_F(SDReplaceTest, ShortWritePreservesLiveFile) {
    fs.short_write = true;
    EXPECT_FALSE(replace_fake(fs, NEW_DATA));
    EXPECT_EQ(fs.files[LIVE_PATH], OLD_DATA);
    EXPECT_EQ(fs.files.count(TEMP_PATH), 0u);
}

TEST_F(SDReplaceTest, SyncOrSizeFailurePreservesLiveFile) {
    fs.fail_sync = true;
    EXPECT_FALSE(replace_fake(fs, NEW_DATA));
    EXPECT_EQ(fs.files[LIVE_PATH], OLD_DATA);

    fs.fail_sync = false;
    fs.fail_size = true;
    EXPECT_FALSE(replace_fake(fs, NEW_DATA));
    EXPECT_EQ(fs.files[LIVE_PATH], OLD_DATA);
}

TEST_F(SDReplaceTest, CloseFailurePreservesLiveFile) {
    fs.fail_close = true;
    EXPECT_FALSE(replace_fake(fs, NEW_DATA));
    EXPECT_EQ(fs.files[LIVE_PATH], OLD_DATA);
    EXPECT_EQ(fs.files.count(TEMP_PATH), 0u);
}

TEST_F(SDReplaceTest, RemoveFailureRetainsLiveAndValidatedTemp) {
    fs.fail_remove = true;
    EXPECT_FALSE(replace_fake(fs, NEW_DATA));
    EXPECT_EQ(fs.files[LIVE_PATH], OLD_DATA);
    EXPECT_EQ(fs.files[READY_PATH], NEW_DATA);

    fs.fail_remove = false;
    EXPECT_TRUE(recover_fake(fs));
    EXPECT_EQ(fs.files[LIVE_PATH], NEW_DATA);
    EXPECT_EQ(fs.files.count(TEMP_PATH), 0u);
    EXPECT_EQ(fs.files.count(READY_PATH), 0u);
}

TEST_F(SDReplaceTest, ValidationRenameFailureKeepsOldLiveFile) {
    fs.fail_rename_call = 1;
    EXPECT_FALSE(replace_fake(fs, NEW_DATA));
    EXPECT_EQ(fs.files[LIVE_PATH], OLD_DATA);
    EXPECT_EQ(fs.files[TEMP_PATH], NEW_DATA);
    EXPECT_EQ(fs.files.count(READY_PATH), 0u);

    fs.fail_rename_call = 0;
    EXPECT_TRUE(recover_fake(fs));
    EXPECT_EQ(fs.files[LIVE_PATH], OLD_DATA);
    EXPECT_EQ(fs.files.count(TEMP_PATH), 0u);
}

TEST_F(SDReplaceTest, RenameFailureAfterRemoveLeavesRecoverableTemp) {
    fs.fail_rename_call = 3;
    EXPECT_FALSE(replace_fake(fs, NEW_DATA));
    EXPECT_EQ(fs.files.count(LIVE_PATH), 0u);
    EXPECT_EQ(fs.files[READY_PATH], NEW_DATA);

    fs.fail_rename_call = 0;
    EXPECT_TRUE(recover_fake(fs));
    EXPECT_EQ(fs.files[LIVE_PATH], NEW_DATA);
    EXPECT_EQ(fs.files.count(TEMP_PATH), 0u);
    EXPECT_EQ(fs.files.count(READY_PATH), 0u);
}

TEST_F(SDReplaceTest, CardRemovalDuringWritePreservesRecoverableState) {
    fs.remove_card_stage = static_cast<int>(ReplaceStage::TempWritten);
    EXPECT_FALSE(replace_fake(fs, NEW_DATA));

    fs.card_present = true;
    fs.remove_card_stage = -1;
    EXPECT_TRUE(recover_fake(fs));
    EXPECT_EQ(fs.files[LIVE_PATH], OLD_DATA);
    EXPECT_EQ(fs.files.count(TEMP_PATH), 0u);
    EXPECT_EQ(fs.files.count(READY_PATH), 0u);
}

TEST_F(SDReplaceTest, PartialTempForNewFileIsNeverPromoted) {
    fs.files.erase(LIVE_PATH);
    fs.reset_stage = static_cast<int>(ReplaceStage::TempWritten);

    EXPECT_FALSE(replace_fake(fs, NEW_DATA));
    fs.reset_stage = -1;
    EXPECT_TRUE(recover_fake(fs));

    EXPECT_EQ(fs.files.count(LIVE_PATH), 0u);
    EXPECT_EQ(fs.files.count(TEMP_PATH), 0u);
    EXPECT_EQ(fs.files.count(READY_PATH), 0u);
}

class SDReplaceResetTest : public SDReplaceTest,
                           public ::testing::WithParamInterface<ReplaceStage> {};

TEST_P(SDReplaceResetTest, ResetAtEachStageLeavesValidLiveOrRecoverableTemp) {
    const ReplaceStage stage = GetParam();
    fs.reset_stage = static_cast<int>(stage);

    EXPECT_FALSE(replace_fake(fs, NEW_DATA));
    fs.reset_stage = -1;
    EXPECT_TRUE(recover_fake(fs));

    const bool promotion_started = stage == ReplaceStage::TempValidated ||
        stage == ReplaceStage::LiveRemoved ||
        stage == ReplaceStage::TempRenamed;
    EXPECT_EQ(fs.files[LIVE_PATH], promotion_started ? NEW_DATA : OLD_DATA);
    EXPECT_EQ(fs.files.count(TEMP_PATH), 0u);
    EXPECT_EQ(fs.files.count(READY_PATH), 0u);
}

INSTANTIATE_TEST_SUITE_P(
    PersistenceBoundaries,
    SDReplaceResetTest,
    ::testing::Values(
        ReplaceStage::TempOpened,
        ReplaceStage::TempWritten,
        ReplaceStage::TempSynced,
        ReplaceStage::TempClosed,
        ReplaceStage::TempValidated,
        ReplaceStage::LiveRemoved,
        ReplaceStage::TempRenamed));

} // anonymous namespace
