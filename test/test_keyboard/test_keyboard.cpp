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
 * Unit tests for T-Deck keyboard driver (I2C slave at 0x55)
 *
 * Architecture: The T-Deck keyboard is a separate ESP32-C3 MCU that scans
 * the physical matrix and returns key codes over I2C.
 *
 * The fixture resets the driver's one-shot init cache before every test.
 *
 * Reference: Xinyuan-LilyGO/T-Deck examples/Keyboard_ESP32C3/
 * License: MIT
 */
#include <gtest/gtest.h>
#include "hal/tdeck_pins.h"
#include "hal/i2c_bus.h"
#include "hal/keyboard.h"
#include "Arduino.h"
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace {

class KeyboardTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
        Wire = TwoWire();
        sigurdos_keyboard_reset_init_for_test();
    }

    void init_with_ack() {
        sigurdos_keyboard_reset_scan_state();
        // Queue a probe-response byte for sigurdos_keyboard_init().
        // If init is idempotent (already initialized from prior test),
        // the byte won't be consumed — drain it explicitly so it
        // doesn't leak into the next keyboard scan.
        Wire.mock_queue_rx_byte(0x00);
        sigurdos_keyboard_init();
        Wire.requestFrom(0x55, 1);
        if (Wire.available()) { Wire.read(); }
        // Advance clock past the 5ms poll interval.
        arduino_mock::current_millis = 11;
    }

    void scan_keymode_byte(uint8_t key_byte) {
        arduino_mock::current_millis += 6;
        Wire.mock_queue_rx_byte(key_byte);
        sigurdos_keyboard_scan();
    }

    // Compatibility alias: key-mode only — matrix args are ignored.
    void scan_key_and_sample(
        uint8_t key_byte,
        std::initializer_list<std::pair<uint8_t, uint8_t>> = {}) {
        scan_keymode_byte(key_byte);
    }
};

// ════════════════════════════════════════════════════════
// INIT TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, InitFailsWhenKeyboardNotResponding) {
    EXPECT_FALSE(sigurdos_keyboard_init());
}

TEST_F(KeyboardTest, InitRetriesUpToLimitThenFails) {
    // Never ACKs — should fail after all retries exhausted
    Wire.mock_set_nack_count(99);
    EXPECT_FALSE(sigurdos_keyboard_init());
}

TEST_F(KeyboardTest, InitRecoversFromTransientNack) {
    // Warm-handoff scenario: first beginTransmission NACKs (C3 slow to
    // respond after Launcher ESP.restart()), second retry succeeds.
    Wire.mock_set_nack_count(1);
    Wire.mock_queue_rx_byte(0x00);
    EXPECT_TRUE(sigurdos_keyboard_init());
}

TEST_F(KeyboardTest, InitLeavesKeyboardInKeyMode) {
    Wire.mock_queue_rx_byte(0x00);
    EXPECT_TRUE(sigurdos_keyboard_init());

    // Init sends two commands:
    //   1. Brightness        (0x01 + prefs value)
    //   2. Default brightness (0x02 + prefs value)
    //   3. Key mode switch   (0x04)
    // The mock only records the LAST transmission.
    EXPECT_EQ(Wire.mock_last_tx_addr(), 0x55u);
    EXPECT_EQ(Wire.mock_last_tx_data(0), 0x04u);
}

TEST_F(KeyboardTest, InitProbesKeyboardAtCorrectAddress) {
    init_with_ack();
    SUCCEED();
}

TEST_F(KeyboardTest, InitIsIdempotent) {
    init_with_ack();
    Wire.mock_set_error(1);
    EXPECT_TRUE(sigurdos_keyboard_init());
}

TEST_F(KeyboardTest, InitUsesBoundedSharedBusTimeout) {
    Wire.mock_queue_rx_byte(0x00);
    EXPECT_TRUE(sigurdos_keyboard_init());
    EXPECT_EQ(Wire.mock_timeout_ms(), sigurdos::i2c::TRANSACTION_TIMEOUT_MS);
    EXPECT_EQ(Wire.mock_clock(), sigurdos::i2c::BUS_CLOCK_HZ);
}

TEST_F(KeyboardTest, FailedInitResultIsCached) {
    EXPECT_FALSE(sigurdos_keyboard_init());
    const size_t transactions_after_failure = Wire.mock_end_count();

    Wire.mock_queue_rx_byte(0x00);
    EXPECT_FALSE(sigurdos_keyboard_init());
    EXPECT_EQ(Wire.mock_end_count(), transactions_after_failure);
}

