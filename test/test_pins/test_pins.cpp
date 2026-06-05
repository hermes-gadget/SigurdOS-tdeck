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
 * Unit tests for T-Deck pin definitions
 * Validates: no pin conflicts, valid GPIO ranges, reasonable assignments
 */
#include <gtest/gtest.h>
#include <cstdint>
#include <set>

// Include pin definitions
#include "hal/tdeck_pins.h"

namespace {

class PinsTest : public ::testing::Test {};

// ── Pin ranges (ESP32-S3: GPIO 0-48) ────────────────────
TEST_F(PinsTest, AllDefinedPinsInValidGPIORange) {
    // Collect all defined GPIO pins
    std::vector<int> pins = {
        PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RESET, PIN_LORA_BUSY,
        PIN_LORA_SCLK, PIN_LORA_MISO, PIN_LORA_MOSI,
        PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_BL,
        PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_INT,
        PIN_TRACKBALL, PIN_PERIPH_PWR, PIN_BAT_ADC,
        PIN_GPS_RX, PIN_GPS_TX,
        PIN_SD_CS, PIN_BUZZER,
        PIN_I2C_SDA, PIN_I2C_SCL,
    };

    for (int p : pins) {
        if (p >= 0) { // -1 means "not connected", skip
            EXPECT_GE(p, 0)  << "Pin " << p << " is negative";
            EXPECT_LE(p, 48) << "Pin " << p << " exceeds ESP32-S3 GPIO range";
        }
    }
}

// GPIO helper coverage
TEST_F(PinsTest, DisabledSentinelIsNotValidGpio) {
    EXPECT_EQ(PIN_TFT_RST, SIGURDOS_GPIO_DISABLED);
    EXPECT_EQ(PIN_TOUCH_RST, SIGURDOS_GPIO_DISABLED);
    EXPECT_FALSE(sigurdos_gpio_is_valid(SIGURDOS_GPIO_DISABLED));
}

TEST_F(PinsTest, GpioValidityCoversRangeBoundaries) {
    EXPECT_TRUE(sigurdos_gpio_is_valid(SIGURDOS_ESP32S3_GPIO_MIN));
    EXPECT_TRUE(sigurdos_gpio_is_valid(SIGURDOS_ESP32S3_GPIO_MAX));
    EXPECT_FALSE(sigurdos_gpio_is_valid(SIGURDOS_ESP32S3_GPIO_MIN - 1));
    EXPECT_FALSE(sigurdos_gpio_is_valid(SIGURDOS_ESP32S3_GPIO_MAX + 1));
}

TEST_F(PinsTest, GpioMaskUsesSixtyFourBitShiftForHighPins) {
    const uint64_t dio1_mask = sigurdos_gpio_mask(PIN_LORA_DIO1);

    EXPECT_EQ(PIN_LORA_DIO1, 45);
    EXPECT_EQ(dio1_mask, (1ULL << PIN_LORA_DIO1));
    EXPECT_NE(dio1_mask, 0ULL);
}

TEST_F(PinsTest, LoraDio1WakeMaskUsesGpioHelper) {
    EXPECT_EQ(SIGURDOS_LORA_DIO1_WAKE_MASK, sigurdos_gpio_mask(PIN_LORA_DIO1));
    EXPECT_EQ(SIGURDOS_LORA_DIO1_WAKE_MASK, (1ULL << PIN_LORA_DIO1));
    EXPECT_NE(SIGURDOS_LORA_DIO1_WAKE_MASK, 0ULL);
}

TEST_F(PinsTest, GpioMaskRejectsInvalidPins) {
    EXPECT_EQ(sigurdos_gpio_mask(SIGURDOS_GPIO_DISABLED), 0ULL);
    EXPECT_EQ(sigurdos_gpio_mask(SIGURDOS_ESP32S3_GPIO_MAX + 1), 0ULL);
}

// No pin conflicts on shared buses
TEST_F(PinsTest, NoSPIConflicts) {
    // TFT and LoRa share SPI bus — SCLK and MOSI must match
    EXPECT_EQ(PIN_TFT_SCL, PIN_LORA_SCLK);
    EXPECT_EQ(PIN_TFT_SDA, PIN_LORA_MOSI);

    // CS pins must be unique per device on the bus
    EXPECT_NE(PIN_LORA_NSS, PIN_TFT_CS);
    EXPECT_NE(PIN_LORA_NSS, PIN_SD_CS);
    EXPECT_NE(PIN_TFT_CS, PIN_SD_CS);
}

TEST_F(PinsTest, NoI2CConflicts) {
    // I2C pins should be consistent
    EXPECT_EQ(PIN_TOUCH_SDA, PIN_I2C_SDA);
    EXPECT_EQ(PIN_TOUCH_SCL, PIN_I2C_SCL);
}

TEST_F(PinsTest, NoDuplicateGPIOPins) {
    std::set<int> used;
    std::vector<int> pins = {
        PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RESET, PIN_LORA_BUSY,
        PIN_LORA_SCLK, PIN_LORA_MISO, PIN_LORA_MOSI,
        PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_BL,
        PIN_TOUCH_INT,
        PIN_TRACKBALL, PIN_PERIPH_PWR, PIN_BAT_ADC,
        PIN_GPS_RX, PIN_GPS_TX,
        PIN_SD_CS, PIN_BUZZER,
    };
    // Note: I2C SDA/SCL share with touch, so they ARE duplicates by design
    // SPI CLK/MOSI share between TFT and LoRa, also by design

    int duplicates = 0;
    for (int p : pins) {
        if (p >= 0) {
            if (used.count(p)) {
                // Acceptable duplicates: I2C == Touch, and SPI bus sharing
                if (p == PIN_I2C_SDA || p == PIN_I2C_SCL ||
                    p == PIN_TFT_SCL || p == PIN_TFT_SDA) {
                    // Expected shared pins, OK
                } else {
                    duplicates++;
                }
                used.insert(p);
            } else {
                used.insert(p);
            }
        }
    }
    EXPECT_EQ(duplicates, 0) << "Unexpected duplicate GPIO pin assignments found";
}

// ── ADC pin is valid ────────────────────────────────────
TEST_F(PinsTest, BatteryPinIsADC1Capable) {
    // ESP32-S3 ADC1 channels: GPIO 1-10
    EXPECT_GE(PIN_BAT_ADC, 1);
    EXPECT_LE(PIN_BAT_ADC, 10);
}

// ── Display dimensions are sensible ─────────────────────
TEST_F(PinsTest, DisplayDimensionsAreValid) {
    EXPECT_EQ(TFT_WIDTH, 320);
    EXPECT_EQ(TFT_HEIGHT, 240);
    EXPECT_GT(TFT_WIDTH, 0);
    EXPECT_GT(TFT_HEIGHT, 0);
}

// ── LoRa defaults are within valid ranges ────────────────
TEST_F(PinsTest, LoraDefaultsInRange) {
    // Frequency: 868-870 MHz (EU) or 902-928 MHz (US)
    EXPECT_GE(LORA_FREQ, 860.0f);
    EXPECT_LE(LORA_FREQ, 930.0f);

    // Bandwidth: 7.8 - 500 kHz for SX1262
    EXPECT_GE(LORA_BW, 7.0f);
    EXPECT_LE(LORA_BW, 510.0f);

    // Spreading factor: 5-12 for SX1262
    EXPECT_GE(LORA_SF, 5);
    EXPECT_LE(LORA_SF, 12);

    // Coding rate: 5-8
    EXPECT_GE(LORA_CR, 5);
    EXPECT_LE(LORA_CR, 8);

    // TX power: -9 to +22 dBm for SX1262
    EXPECT_GE(LORA_TX_PWR, -10);
    EXPECT_LE(LORA_TX_PWR, 23);
}

// ── GPS baud rate is standard ───────────────────────────
TEST_F(PinsTest, GPSBaudRateIsValid) {
    EXPECT_GE(GPS_BAUD_RATE, 4800);
    EXPECT_LE(GPS_BAUD_RATE, 115200);
    // Must be a standard rate
    EXPECT_TRUE(
        GPS_BAUD_RATE == 4800  || GPS_BAUD_RATE == 9600  ||
        GPS_BAUD_RATE == 19200 || GPS_BAUD_RATE == 38400 ||
        GPS_BAUD_RATE == 57600 || GPS_BAUD_RATE == 115200
    );
}

// ── Battery voltage range ────────────────────────────────
TEST_F(PinsTest, BatteryVoltageRangeIsSensible) {
    // LiPo: 3.0V min to 4.2V max
    EXPECT_LE(BAT_MIN_MV, 3200);
    EXPECT_GE(BAT_MAX_MV, 4100);
    EXPECT_LT(BAT_MIN_MV, BAT_MAX_MV);

    // ADC multiplier sanity: 2x voltage divider * 3.3V ref * 1000
    EXPECT_GT(BAT_ADC_MULT, 5000.0f);
    EXPECT_LT(BAT_ADC_MULT, 10000.0f);
}

} // anonymous namespace
