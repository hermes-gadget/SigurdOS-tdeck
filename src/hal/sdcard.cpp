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


#include "sdcard.h"
#include "sdcard_replace.h"
#include "tdeck_pins.h"
#include "spi_shared.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

// T-Deck SD card uses SPI on the shared LoRa/display bus (GPIO40/38/41).
// FSPI (SPI2_HOST) is used here; the display also uses SPI2_HOST (via
// LovyanGFX), and the LoRa radio uses SPI2_HOST (via RadioLib). All three
// devices share the same SPI host and bus pins with different CS lines.
// We use the shared singleton SPIClass from spi_shared.h to avoid reinitialising
// SPI2_HOST independently from the LoRa radio.
static SPIClass& sd_spi = sigurdos_shared_spi();

static bool mounted = false;
static bool bus_reset_locked = false;
static uint64_t capacity_bytes = 0;
static uint64_t free_bytes = 0;

static constexpr uint8_t SDCARD_INIT_MAX_ATTEMPTS = 3;
static constexpr uint8_t SDCARD_LAZY_RETRY_MAX_ATTEMPTS = 3;

static int sdcard_retry_count = 0;  // total additional lazy retry attempts

namespace {

using sigurdos::sdcard::detail::RenameResult;
using sigurdos::sdcard::detail::ReplaceOps;

bool sdcard_vfs_path(const char* path, char* out, size_t out_size)
{
    if (!path || !out || out_size == 0) return false;
    constexpr size_t mount_len = sizeof(SIGURDOS_SD_MOUNTPOINT) - 1;
    const bool already_prefixed = std::strncmp(
        path, SIGURDOS_SD_MOUNTPOINT, mount_len) == 0 &&
        (path[mount_len] == '\0' || path[mount_len] == '/');
    const int written = already_prefixed
        ? std::snprintf(out, out_size, "%s", path)
        : std::snprintf(out, out_size, "%s%s", SIGURDOS_SD_MOUNTPOINT, path);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

bool sdcard_temp_path(const char* path, char* out, size_t out_size)
{
    const int written = std::snprintf(out, out_size, "%s.tmp", path);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

bool sdcard_ready_path(const char* path, char* out, size_t out_size)
{
    const int written = std::snprintf(out, out_size, "%s.ready", path);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

bool posix_exists(void*, const char* path)
{
    struct stat info {};
    return ::stat(path, &info) == 0;
}

bool posix_remove(void*, const char* path)
{
    return std::remove(path) == 0 || errno == ENOENT;
}

void* posix_open(void*, const char* path)
{
    return std::fopen(path, "wb");
}

size_t posix_write(void*, void* file, const uint8_t* data, size_t length)
{
    return std::fwrite(data, 1, length, static_cast<FILE*>(file));
}

bool posix_sync(void*, void* file)
{
    FILE* stream = static_cast<FILE*>(file);
    return std::fflush(stream) == 0 && ::fsync(::fileno(stream)) == 0;
}

bool posix_size(void*, void* file, size_t* size)
{
    if (!size) return false;
    struct stat info {};
    if (::fstat(::fileno(static_cast<FILE*>(file)), &info) != 0 || info.st_size < 0) {
        return false;
    }
    *size = static_cast<size_t>(info.st_size);
    return true;
}

bool posix_close(void*, void* file)
{
    return std::fclose(static_cast<FILE*>(file)) == 0;
}

RenameResult posix_rename(void*, const char* from, const char* to)
{
    if (std::rename(from, to) == 0) return RenameResult::Success;
    return errno == EEXIST || errno == ENOTEMPTY
        ? RenameResult::DestinationExists
        : RenameResult::Failure;
}

const ReplaceOps SDCARD_REPLACE_OPS = {
    nullptr,
    posix_exists,
    posix_remove,
    posix_open,
    posix_write,
    posix_sync,
    posix_size,
    posix_close,
    posix_rename,
    nullptr,
};

bool sdcard_file_paths(const char* path, char* live_path, size_t live_size,
                       char* temp_path, size_t temp_size,
                       char* ready_path, size_t ready_size)
{
    return sdcard_vfs_path(path, live_path, live_size) &&
        sdcard_temp_path(live_path, temp_path, temp_size) &&
        sdcard_ready_path(live_path, ready_path, ready_size);
}

bool sdcard_recover_file(const char* path)
{
    char live_path[sizeof(SIGURDOS_SD_MOUNTPOINT) + SIGURDOS_SD_MAX_PATH_LEN + 1];
    char temp_path[sizeof(live_path) + 4];
    char ready_path[sizeof(live_path) + 6];
    return sdcard_file_paths(path, live_path, sizeof(live_path),
                             temp_path, sizeof(temp_path),
                             ready_path, sizeof(ready_path)) &&
        sigurdos::sdcard::detail::recoverPending(
            live_path, temp_path, ready_path, SDCARD_REPLACE_OPS);
}

} // namespace

static SigurdosSdMountDiagnostic sdcard_diag = {
    false,
    0,
    SIGURDOS_SD_MOUNT_SOURCE_NONE,
    SIGURDOS_SD_MOUNT_ERROR_NONE,
    0,
};

static uint32_t sdcard_backoff_ms(uint8_t attempt_index)
{
    if (attempt_index == 0) return 0;
    if (attempt_index == 1) return 120;
    if (attempt_index == 2) return 300;
    return 600;
}

static void sdcard_reset_mount_state()
{
    mounted = false;
    capacity_bytes = 0;
    free_bytes = 0;
}

static void sdcard_reset_diagnostics()
{
    sdcard_diag.mounted = false;
    sdcard_diag.attempt_count = 0;
    sdcard_diag.last_source = SIGURDOS_SD_MOUNT_SOURCE_NONE;
    sdcard_diag.last_error = SIGURDOS_SD_MOUNT_ERROR_NONE;
    sdcard_diag.last_backoff_ms = 0;
}

static void sdcard_record_success()
{
    uint64_t total = (uint64_t)SD.totalBytes();
    uint64_t used = (uint64_t)SD.usedBytes();

    capacity_bytes = total;
    free_bytes = used <= total ? (total - used) : 0;
    mounted = true;
    sdcard_diag.mounted = true;
    sdcard_diag.last_error = SIGURDOS_SD_MOUNT_ERROR_NONE;
}

static bool sdcard_mount_once(SigurdosSdMountSource source)
{
    sdcard_diag.attempt_count++;
    sdcard_diag.last_source = source;

    // Pre-radio probing may reset SPI2 for the card's CMD0 handshake. After
    // radio startup, retries must preserve the active SX1262 peripheral state.
    if (sigurdos_sdcard_may_reset_bus(bus_reset_locked)) {
        if (!sigurdos_shared_spi_reset(PIN_LORA_SCLK, PIN_LORA_MISO,
                                       PIN_LORA_MOSI, PIN_SD_CS)) {
            sdcard_diag.last_error = SIGURDOS_SD_MOUNT_ERROR_BEGIN_FAILED;
            return false;
        }
    } else {
        sigurdos_shared_spi_begin(PIN_LORA_SCLK, PIN_LORA_MISO,
                                  PIN_LORA_MOSI, PIN_SD_CS);
    }

    // The shared SPI bus has three devices (Display CS=12, LoRa CS=9,
    // SD CS=39). Before probing the SD card, explicitly de-assert the
    // other CS lines so they don't float low and corrupt the bus during
    // the card's CMD0 handshake. The ESP32 default pin state is input-
    // only with internal pull-up disabled, so an unconfigured device CS
    // can sit at an indeterminate level.
    pinMode(PIN_LORA_NSS, OUTPUT);
    digitalWrite(PIN_LORA_NSS, HIGH);
    pinMode(PIN_TFT_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);

    // Give the SD card time to stabilise before CMD0.
    // Some cards need >1ms after power-on before they accept CMD0.
    delay(10);

    if (SD.begin(PIN_SD_CS, sd_spi, 4000000, SIGURDOS_SD_MOUNTPOINT)) {
        sdcard_record_success();
        return true;
    }

    SD.end();
    sdcard_reset_mount_state();
    sdcard_diag.mounted = false;
    sdcard_diag.last_error = SIGURDOS_SD_MOUNT_ERROR_BEGIN_FAILED;
    return false;
}

static bool sdcard_mount_with_backoff(SigurdosSdMountSource source, uint8_t max_attempts)
{
    for (uint8_t attempt = 0; attempt < max_attempts; ++attempt) {
        uint32_t backoff_ms = sdcard_backoff_ms(attempt);
        sdcard_diag.last_backoff_ms = backoff_ms;
        if (backoff_ms > 0) {
            delay(backoff_ms);
        }

        if (sdcard_mount_once(source)) {
            return true;
        }
    }

    return false;
}

bool sigurdos_sdcard_init()
{
    sdcard_reset_diagnostics();
    sdcard_reset_mount_state();
    sdcard_retry_count = 0;

    // sdcard_mount_once() calls sigurdos_shared_spi_begin() internally,
    // so we don't need to duplicate it here.

    return sdcard_mount_with_backoff(
        SIGURDOS_SD_MOUNT_SOURCE_INIT,
        SDCARD_INIT_MAX_ATTEMPTS);
}

void sigurdos_sdcard_lock_bus_reset()
{
    bus_reset_locked = true;
}

bool sigurdos_sdcard_bus_reset_locked()
{
    return bus_reset_locked;
}

bool sigurdos_sdcard_retry()
{
    if (mounted) return true;  // already mounted
    if (sdcard_retry_count >= SDCARD_LAZY_RETRY_MAX_ATTEMPTS) {
        sdcard_diag.last_source = SIGURDOS_SD_MOUNT_SOURCE_RETRY;
        sdcard_diag.last_error = SIGURDOS_SD_MOUNT_ERROR_RETRIES_EXHAUSTED;
        sdcard_diag.last_backoff_ms = 0;
        return false;
    }

    sdcard_retry_count++;

    uint32_t backoff_ms = sdcard_backoff_ms((uint8_t)sdcard_retry_count);
    sdcard_diag.last_backoff_ms = backoff_ms;
    if (backoff_ms > 0) {
        delay(backoff_ms);
    }

    return sdcard_mount_once(SIGURDOS_SD_MOUNT_SOURCE_RETRY);
}

bool sigurdos_sdcard_mounted()
{
    return mounted;
}

SigurdosSdMountDiagnostic sigurdos_sdcard_diagnostics()
{
    sdcard_diag.mounted = mounted;
    return sdcard_diag;
}

const char* sigurdos_sdcard_mount_source_name(SigurdosSdMountSource source)
{
    switch (source) {
    case SIGURDOS_SD_MOUNT_SOURCE_INIT:
        return "init";
    case SIGURDOS_SD_MOUNT_SOURCE_RETRY:
        return "retry";
    case SIGURDOS_SD_MOUNT_SOURCE_NONE:
    default:
        return "none";
    }
}

const char* sigurdos_sdcard_mount_error_name(SigurdosSdMountError error)
{
    switch (error) {
    case SIGURDOS_SD_MOUNT_ERROR_BEGIN_FAILED:
        return "begin_failed";
    case SIGURDOS_SD_MOUNT_ERROR_RETRIES_EXHAUSTED:
        return "retries_exhausted";
    case SIGURDOS_SD_MOUNT_ERROR_NONE:
    default:
        return "none";
    }
}

uint64_t sigurdos_sdcard_capacity_bytes()
{
    return capacity_bytes;
}

uint64_t sigurdos_sdcard_free_bytes()
{
    return free_bytes;
}

const char* sigurdos_sdcard_format_size(uint64_t bytes, char* buf, size_t buf_sz)
{
    if (!buf || buf_sz == 0) return "";

    if (bytes >= 1024ULL * 1024 * 1024) {
        snprintf(buf, buf_sz, "%.1f GB",
                 (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024 * 1024) {
        snprintf(buf, buf_sz, "%.1f MB",
                 (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buf, buf_sz, "%lu KB",
                 (unsigned long)(bytes / 1024));
    } else {
        snprintf(buf, buf_sz, "%lu B", (unsigned long)bytes);
    }
    return buf;
}

bool sigurdos_sdcard_exists(const char* path)
{
    if (!mounted || !sigurdos_sdcard_path_valid(path)) return false;
    if (!sdcard_recover_file(path)) return false;
    char live_path[sizeof(SIGURDOS_SD_MOUNTPOINT) + SIGURDOS_SD_MAX_PATH_LEN + 1];
    return sdcard_vfs_path(path, live_path, sizeof(live_path)) &&
        posix_exists(nullptr, live_path);
}

size_t sigurdos_sdcard_read(const char* path, uint8_t* buf, size_t max_len)
{
    if (!mounted || !sigurdos_sdcard_path_valid(path) || !buf || max_len == 0) return 0;

    if (!sdcard_recover_file(path)) return 0;
    char live_path[sizeof(SIGURDOS_SD_MOUNTPOINT) + SIGURDOS_SD_MAX_PATH_LEN + 1];
    if (!sdcard_vfs_path(path, live_path, sizeof(live_path))) return 0;
    FILE* file = std::fopen(live_path, "rb");
    if (!file) return 0;
    const size_t read = std::fread(buf, 1, max_len, file);
    std::fclose(file);
    return read;
}

bool sigurdos_sdcard_list(const char* path, SigurdosSdDirEntry* entries,
                          size_t max_entries, size_t* count, bool* truncated)
{
    if (count) *count = 0;
    if (truncated) *truncated = false;
    if (!mounted || !sigurdos_sdcard_path_valid(path) || !entries ||
        max_entries == 0 || !count) {
        return false;
    }

    char directory_path[sizeof(SIGURDOS_SD_MOUNTPOINT) + SIGURDOS_SD_MAX_PATH_LEN + 1];
    if (!sdcard_vfs_path(path, directory_path, sizeof(directory_path))) return false;
    DIR* directory = ::opendir(directory_path);
    if (!directory) return false;

    bool overflow = false;
    struct dirent* item = nullptr;
    while ((item = ::readdir(directory)) != nullptr) {
        if (std::strcmp(item->d_name, ".") == 0 ||
            std::strcmp(item->d_name, "..") == 0) {
            continue;
        }
        if (*count >= max_entries) {
            overflow = true;
            continue;
        }
        const size_t name_length = std::strlen(item->d_name);
        if (name_length > SIGURDOS_SD_MAX_NAME_LEN) {
            overflow = true;
            continue;
        }

        char item_path[sizeof(directory_path) + SIGURDOS_SD_MAX_NAME_LEN + 2];
        const int written = std::snprintf(
            item_path, sizeof(item_path), "%s%s%s",
            directory_path,
            directory_path[std::strlen(directory_path) - 1] == '/' ? "" : "/",
            item->d_name);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(item_path)) {
            overflow = true;
            continue;
        }

        struct stat info {};
        if (::stat(item_path, &info) != 0) continue;

        SigurdosSdDirEntry& entry = entries[*count];
        std::memcpy(entry.name, item->d_name, name_length + 1);
        entry.is_directory = S_ISDIR(info.st_mode);
        entry.size_bytes = entry.is_directory || info.st_size < 0
            ? 0
            : static_cast<uint64_t>(info.st_size);
        entry.modified_time = info.st_mtime;
        (*count)++;
    }
    ::closedir(directory);

    std::sort(entries, entries + *count,
              [](const SigurdosSdDirEntry& left, const SigurdosSdDirEntry& right) {
        if (left.is_directory != right.is_directory) return left.is_directory;
        return std::strcmp(left.name, right.name) < 0;
    });
    if (truncated) *truncated = overflow;
    return true;
}

bool sigurdos_sdcard_copy_file(const char* source_path, const char* destination_path)
{
    if (!mounted || !sigurdos_sdcard_path_valid(source_path) ||
        !sigurdos_sdcard_path_valid(destination_path) ||
        std::strcmp(source_path, destination_path) == 0) {
        return false;
    }

    char source[sizeof(SIGURDOS_SD_MOUNTPOINT) + SIGURDOS_SD_MAX_PATH_LEN + 1];
    char destination[sizeof(source)];
    if (!sdcard_vfs_path(source_path, source, sizeof(source)) ||
        !sdcard_vfs_path(destination_path, destination, sizeof(destination))) {
        return false;
    }

    struct stat source_info {};
    if (::stat(source, &source_info) != 0 || !S_ISREG(source_info.st_mode)) {
        return false;
    }
    struct stat destination_info {};
    if (::stat(destination, &destination_info) == 0 || errno != ENOENT) {
        return false;
    }

    FILE* input = std::fopen(source, "rb");
    if (!input) return false;
    FILE* output = std::fopen(destination, "wb");
    if (!output) {
        std::fclose(input);
        return false;
    }

    bool success = true;
    uint8_t buffer[1024];
    size_t bytes_read = 0;
    while ((bytes_read = std::fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (std::fwrite(buffer, 1, bytes_read, output) != bytes_read) {
            success = false;
            break;
        }
        delay(0);  // Keep the ESP task watchdog serviced during large copies.
    }
    if (std::ferror(input)) success = false;
    if (success && (std::fflush(output) != 0 || ::fsync(::fileno(output)) != 0)) {
        success = false;
    }
    if (std::fclose(input) != 0) success = false;
    if (std::fclose(output) != 0) success = false;
    if (!success) std::remove(destination);
    return success;
}

bool sigurdos_sdcard_delete_file(const char* path)
{
    if (!mounted || !sigurdos_sdcard_path_valid(path) ||
        std::strcmp(path, "/") == 0) {
        return false;
    }
    char live_path[sizeof(SIGURDOS_SD_MOUNTPOINT) + SIGURDOS_SD_MAX_PATH_LEN + 1];
    if (!sdcard_vfs_path(path, live_path, sizeof(live_path))) return false;
    struct stat info {};
    return ::stat(live_path, &info) == 0 && S_ISREG(info.st_mode) &&
        std::remove(live_path) == 0;
}

bool sigurdos_sdcard_write(const char* path, const uint8_t* data, size_t len)
{
    if (!mounted || !sigurdos_sdcard_path_valid(path)) return false;
    if (len > 0 && !data) return false;  // data required only for non-empty writes

    char live_path[sizeof(SIGURDOS_SD_MOUNTPOINT) + SIGURDOS_SD_MAX_PATH_LEN + 1];
    char temp_path[sizeof(live_path) + 4];
    char ready_path[sizeof(live_path) + 6];
    if (!sdcard_file_paths(path, live_path, sizeof(live_path),
                           temp_path, sizeof(temp_path),
                           ready_path, sizeof(ready_path))) {
        return false;
    }

    return sigurdos::sdcard::detail::replaceFile(
        live_path, temp_path, ready_path, data, len, SDCARD_REPLACE_OPS);
}
