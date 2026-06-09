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

#include "spi_shared.h"
#include <Arduino.h>  // provides SPIClass (real or mock)

// Single SPIClass instance for the shared SPI2_HOST (FSPI) bus.
// Initialised on first call to sigurdos_shared_spi_begin(). Both the LoRa
// radio (mesh_wrapper.cpp) and SD card (sdcard.cpp) use this instance so the
// bus is configured exactly once and all transactions share the same IDF bus
// lock. Note: LovyanGFX (display) manages its own bus access on SPI2_HOST
// through the IDF SPI driver directly.
static SPIClass s_shared_spi(FSPI);
static bool     s_shared_spi_begun = false;

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
    if (!s_shared_spi_begun) {
        if (cs >= 0) {
            s_shared_spi.begin(sck, miso, mosi, cs);
        } else {
            s_shared_spi.begin(sck, miso, mosi);
        }
        s_shared_spi_begun = true;
    }
}