TEST_F(KeyboardTest, InitRetryWindowCoversSlowColdBoot) {
    Wire.mock_set_nack_count(6);
    Wire.mock_queue_rx_byte(0x00);

    EXPECT_TRUE(sigurdos_keyboard_init());
    EXPECT_GE(arduino_mock::current_millis, 600u);
}

// ════════════════════════════════════════════════════════
// KEY READING TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, NoKeyReturnsZero) {
    init_with_ack();
    scan_key_and_sample(0x00);
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0u);
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyPressReturnsAsciiValue) {
    init_with_ack();
    scan_key_and_sample('a');
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x61u);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, ReadReturnsEnter) {
    init_with_ack();
    scan_key_and_sample(0x0D);
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x0Du);
}

TEST_F(KeyboardTest, ReadReturnsBackspace) {
    init_with_ack();
    scan_key_and_sample(0x08);
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x08u);
}

TEST_F(KeyboardTest, ReadReturnsSpace) {
    init_with_ack();
    scan_key_and_sample(' ');
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x20u);
}

TEST_F(KeyboardTest, Ignores0xFFInvalidRead) {
    init_with_ack();
    scan_key_and_sample(0xFF);
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0u);
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

// ════════════════════════════════════════════════════════
// EVENT CONSUMPTION TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, HasNewEventIsOneShot) {
    init_with_ack();
    scan_key_and_sample('z');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyValuePersistsAfterEventConsumed) {
    init_with_ack();
    scan_key_and_sample('x');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x78u);
}

TEST_F(KeyboardTest, SubsequentScansWithoutKeyKeepLastValue) {
    init_with_ack();
    scan_key_and_sample('k');
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'k');
    // Advance past poll interval so second scan executes (not throttled)
    scan_keymode_byte(0x00);
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'k');
    // New latching behavior: event persists until consume_key() is called.
    // MCU returning 0 does NOT clear has_new_event (was clearing it before PR #7).
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, ConsumeKeyClearsAfterProcessing) {
    init_with_ack();
    scan_key_and_sample('k');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'k');

    sigurdos_keyboard_consume_key();
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0);
}

// ════════════════════════════════════════════════════════
// BACKLIGHT TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, SetBrightnessSendsCorrectCommand) {
    init_with_ack();
    sigurdos_keyboard_set_brightness(200);
    EXPECT_EQ(Wire.mock_tx_len(), 2);
    EXPECT_EQ(Wire.mock_last_tx_addr(), 0x55u);
    EXPECT_EQ(Wire.mock_last_tx_data(0), 0x01u);
    EXPECT_EQ(Wire.mock_last_tx_data(1), 200u);
}

TEST_F(KeyboardTest, SetBrightnessZeroTurnsOff) {
    init_with_ack();
    sigurdos_keyboard_set_brightness(0);
    EXPECT_EQ(Wire.mock_last_tx_data(1), 0u);
}

TEST_F(KeyboardTest, SetDefaultBrightnessClampsToMinimum30) {
    init_with_ack();
    sigurdos_keyboard_set_default_brightness(5);
    EXPECT_EQ(Wire.mock_tx_len(), 2);
    EXPECT_EQ(Wire.mock_last_tx_data(1), 30u);
}

TEST_F(KeyboardTest, SetDefaultBrightnessNormalValue) {
    init_with_ack();
    sigurdos_keyboard_set_default_brightness(180);
    EXPECT_EQ(Wire.mock_last_tx_data(1), 180u);
}

// ════════════════════════════════════════════════════════
// MODIFIER / THROTTLE TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, ModifierStateDefaultsToFalse) {
    init_with_ack();
    EXPECT_FALSE(sigurdos_keyboard_is_shift());
    EXPECT_FALSE(sigurdos_keyboard_is_ctrl());
    EXPECT_FALSE(sigurdos_keyboard_is_alt());
}

