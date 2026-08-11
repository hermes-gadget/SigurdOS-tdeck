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

/**
 * Shared SPI2_HOST (FSPI) bus singleton.
 *
 * The T-Deck's SPI bus (SCK=40, MOSI=41, MISO=38) is shared by three
 * peripherals: display (ST7789 via LovyanGFX), LoRa (SX1262 via RadioLib),
 * and microSD card. LovyanGFX manages its own bus internally using the IDF
 * SPI driver directly. The LoRa radio and SD card each used separate Arduino
 * SPIClass instances on the same host, causing double bus initialisation.
 *
 * This singleton provides a single SPIClass(FSPI) instance that both the
 * LoRa radio and SD card should use, ensuring the bus is initialised once
 * and all transactions share the same bus lock.
 *
 * Drivers on SPI2_HOST do not all share one library-level mutex: LovyanGFX
 * uses the IDF bus directly while RadioLib and SD enter through SPIClass.
 * The arbiter below is therefore the outer, firmware-owned transaction
 * boundary for all three devices. Every take is bounded; callers must skip or
 * fail the operation when acquisition times out instead of touching the bus.
 *
 * NOTE: Do not #include <SPI.h> in this header — it breaks native test builds.
 * SPIClass is available through Arduino.h (real or mock) which every file
 * that includes this header already pulls in.
 */

#include <cstdint>

class SPIClass;  // forward decl — full definition from Arduino.h / mock

enum class SigurdosSharedSpiDevice : uint8_t {
    Display,
    SdCard,
    Radio,
};

// These waits are deliberately far below the 10-second loopTask watchdog.
// Display keeps the existing 150 ms frame-drop budget. SD workers wait only
// long enough for a normal polling-mode display flush, and radio loop work has
// the shortest budget so mesh/companion servicing remains responsive.
static constexpr uint32_t SIGURDOS_SHARED_SPI_DISPLAY_TIMEOUT_MS = 150;
static constexpr uint32_t SIGURDOS_SHARED_SPI_SD_TIMEOUT_MS = 50;
static constexpr uint32_t SIGURDOS_SHARED_SPI_RADIO_TIMEOUT_MS = 25;
static constexpr uint32_t SIGURDOS_SHARED_SPI_STARTUP_TIMEOUT_MS = 250;

SPIClass& sigurdos_shared_spi();
void      sigurdos_shared_spi_begin(int sck, int miso, int mosi);
void      sigurdos_shared_spi_begin(int sck, int miso, int mosi, int cs);
bool      sigurdos_shared_spi_reset(int sck, int miso, int mosi, int cs);

bool sigurdos_shared_spi_try_lock(SigurdosSharedSpiDevice device,
                                  uint32_t timeout_ms);
void sigurdos_shared_spi_unlock();

class SigurdosSharedSpiGuard {
public:
    SigurdosSharedSpiGuard(SigurdosSharedSpiDevice device,
                           uint32_t timeout_ms)
        : acquired_(sigurdos_shared_spi_try_lock(device, timeout_ms)) {}

    ~SigurdosSharedSpiGuard() { release(); }

    SigurdosSharedSpiGuard(const SigurdosSharedSpiGuard&) = delete;
    SigurdosSharedSpiGuard& operator=(const SigurdosSharedSpiGuard&) = delete;

    bool acquired() const { return acquired_; }
    explicit operator bool() const { return acquired_; }

    void release()
    {
        if (!acquired_) return;
        sigurdos_shared_spi_unlock();
        acquired_ = false;
    }

private:
    bool acquired_;
};
