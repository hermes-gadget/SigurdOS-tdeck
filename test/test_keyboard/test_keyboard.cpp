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


/**
 * Unit tests for T-Deck keyboard driver (I2C slave at 0x55)
 *
 * Architecture: The T-Deck keyboard is a separate ESP32-C3 MCU that scans
 * the physical matrix and returns key codes over I2C.
 *
 * NOTE: keyboard.cpp uses static `initialized` flag that persists across
 * tests. Test order matters — init-failure and init-side-effect tests
 * must run BEFORE any successful init. All other tests call init_with_ack().
 *
 * Reference: Xinyuan-LilyGO/T-Deck examples/Keyboard_ESP32C3/
 * License: MIT
 */
#include <gtest/gtest.h>
#include "hal/tdeck_pins.h"
#include "hal/keyboard.h"
#include "Arduino.h"
#include <cstdint>

namespace {

class KeyboardTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
        Wire = TwoWire();
    }

    void init_with_ack() {
        slopos_keyboard_reset_scan_state();
        // Queue a probe-response byte for slopos_keyboard_init().
        // If init is idempotent (already initialized from prior test),
        // the byte won't be consumed — drain it explicitly so it
        // doesn't leak into the next keyboard scan.
        Wire.mock_queue_rx_byte(0x00);
        slopos_keyboard_init();
        Wire.requestFrom(0x55, 1);
        if (Wire.available()) { Wire.read(); }
        // Advance clock past the 10ms poll interval
        arduino_mock::current_millis = 11;
    }
};

// ════════════════════════════════════════════════════════
// INIT TESTS (must run first — static initialized flag)
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, InitFailsWhenKeyboardNotResponding) {
    EXPECT_FALSE(slopos_keyboard_init());
}

TEST_F(KeyboardTest, InitSendsDefaultBrightnessOnFirstSuccess) {
    Wire.mock_queue_rx_byte(0x00);
    EXPECT_TRUE(slopos_keyboard_init());

    // Init sends two commands:
    //   1. Default brightness (0x02 + 127)
    //   2. Key mode switch   (0x04)
    // The mock only records the LAST transmission.
    EXPECT_EQ(Wire.mock_last_tx_addr(), 0x55u);
    // Last transmission should be key-mode command
    EXPECT_EQ(Wire.mock_last_tx_data(0), 0x04u);
}

TEST_F(KeyboardTest, InitProbesKeyboardAtCorrectAddress) {
    init_with_ack();
    SUCCEED();
}

TEST_F(KeyboardTest, InitIsIdempotent) {
    init_with_ack();
    Wire.mock_set_error(1);
    EXPECT_TRUE(slopos_keyboard_init());
}

// ════════════════════════════════════════════════════════
// KEY READING TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, NoKeyReturnsZero) {
    init_with_ack();
    Wire.mock_queue_rx_byte(0x00);
    slopos_keyboard_scan();
    EXPECT_EQ(slopos_keyboard_get_key(), 0u);
    EXPECT_FALSE(slopos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyPressReturnsAsciiValue) {
    init_with_ack();
    Wire.mock_queue_rx_byte('a');
    slopos_keyboard_scan();
    EXPECT_EQ(slopos_keyboard_get_key(), 0x61u);
    EXPECT_TRUE(slopos_keyboard_consume_event());
}

TEST_F(KeyboardTest, ReadReturnsEnter) {
    init_with_ack();
    Wire.mock_queue_rx_byte(0x0D);
    slopos_keyboard_scan();
    EXPECT_EQ(slopos_keyboard_get_key(), 0x0Du);
}

TEST_F(KeyboardTest, ReadReturnsBackspace) {
    init_with_ack();
    Wire.mock_queue_rx_byte(0x08);
    slopos_keyboard_scan();
    EXPECT_EQ(slopos_keyboard_get_key(), 0x08u);
}

TEST_F(KeyboardTest, ReadReturnsSpace) {
    init_with_ack();
    Wire.mock_queue_rx_byte(' ');
    slopos_keyboard_scan();
    EXPECT_EQ(slopos_keyboard_get_key(), 0x20u);
}

TEST_F(KeyboardTest, Ignores0xFFInvalidRead) {
    init_with_ack();
    Wire.mock_queue_rx_byte(0xFF);
    slopos_keyboard_scan();
    EXPECT_EQ(slopos_keyboard_get_key(), 0u);
    EXPECT_FALSE(slopos_keyboard_consume_event());
}

