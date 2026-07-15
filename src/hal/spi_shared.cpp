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
#define SIGURDOS_SHARED_SPI_HOST FSPI
#else
#include <Arduino.h>  // native test mock
#define SIGURDOS_SHARED_SPI_HOST 1
#endif
#include "spi_shared.h"

// Single SPIClass instance for the shared SPI2_HOST (FSPI) bus.
// Both the LoRa radio (mesh_wrapper.cpp) and SD card (sdcard.cpp) use this
// instance so all transactions share the same IDF bus lock.
//
// NOTE: sigurdos_shared_spi_begin() intentionally re-calls s_shared_spi.begin()
// on every invocation rather than guarding with a one-shot flag. The ESP32
// SPIClass.begin() calls spiStartBus() which issues periph_module_reset() on
// SPI2 — resetting the peripheral hardware. The SD card's GO_IDLE_STATE (CMD0)
// requires this fresh hardware state to handshake correctly. Without the reset,
// the bus has stale state from the LoRa init and the SD card never responds.
// Repeated begin() is safe: transaction-specific settings (clock speed, mode)
// are applied per-transaction via SPI.beginTransaction().
//
// Note: LovyanGFX (display) manages its own bus access on SPI2_HOST through
// the IDF SPI driver directly and is not affected by this SPI class state.
static SPIClass s_shared_spi(SIGURDOS_SHARED_SPI_HOST);

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
    if (cs >= 0) {
        s_shared_spi.begin(sck, miso, mosi, cs);
    } else {
        s_shared_spi.begin(sck, miso, mosi);
    }
}
