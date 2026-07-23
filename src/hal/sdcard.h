#pragma once

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

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <ctime>

// VFS mountpoint — use this prefix for POSIX file I/O on the SD card
#define SIGURDOS_SD_MOUNTPOINT "/sdcard"

static constexpr size_t SIGURDOS_SD_MAX_PATH_LEN = 255;
static constexpr size_t SIGURDOS_SD_MAX_NAME_LEN = 127;

struct SigurdosSdDirEntry {
    char name[SIGURDOS_SD_MAX_NAME_LEN + 1];
    uint64_t size_bytes;
    std::time_t modified_time;
    bool is_directory;
};

inline bool sigurdos_sdcard_may_reset_bus(bool reset_locked)
{
    return !reset_locked;
}

enum SigurdosSdMountSource : uint8_t {
    SIGURDOS_SD_MOUNT_SOURCE_NONE = 0,
    SIGURDOS_SD_MOUNT_SOURCE_INIT,
    SIGURDOS_SD_MOUNT_SOURCE_RETRY,
};

enum SigurdosSdMountError : uint8_t {
    SIGURDOS_SD_MOUNT_ERROR_NONE = 0,
    SIGURDOS_SD_MOUNT_ERROR_BEGIN_FAILED,
    SIGURDOS_SD_MOUNT_ERROR_RETRIES_EXHAUSTED,
};

struct SigurdosSdMountDiagnostic {
    bool mounted;
    uint8_t attempt_count;  // Total SD.begin() calls since the last boot init reset.
    SigurdosSdMountSource last_source;
    SigurdosSdMountError last_error;
    uint32_t last_backoff_ms;
};

inline bool sigurdos_sdcard_path_valid(const char* path)
{
    if (!path || path[0] == '\0') return false;
    if (path[0] != '/') return false;
    if (std::strstr(path, "..")) return false;
    if (std::strlen(path) > SIGURDOS_SD_MAX_PATH_LEN) return false;
    return true;
}

// Initialize SD card over SPI with short bounded retry/backoff for warm reboot recovery.
// Call sigurdos_sdcard_retry() lazily when a consumer needs the card
bool sigurdos_sdcard_init();

// Permanently disable SPI peripheral resets after the radio driver starts.
// Lazy retries continue on the configured bus without invalidating SX1262.
void sigurdos_sdcard_lock_bus_reset();
bool sigurdos_sdcard_bus_reset_locked();

// Retry SD mount (called lazily from consumers like the map renderer)
// Caps total retries at 3 to avoid unbounded re-probing of a broken card.
// Returns true if mounted (either already or after this attempt).
bool sigurdos_sdcard_retry();

// Check if SD card is currently mounted
bool sigurdos_sdcard_mounted();

// Last mount diagnostic state for telemetry/UI/debug surfaces.
SigurdosSdMountDiagnostic sigurdos_sdcard_diagnostics();
const char* sigurdos_sdcard_mount_source_name(SigurdosSdMountSource source);
const char* sigurdos_sdcard_mount_error_name(SigurdosSdMountError error);

// Filesystem info
uint64_t sigurdos_sdcard_capacity_bytes();
uint64_t sigurdos_sdcard_free_bytes();

// Format size string for display
const char* sigurdos_sdcard_format_size(uint64_t bytes, char* buf, size_t buf_sz);

// File operations (optional — for map tiles, logs)
bool sigurdos_sdcard_exists(const char* path);
size_t sigurdos_sdcard_read(const char* path, uint8_t* buf, size_t max_len);
// List one directory. Entries are sorted with directories first, then by name.
// `truncated` is set when more entries exist than fit in the caller's buffer.
bool sigurdos_sdcard_list(const char* path, SigurdosSdDirEntry* entries,
                          size_t max_entries, size_t* count, bool* truncated);
bool sigurdos_sdcard_copy_file(const char* source_path, const char* destination_path);
bool sigurdos_sdcard_delete_file(const char* path);
// Persistence-safe replacement: syncs a same-directory temp, marks it ready,
// then atomically promotes it. Reads recover any interrupted promotion.
bool sigurdos_sdcard_write(const char* path, const uint8_t* data, size_t len);