// ════════════════════════════════════════════════════════
// EVENT CONSUMPTION TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, HasNewEventIsOneShot) {
    init_with_ack();
    Wire.mock_queue_rx_byte('z');
    slopos_keyboard_scan();
    EXPECT_TRUE(slopos_keyboard_consume_event());
    EXPECT_FALSE(slopos_keyboard_consume_event());
    EXPECT_FALSE(slopos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyValuePersistsAfterEventConsumed) {
    init_with_ack();
    Wire.mock_queue_rx_byte('x');
    slopos_keyboard_scan();
    EXPECT_TRUE(slopos_keyboard_consume_event());
    EXPECT_EQ(slopos_keyboard_get_key(), 0x78u);
}

TEST_F(KeyboardTest, SubsequentScansWithoutKeyKeepLastValue) {
    init_with_ack();
    Wire.mock_queue_rx_byte('k');
    slopos_keyboard_scan();
    EXPECT_EQ(slopos_keyboard_get_key(), 'k');
    // Advance past poll interval so second scan executes (not throttled)
    arduino_mock::current_millis += 11;
    Wire.mock_queue_rx_byte(0x00);
    slopos_keyboard_scan();
    EXPECT_EQ(slopos_keyboard_get_key(), 'k');
    // New latching behavior: event persists until consume_key() is called.
    // MCU returning 0 does NOT clear has_new_event (was clearing it before PR #7).
    EXPECT_TRUE(slopos_keyboard_consume_event());
}

TEST_F(KeyboardTest, ConsumeKeyClearsAfterProcessing) {
    init_with_ack();
    Wire.mock_queue_rx_byte('k');
    slopos_keyboard_scan();
    EXPECT_TRUE(slopos_keyboard_consume_event());
    EXPECT_EQ(slopos_keyboard_get_key(), 'k');

    slopos_keyboard_consume_key();
    EXPECT_FALSE(slopos_keyboard_consume_event());
    EXPECT_EQ(slopos_keyboard_get_key(), 0);
}

// ════════════════════════════════════════════════════════
// BACKLIGHT TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, SetBrightnessSendsCorrectCommand) {
    init_with_ack();
    slopos_keyboard_set_brightness(200);
    EXPECT_EQ(Wire.mock_tx_len(), 2);
    EXPECT_EQ(Wire.mock_last_tx_addr(), 0x55u);
    EXPECT_EQ(Wire.mock_last_tx_data(0), 0x01u);
    EXPECT_EQ(Wire.mock_last_tx_data(1), 200u);
}

TEST_F(KeyboardTest, SetBrightnessZeroTurnsOff) {
    init_with_ack();
    slopos_keyboard_set_brightness(0);
    EXPECT_EQ(Wire.mock_last_tx_data(1), 0u);
}

TEST_F(KeyboardTest, SetDefaultBrightnessClampsToMinimum30) {
    init_with_ack();
    slopos_keyboard_set_default_brightness(5);
    EXPECT_EQ(Wire.mock_tx_len(), 2);
    EXPECT_EQ(Wire.mock_last_tx_data(1), 30u);
}

TEST_F(KeyboardTest, SetDefaultBrightnessNormalValue) {
    init_with_ack();
    slopos_keyboard_set_default_brightness(180);
    EXPECT_EQ(Wire.mock_last_tx_data(1), 180u);
}

// ════════════════════════════════════════════════════════
// MODIFIER / THROTTLE TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, ModifierStateDefaultsToFalse) {
    init_with_ack();
    EXPECT_FALSE(slopos_keyboard_is_shift());
    EXPECT_FALSE(slopos_keyboard_is_ctrl());
    EXPECT_FALSE(slopos_keyboard_is_alt());
}

TEST_F(KeyboardTest, ScanThrottledByPollInterval) {
    init_with_ack();
    Wire.mock_queue_rx_byte('a');
    slopos_keyboard_scan();
    EXPECT_TRUE(slopos_keyboard_consume_event());
    Wire.mock_queue_rx_byte('b');
    slopos_keyboard_scan();
    EXPECT_FALSE(slopos_keyboard_consume_event());
}

} // anonymous namespace
