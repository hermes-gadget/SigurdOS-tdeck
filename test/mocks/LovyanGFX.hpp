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

// Mock LovyanGFX.hpp for native PlatformIO testing

#define LGFX_USE_V1

#include <cstdint>

namespace lgfx {
    typedef uint16_t rgb565_t;

    class LGFX_Device {
    public:
        void init() {}
        void setRotation(int) {}
        void setBrightness(int) {}
        void fillScreen(int) {}
        void startWrite() {}
        void endWrite() {}
        void setAddrWindow(int, int, int, int) {}
        void writePixels(rgb565_t*, int) {}
    };

    class Panel_ST7789 {
    public:
        void setBus(class Bus_SPI*) {}
        struct config_t {
            int pin_cs, pin_rst;
            int panel_width, panel_height;
            int offset_x, offset_y, offset_rotation;
            bool invert, rgb_order;
            int memory_width, memory_height;
        };
        void config(const config_t&) {}
        config_t config() { return {}; }
    };

    class Bus_SPI {
    public:
        struct config_t {
            int spi_host, spi_mode;
            int freq_write, freq_read;
            bool spi_3wire, use_lock;
            int dma_channel;
            int pin_sclk, pin_mosi, pin_miso, pin_dc;
        };
        void config(const config_t&) {}
        config_t config() { return {}; }
    };

    class Light_PWM {
    public:
        struct config_t {
            int pin_bl;
            bool invert;
            int freq;
            int pwm_channel;
        };
        void config(const config_t&) {}
        config_t config() { return {}; }
    };
}

using LGFX = lgfx::LGFX_Device;

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
