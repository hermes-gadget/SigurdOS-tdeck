#pragma once

#include <cstdint>

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

// SigurdOS T-Deck Hardware Pin Definitions
// LilyGo T-Deck: ESP32-S3 + ST7789 320x240 + SX1262 LoRa + GT911 Touch

static constexpr int SIGURDOS_GPIO_DISABLED = -1;
static constexpr int SIGURDOS_ESP32S3_GPIO_MIN = 0;
static constexpr int SIGURDOS_ESP32S3_GPIO_MAX = 48;

static constexpr bool sigurdos_gpio_is_valid(int pin) {
    return pin >= SIGURDOS_ESP32S3_GPIO_MIN && pin <= SIGURDOS_ESP32S3_GPIO_MAX;
}

static constexpr uint64_t sigurdos_gpio_mask(int pin) {
    return sigurdos_gpio_is_valid(pin) ? (uint64_t{1} << pin) : 0ULL;
}

// ════════════════════════════════════════════════════════
// LoRa SX1262 (SPI)
// ════════════════════════════════════════════════════════
#define PIN_LORA_NSS      9
#define PIN_LORA_DIO1    45
#define PIN_LORA_RESET   17
#define PIN_LORA_BUSY    13
#define PIN_LORA_SCLK    40
#define PIN_LORA_MISO    38
#define PIN_LORA_MOSI    41

static constexpr uint64_t SIGURDOS_LORA_DIO1_WAKE_MASK =
    sigurdos_gpio_mask(PIN_LORA_DIO1);

// ════════════════════════════════════════════════════════
// Display ST7789 320x240 (shares SPI with LoRa)
// ════════════════════════════════════════════════════════
#define PIN_TFT_CS       12
#define PIN_TFT_DC       11
#define PIN_TFT_RST      -1
#define PIN_TFT_BL       42
#define PIN_TFT_SCL      PIN_LORA_SCLK   // shared SPI clock
#define PIN_TFT_SDA      PIN_LORA_MOSI   // shared SPI data
#define TFT_WIDTH       320
#define TFT_HEIGHT      240

// ════════════════════════════════════════════════════════
// Touch GT911 (I2C)
// ════════════════════════════════════════════════════════
#define PIN_TOUCH_SDA    18
#define PIN_TOUCH_SCL     8
#define PIN_TOUCH_INT    16
#define PIN_TOUCH_RST    -1
#define TOUCH_I2C_ADDR  0x5D
#define TOUCH_MAX_X      TFT_WIDTH
#define TOUCH_MAX_Y      TFT_HEIGHT
#define TOUCH_SENSOR_X   TFT_HEIGHT  // GT911 portrait X max (pre-swap); equals display height
#define TOUCH_SENSOR_Y   TFT_WIDTH   // GT911 portrait Y max (pre-swap); equals display width
#define TOUCH_SWAP_XY    true    // GT911 is portrait glass (240×320); swap axes for landscape LVGL
#define TOUCH_MIRROR_X   false
#define TOUCH_MIRROR_Y   true    // portrait X increases bottom→top in landscape; invert for LVGL

// ── I2C bus aliases (shared with touch) ──────────────────
#define PIN_I2C_SDA      PIN_TOUCH_SDA
#define PIN_I2C_SCL      PIN_TOUCH_SCL

// ════════════════════════════════════════════════════════
// T-Deck Keyboard (ESP32-C3 I2C slave at 0x55)
// ════════════════════════════════════════════════════════
#define TDECK_KB_I2C_ADDR  0x55   // keyboard MCU I2C address

// ════════════════════════════════════════════════════════
// Trackball / User Button
// ════════════════════════════════════════════════════════
#define PIN_TRACKBALL          0   // Center click / BOOT button
#define PIN_TRACKBALL_BTN      PIN_TRACKBALL
#define PIN_TRACKBALL_LEFT     1
#define PIN_TRACKBALL_RIGHT    2
#define PIN_TRACKBALL_UP       3
#define PIN_TRACKBALL_DOWN    15

// ════════════════════════════════════════════════════════
// Battery & Power
// ════════════════════════════════════════════════════════
#define PIN_BAT_ADC       4
#define PIN_PERIPH_PWR   10
#define BAT_ADC_MULT     (2.0f * 3.3f * 1000.0f)
#define BAT_MIN_MV      3000
#define BAT_MAX_MV      4200

// ════════════════════════════════════════════════════════
// GPS (Serial1)
// ════════════════════════════════════════════════════════
#define PIN_GPS_RX       44  // ESP32-S3 RX; LILYGO GPSShield uses rx=44
#define PIN_GPS_TX       43  // ESP32-S3 TX; LILYGO GPSShield uses tx=43
#define GPS_PRIMARY_BAUD_RATE 9600
#define GPS_FALLBACK_BAUD_RATE 38400
#define GPS_BAUD_RATE GPS_PRIMARY_BAUD_RATE

// ════════════════════════════════════════════════════════
// SD Card (SPI mode, shared bus with LoRa/Display)
// SCK=PIN_LORA_SCLK(40), MISO=PIN_LORA_MISO(38), MOSI=PIN_LORA_MOSI(41)
// ════════════════════════════════════════════════════════
#define PIN_SD_CS        39  // T-Deck microSD chip select (GPIO 39)

// ════════════════════════════════════════════════════════
// Audio Buzzer
// ════════════════════════════════════════════════════════
#define PIN_BUZZER       46

// ════════════════════════════════════════════════════════
// LoRa Radio Defaults
// ════════════════════════════════════════════════════════
#ifndef LORA_FREQ
#define LORA_FREQ    869.618f
#endif
#ifndef LORA_BW
#define LORA_BW        62.5f
#endif
#ifndef LORA_SF
#define LORA_SF           8
#endif
#ifndef LORA_CR
#define LORA_CR           5
#endif
#ifndef LORA_TX_PWR
#define LORA_TX_PWR      22
#endif

// ════════════════════════════════════════════════════════
// MeshCore expects P_ prefix for radio pins
// ════════════════════════════════════════════════════════
#define P_LORA_NSS    PIN_LORA_NSS
#define P_LORA_DIO_1  PIN_LORA_DIO1
#define P_LORA_RESET  PIN_LORA_RESET
#define P_LORA_BUSY   PIN_LORA_BUSY
#define P_LORA_SCLK   PIN_LORA_SCLK
#define P_LORA_MISO   PIN_LORA_MISO
#define P_LORA_MOSI   PIN_LORA_MOSI

// Firmware version — displayed in Settings > About
// Can be overridden at build time by scripts/build_metadata.py (git describe --tags)
#ifndef SIGURDOS_VERSION
#define SIGURDOS_VERSION  "beta-0.1.39"
#endif
