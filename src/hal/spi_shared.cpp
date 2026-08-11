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

// SPIClass definition comes from <SPI.h> on real ESP32 hardware, or from
// the mock Arduino.h in native test builds. Include both to cover all cases.
#ifdef ESP32_PLATFORM
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#define SIGURDOS_SHARED_SPI_HOST FSPI
#else
#include <Arduino.h>  // native test mock
#include <chrono>
#include <mutex>
#define SIGURDOS_SHARED_SPI_HOST 1
#endif
#include "spi_shared.h"

// Single SPIClass instance for the shared SPI2_HOST (FSPI) bus.
// Both the LoRa radio (mesh_wrapper.cpp) and SD card (sdcard.cpp) use this
// instance so all transactions share the same IDF bus lock.
//
// Repeating SPIClass::begin() does not reset an already initialized ESP32 bus.
// Pre-radio SD probing uses the explicit end()/begin() reset below; after the
// radio starts, callers only reuse the initialized singleton.
//
// Note: LovyanGFX (display) manages its own bus access on SPI2_HOST through
// the IDF SPI driver directly and is not affected by this SPI class state.
static SPIClass s_shared_spi(SIGURDOS_SHARED_SPI_HOST);
static bool s_shared_spi_started = false;

#ifdef ESP32_PLATFORM
static SemaphoreHandle_t s_shared_spi_mutex = nullptr;

static SemaphoreHandle_t shared_spi_mutex()
{
    if (!s_shared_spi_mutex) {
        // The first take occurs during single-threaded boot. Recursive
        // ownership is required because a mesh RX callback can persist an SD
        // message while mesh::loop() owns the radio transaction boundary.
        s_shared_spi_mutex = xSemaphoreCreateRecursiveMutex();
    }
    return s_shared_spi_mutex;
}
#else
static std::recursive_timed_mutex s_shared_spi_mutex;
static thread_local uint32_t s_shared_spi_lock_depth = 0;
#endif

SPIClass& sigurdos_shared_spi()
{
    return s_shared_spi;
}

void sigurdos_shared_spi_begin(int sck, int miso, int mosi)
{
    sigurdos_shared_spi_begin(sck, miso, mosi, -1);
}

void sigurdos_shared_spi_begin(int sck, int miso, int mosi, int cs)
{
    if (s_shared_spi_started) return;
    if (cs >= 0) {
        s_shared_spi.begin(sck, miso, mosi, cs);
    } else {
        s_shared_spi.begin(sck, miso, mosi);
    }
    s_shared_spi_started = true;
}

bool sigurdos_shared_spi_reset(int sck, int miso, int mosi, int cs)
{
    if (s_shared_spi_started) {
        s_shared_spi.end();
        s_shared_spi_started = false;
    }
    sigurdos_shared_spi_begin(sck, miso, mosi, cs);
    return s_shared_spi_started;
}

bool sigurdos_shared_spi_try_lock(SigurdosSharedSpiDevice device,
                                  uint32_t timeout_ms)
{
    (void)device;
#ifdef ESP32_PLATFORM
    SemaphoreHandle_t mutex = shared_spi_mutex();
    if (!mutex) return false;
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms > 0 && ticks == 0) ticks = 1;
    return xSemaphoreTakeRecursive(mutex, ticks) == pdTRUE;
#else
    const bool acquired = timeout_ms == 0
        ? s_shared_spi_mutex.try_lock()
        : s_shared_spi_mutex.try_lock_for(std::chrono::milliseconds(timeout_ms));
    if (acquired) ++s_shared_spi_lock_depth;
    return acquired;
#endif
}

void sigurdos_shared_spi_unlock()
{
#ifdef ESP32_PLATFORM
    if (s_shared_spi_mutex) {
        // FreeRTOS rejects a recursive-mutex give by a non-owner. Guards call
        // this only after a successful take; the check also keeps the legacy
        // SD compatibility wrappers harmless after a timed-out take.
        (void)xSemaphoreGiveRecursive(s_shared_spi_mutex);
    }
#else
    if (s_shared_spi_lock_depth == 0) return;
    --s_shared_spi_lock_depth;
    s_shared_spi_mutex.unlock();
#endif
}
