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
#include <initializer_list>
#include <utility>

namespace {

class KeyboardTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
        Wire = TwoWire();
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
        // Advance clock past the 10ms poll interval
        arduino_mock::current_millis = 11;
    }

    void queue_raw(std::initializer_list<std::pair<uint8_t, uint8_t>> pressed) {
        uint8_t cols[5] = {0};
        for (auto key : pressed) {
            if (key.first < 5 && key.second < 7) {
                cols[key.first] |= (uint8_t)(1u << key.second);
            }
        }
        for (uint8_t col = 0; col < 5; col++) {
            Wire.mock_queue_rx_byte(cols[col]);
        }
    }

    void scan_raw(std::initializer_list<std::pair<uint8_t, uint8_t>> pressed) {
        arduino_mock::current_millis += 6;
        queue_raw(pressed);
        sigurdos_keyboard_scan();
    }

    void release_raw() {
        scan_raw({});
    }
};

// ════════════════════════════════════════════════════════
// INIT TESTS (must run first — static initialized flag)
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, InitFailsWhenKeyboardNotResponding) {
    EXPECT_FALSE(sigurdos_keyboard_init());
}

TEST_F(KeyboardTest, InitSendsDefaultBrightnessOnFirstSuccess) {
    Wire.mock_queue_rx_byte(0x00);
    EXPECT_TRUE(sigurdos_keyboard_init());

    // Init sends two commands:
    //   1. Brightness        (0x01 + prefs value)
    //   2. Default brightness (0x02 + prefs value)
    //   3. Raw mode switch   (0x03)
    // The mock only records the LAST transmission.
    EXPECT_EQ(Wire.mock_last_tx_addr(), 0x55u);
    // Last transmission should be raw-mode command
    EXPECT_EQ(Wire.mock_last_tx_data(0), 0x03u);
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

// ════════════════════════════════════════════════════════
// KEY READING TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, NoKeyReturnsZero) {
    init_with_ack();
    Wire.mock_queue_rx_byte(0x00);
    sigurdos_keyboard_scan();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0u);
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyPressReturnsAsciiValue) {
    init_with_ack();
    Wire.mock_queue_rx_byte('a');
    sigurdos_keyboard_scan();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x61u);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, ReadReturnsEnter) {
    init_with_ack();
    Wire.mock_queue_rx_byte(0x0D);
    sigurdos_keyboard_scan();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x0Du);
}

TEST_F(KeyboardTest, ReadReturnsBackspace) {
    init_with_ack();
    Wire.mock_queue_rx_byte(0x08);
    sigurdos_keyboard_scan();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x08u);
}

TEST_F(KeyboardTest, ReadReturnsSpace) {
    init_with_ack();
    Wire.mock_queue_rx_byte(' ');
    sigurdos_keyboard_scan();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x20u);
}

TEST_F(KeyboardTest, Ignores0xFFInvalidRead) {
    init_with_ack();
    Wire.mock_queue_rx_byte(0xFF);
    sigurdos_keyboard_scan();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0u);
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

// ════════════════════════════════════════════════════════
// EVENT CONSUMPTION TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, HasNewEventIsOneShot) {
    init_with_ack();
    Wire.mock_queue_rx_byte('z');
    sigurdos_keyboard_scan();
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyValuePersistsAfterEventConsumed) {
    init_with_ack();
    Wire.mock_queue_rx_byte('x');
    sigurdos_keyboard_scan();
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x78u);
}

