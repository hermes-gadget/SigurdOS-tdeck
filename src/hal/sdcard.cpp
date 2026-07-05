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
#include "tdeck_pins.h"
#include "spi_shared.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

// T-Deck SD card uses SPI on the shared LoRa/display bus (GPIO40/38/41).
// FSPI (SPI2_HOST) is used here; the display also uses SPI2_HOST (via
// LovyanGFX), and the LoRa radio uses SPI2_HOST (via RadioLib). All three
// devices share the same SPI host and bus pins with different CS lines.
// We use the shared singleton SPIClass from spi_shared.h to avoid reinitialising
// SPI2_HOST independently from the LoRa radio.
static SPIClass& sd_spi = sigurdos_shared_spi();

static bool mounted = false;
static uint64_t capacity_bytes = 0;
static uint64_t free_bytes = 0;

static constexpr uint8_t SDCARD_INIT_MAX_ATTEMPTS = 3;
static constexpr uint8_t SDCARD_LAZY_RETRY_MAX_ATTEMPTS = 3;

static int sdcard_retry_count = 0;  // total additional lazy retry attempts

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

    // Re-initialise the shared SPI bus before every mount attempt.
    // The ESP32 SPIClass.begin() issues periph_module_reset() on SPI2,
    // which the SD card's GO_IDLE_STATE (CMD0) requires to handshake.
    // This is especially important on retries where SD.end() was called.
    sigurdos_shared_spi_begin(PIN_LORA_SCLK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_SD_CS);

    // Give the SD card time to stabilise after SPI bus reset.
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
    return SD.exists(path);
}

size_t sigurdos_sdcard_read(const char* path, uint8_t* buf, size_t max_len)
{
    if (!mounted || !sigurdos_sdcard_path_valid(path) || !buf || max_len == 0) return 0;

    File f = SD.open(path, FILE_READ);
    if (!f) return 0;

    size_t read = f.read(buf, max_len);
    f.close();
    return read;
}

bool sigurdos_sdcard_write(const char* path, const uint8_t* data, size_t len)
{
    if (!mounted || !sigurdos_sdcard_path_valid(path)) return false;
    if (len > 0 && !data) return false;  // data required only for non-empty writes

    // SD.begin() with FILE_WRITE opens for append — remove first so we replace the file
    if (SD.exists(path)) {
        SD.remove(path);
    }

    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;

    if (len > 0) {
        size_t written = f.write(data, len);
        f.close();
        return written == len;
    }
    // Zero-length write — create/truncate an empty file
    f.close();
    return true;
}
