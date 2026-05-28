// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SlopOS-TDeck.
//
// SlopOS-TDeck is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SlopOS-TDeck is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SlopOS-TDeck.  If not, see <https://www.gnu.org/licenses/>.


#include "sdcard.h"
#include "tdeck_pins.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

// T-Deck SD card uses SPI on the shared LoRa/display bus (GPIO40/38/41).
// FSPI (SPI2_HOST) is used here; the display also uses SPI2_HOST (via
// LovyanGFX), and the LoRa radio uses SPI2_HOST (via RadioLib). All three
// devices share the same SPI host and bus pins with different CS lines.
static SPIClass sd_spi(FSPI);

static bool mounted = false;
static uint64_t capacity_bytes = 0;
static uint64_t free_bytes = 0;

bool slopos_sdcard_init()
{
    sd_spi.begin(PIN_LORA_SCLK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_SD_CS);

    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) delay(500);
        if (SD.begin(PIN_SD_CS, sd_spi, 4000000, SLOPOS_SD_MOUNTPOINT)) {
            capacity_bytes = (uint64_t)SD.totalBytes();
            free_bytes     = (uint64_t)(SD.totalBytes() - SD.usedBytes());
            mounted = true;
            return true;
        }
    }

    mounted = false;
    return false;
}

bool slopos_sdcard_mounted()
{
    return mounted;
}

uint64_t slopos_sdcard_capacity_bytes()
{
    return capacity_bytes;
}

uint64_t slopos_sdcard_free_bytes()
{
    return free_bytes;
}

const char* slopos_sdcard_format_size(uint64_t bytes, char* buf, size_t buf_sz)
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

bool slopos_sdcard_exists(const char* path)
{
    if (!mounted || !path) return false;
    return SD.exists(path);
}

size_t slopos_sdcard_read(const char* path, uint8_t* buf, size_t max_len)
{
    if (!mounted || !path || !buf || max_len == 0) return 0;

    File f = SD.open(path, FILE_READ);
    if (!f) return 0;

    size_t read = f.read(buf, max_len);
    f.close();
    return read;
}

bool slopos_sdcard_write(const char* path, const uint8_t* data, size_t len)
{
    if (!mounted || !path || !data || len == 0) return false;

    // SD.begin() with FILE_WRITE opens for append — remove first so we replace the file
    if (SD.exists(path)) {
        SD.remove(path);
    }

    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;

    size_t written = f.write(data, len);
    f.close();
    return written == len;
}