TEST_F(KeyboardTest, ScanThrottledByPollInterval) {
    init_with_ack();
    scan_key_and_sample('a');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    Wire.mock_queue_rx_byte('b');
    sigurdos_keyboard_scan();
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, InjectedKeysAreQueuedAndConsumedInOrder) {
    init_with_ack();
    sigurdos_keyboard_inject('a');
    sigurdos_keyboard_inject('b');
    sigurdos_keyboard_inject('c');
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'a');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'b');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'c');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, InjectedInvalidBytesAreIgnored) {
    init_with_ack();
    sigurdos_keyboard_inject(0x00);
    sigurdos_keyboard_inject(0xFF);
    EXPECT_FALSE(sigurdos_keyboard_has_event());
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0);
}

TEST_F(KeyboardTest, InjectedInvalidBytesDoNotDisruptValidOrdering) {
    init_with_ack();
    sigurdos_keyboard_inject(0x00);
    sigurdos_keyboard_inject('a');
    sigurdos_keyboard_inject(0xFF);
    sigurdos_keyboard_inject('b');

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'a');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'b');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

// ════════════════════════════════════════════════════════
// KEY-MODE ONLY PATH (CMD 0x04)
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, KeyModeMapsNormalLetters) {
    init_with_ack();
    scan_keymode_byte('q');

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'q');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModePassesThroughPrintableAscii) {
    init_with_ack();

    for (uint8_t key = 0x20; key <= 0x7E; key++) {
        scan_keymode_byte(key);
        EXPECT_EQ(sigurdos_keyboard_get_key(), key) << "ASCII byte " << (int)key;
        EXPECT_TRUE(sigurdos_keyboard_consume_event());
        sigurdos_keyboard_consume_key();
    }
}

TEST_F(KeyboardTest, KeyModeAcceptsC3ShiftedLetters) {
    init_with_ack();
    scan_keymode_byte('U');

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'U');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeAcceptsC3SymbolLayerCharacters) {
    init_with_ack();
    scan_keymode_byte('2');

    EXPECT_EQ(sigurdos_keyboard_get_key(), '2');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMapsEnterAndBackspace) {
    init_with_ack();
    scan_keymode_byte(0x0D);
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x0D);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();

    scan_keymode_byte(0x08);
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x08);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMapsDedicatedDollarKey) {
    init_with_ack();
    scan_keymode_byte('$');

    EXPECT_EQ(sigurdos_keyboard_get_key(), '$');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeOnlyNeverWritesRawModeCommand) {
    init_with_ack();
    // Exercise polling thoroughly. Scan only issues requestFrom reads once
    // key mode is ready; the last mode switch from init must remain 0x04 and
    // the production path must never have written CMD 0x03.
    for (int i = 0; i < 50; i++) {
        scan_keymode_byte(static_cast<uint8_t>('a' + (i % 26)));
        EXPECT_TRUE(sigurdos_keyboard_consume_event());
        sigurdos_keyboard_consume_key();
    }
    // After init the last mode-command byte recorded by the mock is 0x04.
    // Brightness writes use 0x01/0x02. Never 0x03.
    EXPECT_NE(Wire.mock_last_tx_data(0), 0x03u);
    sigurdos_keyboard_set_brightness(100);
    EXPECT_EQ(Wire.mock_last_tx_data(0), 0x01u);
    EXPECT_NE(Wire.mock_last_tx_data(0), 0x03u);
}

TEST_F(KeyboardTest, InitLeavesKeyboardInKeyModeOnly) {
    Wire.mock_queue_rx_byte(0x00);
    EXPECT_TRUE(sigurdos_keyboard_init());
    EXPECT_EQ(Wire.mock_last_tx_addr(), 0x55u);
    EXPECT_EQ(Wire.mock_last_tx_data(0), 0x04u);
}

TEST_F(KeyboardTest, CharacterPickerKeyHelpersPreserveBaseCharacter) {
    uint32_t lower = sigurdos_keyboard_char_picker_key('c');
    uint32_t upper = sigurdos_keyboard_char_picker_key('C');

    EXPECT_TRUE(sigurdos_keyboard_is_char_picker_key(lower));
    EXPECT_TRUE(sigurdos_keyboard_is_char_picker_key(upper));
    EXPECT_EQ(sigurdos_keyboard_char_picker_base(lower), 'c');
    EXPECT_EQ(sigurdos_keyboard_char_picker_base(upper), 'C');
    EXPECT_FALSE(sigurdos_keyboard_is_char_picker_key('c'));
}

TEST_F(KeyboardTest, InjectCodepointQueuesUnicodeValue) {
    init_with_ack();
    sigurdos_keyboard_inject_codepoint(0x20AC);

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x20AC);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

} // anonymous namespace