TEST_F(KeyboardTest, SubsequentScansWithoutKeyKeepLastValue) {
    init_with_ack();
    Wire.mock_queue_rx_byte('k');
    sigurdos_keyboard_scan();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'k');
    // Advance past poll interval so second scan executes (not throttled)
    arduino_mock::current_millis += 11;
    Wire.mock_queue_rx_byte(0x00);
    sigurdos_keyboard_scan();
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'k');
    // New latching behavior: event persists until consume_key() is called.
    // MCU returning 0 does NOT clear has_new_event (was clearing it before PR #7).
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, ConsumeKeyClearsAfterProcessing) {
    init_with_ack();
    Wire.mock_queue_rx_byte('k');
    sigurdos_keyboard_scan();
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
    Wire.mock_queue_rx_byte('a');
    sigurdos_keyboard_scan();
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
// RAW MATRIX WRAPPER TESTS
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, RawMatrixMapsNormalLetters) {
    init_with_ack();
    scan_raw({{0, 0}}); // q

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'q');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsShiftedLetters) {
    init_with_ack();
    scan_raw({{1, 6}, {3, 0}}); // left shift + u

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'U');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsSymLayerCharacters) {
    init_with_ack();
    scan_raw({{0, 2}, {1, 0}}); // sym + e

    EXPECT_EQ(sigurdos_keyboard_get_key(), '2');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsShiftSymLayerCharacters) {
    init_with_ack();
    scan_raw({{0, 2}, {1, 6}, {2, 6}}); // sym + shift + f

    EXPECT_EQ(sigurdos_keyboard_get_key(), '^');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixKeepsMicKeyUsableAsZeroOnSymLayer) {
    init_with_ack();
    scan_raw({{0, 2}, {0, 6}}); // sym + mic

    EXPECT_EQ(sigurdos_keyboard_get_key(), '0');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsAltCToCharacterPicker) {
    init_with_ack();
    scan_raw({{0, 4}, {2, 5}}); // alt + c

    EXPECT_EQ(sigurdos_keyboard_get_key(), (int)sigurdos_keyboard_char_picker_key('c'));
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsShiftAltCToUpperCharacterPicker) {
    init_with_ack();
    scan_raw({{0, 4}, {1, 6}, {2, 5}}); // alt + shift + c

    EXPECT_EQ(sigurdos_keyboard_get_key(), (int)sigurdos_keyboard_char_picker_key('C'));
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsAltSpaceToChannelShortcut) {
    init_with_ack();
    scan_raw({{0, 4}, {0, 5}}); // alt + space

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x0C);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixLeavesAltBForKeyboardBacklight) {
    init_with_ack();
    scan_raw({{0, 4}, {3, 4}}); // alt + b

    EXPECT_FALSE(sigurdos_keyboard_has_event());
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsEnterAndBackspace) {
    init_with_ack();
    scan_raw({{3, 3}}); // enter
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x0D);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    release_raw();

    scan_raw({{4, 3}}); // backspace
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x08);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsDedicatedDollarKey) {
    init_with_ack();
    scan_raw({{4, 4}}); // $

    EXPECT_EQ(sigurdos_keyboard_get_key(), '$');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsMicLayerToAccentedLatin) {
    init_with_ack();
    scan_raw({{0, 6}, {3, 0}}); // mic + u

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00FC); // u diaeresis
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsShiftMicLayerToUpperAccentedLatin) {
    init_with_ack();
    scan_raw({{0, 6}, {1, 6}, {3, 0}}); // mic + shift + u

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00DC); // U diaeresis
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixCoversFrenchAccentExamples) {
    init_with_ack();
    scan_raw({{0, 6}, {2, 2}}); // mic + t = e circumflex
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00EA);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    release_raw();

    scan_raw({{0, 6}, {2, 5}}); // mic + c = c cedilla
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00E7);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    release_raw();

    scan_raw({{0, 6}, {0, 0}}); // mic + q = a grave
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00E0);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixSupportsOneShotMicLayer) {
    init_with_ack();
    scan_raw({{0, 6}}); // tap mic
    EXPECT_FALSE(sigurdos_keyboard_has_event());
    release_raw();

    scan_raw({{3, 0}}); // next u uses one-shot mic layer
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00FC);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixSupportsOneShotAltLayer) {
    init_with_ack();
    scan_raw({{0, 4}}); // tap alt
    EXPECT_FALSE(sigurdos_keyboard_has_event());
    release_raw();

    scan_raw({{3, 0}}); // next u opens one-shot alt character picker
    EXPECT_EQ(sigurdos_keyboard_get_key(), (int)sigurdos_keyboard_char_picker_key('u'));
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
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

TEST_F(KeyboardTest, RawMatrixSupportsOneShotSymLayer) {
    init_with_ack();
    scan_raw({{0, 2}}); // tap sym
    EXPECT_FALSE(sigurdos_keyboard_has_event());
    release_raw();

    scan_raw({{1, 0}}); // next e uses one-shot sym layer
    EXPECT_EQ(sigurdos_keyboard_get_key(), '2');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, InjectCodepointQueuesUnicodeValue) {
    init_with_ack();
    sigurdos_keyboard_inject_codepoint(0x20AC);

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x20AC);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

} // anonymous namespace
