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
#include "hal/keyboard_layouts.h"
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
        // Advance clock past the 10ms poll interval
        arduino_mock::current_millis = 11;
    }

    // Enter raw matrix mode for testing the raw-matrix path.
    // Must be called AFTER init_with_ack() — sets raw_mode_active
    // so scan() reads 5-byte matrices instead of 1-byte key-mode bytes.
    void enter_raw_mode() {
        sigurdos_keyboard_enter_raw_mode_for_test();
        arduino_mock::current_millis += 6;
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

    // Queue a single key-mode byte and scan (primary path after Phase 1).
    void scan_keymode_byte(uint8_t key_byte) {
        arduino_mock::current_millis += 6;
        Wire.mock_queue_rx_byte(key_byte);
        sigurdos_keyboard_scan();
    }

    void release_raw() {
        scan_raw({});
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

TEST_F(KeyboardTest, InitSendsKeyModeOnFirstSuccess) {
    Wire.mock_queue_rx_byte(0x00);
    EXPECT_TRUE(sigurdos_keyboard_init());

    // Init sends three commands:
    //   1. Brightness        (0x01 + prefs value)
    //   2. Default brightness (0x02 + prefs value)
    //   3. Key mode switch   (0x04)
    // The mock only records the LAST transmission.
    EXPECT_EQ(Wire.mock_last_tx_addr(), 0x55u);
    // Last transmission should be key-mode command (Phase 1: key mode is primary)
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
    enter_raw_mode();
    scan_raw({{0, 0}}); // q

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'q');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsShiftedLetters) {
    init_with_ack();
    enter_raw_mode();
    scan_raw({{1, 6}, {3, 0}}); // left shift + u

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'U');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsSymLayerCharacters) {
    init_with_ack();
    enter_raw_mode();
    scan_raw({{0, 2}, {1, 0}}); // sym + e

    EXPECT_EQ(sigurdos_keyboard_get_key(), '2');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsShiftSymLayerCharacters) {
    init_with_ack();
    enter_raw_mode();
    scan_raw({{0, 2}, {1, 6}, {2, 6}}); // sym + shift + f

    EXPECT_EQ(sigurdos_keyboard_get_key(), '^');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixKeepsMicKeyUsableAsZeroOnSymLayer) {
    init_with_ack();
    enter_raw_mode();
    scan_raw({{0, 2}, {0, 6}}); // sym + mic

    EXPECT_EQ(sigurdos_keyboard_get_key(), '0');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsAltCToCharacterPicker) {
    init_with_ack();
    enter_raw_mode();
    scan_raw({{0, 4}, {2, 5}}); // alt + c

    EXPECT_EQ(sigurdos_keyboard_get_key(), (int)sigurdos_keyboard_char_picker_key('c'));
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsShiftAltCToUpperCharacterPicker) {
    init_with_ack();
    enter_raw_mode();
    scan_raw({{0, 4}, {1, 6}, {2, 5}}); // alt + shift + c

    EXPECT_EQ(sigurdos_keyboard_get_key(), (int)sigurdos_keyboard_char_picker_key('C'));
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsAltSpaceToChannelShortcut) {
    init_with_ack();
    enter_raw_mode();
    scan_raw({{0, 4}, {0, 5}}); // alt + space

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x0C);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixLeavesAltBForKeyboardBacklight) {
    init_with_ack();
    enter_raw_mode();
    scan_raw({{0, 4}, {3, 4}}); // alt + b

    EXPECT_FALSE(sigurdos_keyboard_has_event());
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsEnterAndBackspace) {
    init_with_ack();
    enter_raw_mode();
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
    enter_raw_mode();
    scan_raw({{4, 4}}); // $

    EXPECT_EQ(sigurdos_keyboard_get_key(), '$');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsMicLayerToAccentedLatin) {
    init_with_ack();
    enter_raw_mode();
    scan_raw({{0, 6}, {3, 0}}); // mic + u

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00FC); // u diaeresis
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixMapsShiftMicLayerToUpperAccentedLatin) {
    init_with_ack();
    enter_raw_mode();
    scan_raw({{0, 6}, {1, 6}, {3, 0}}); // mic + shift + u

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00DC); // U diaeresis
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixCoversFrenchAccentExamples) {
    init_with_ack();
    enter_raw_mode();
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
    enter_raw_mode();
    scan_raw({{0, 6}}); // tap mic
    EXPECT_FALSE(sigurdos_keyboard_has_event());
    release_raw();

    scan_raw({{3, 0}}); // next u uses one-shot mic layer
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00FC);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawMatrixSupportsOneShotAltLayer) {
    init_with_ack();
    enter_raw_mode();
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
    enter_raw_mode();
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

// ════════════════════════════════════════════════════════
// KEY-MODE TESTS (Phase 1 — primary path)
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, KeyModeMapsLowercaseLetter) {
    init_with_ack();
    scan_keymode_byte('k');

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'k');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMapsUppercaseLetter) {
    init_with_ack();
    scan_keymode_byte('Z');

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'Z');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMapsDigit) {
    init_with_ack();
    scan_keymode_byte('5');

    EXPECT_EQ(sigurdos_keyboard_get_key(), '5');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMapsPunctuation) {
    init_with_ack();
    scan_keymode_byte('.');

    EXPECT_EQ(sigurdos_keyboard_get_key(), '.');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMapsEnter) {
    init_with_ack();
    scan_keymode_byte(0x0D);

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x0D);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMapsBackspace) {
    init_with_ack();
    scan_keymode_byte(0x08);

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x08);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMapsSpace) {
    init_with_ack();
    scan_keymode_byte(' ');

    EXPECT_EQ(sigurdos_keyboard_get_key(), ' ');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeTracksShiftFromUppercase) {
    init_with_ack();
    EXPECT_FALSE(sigurdos_keyboard_is_shift());

    scan_keymode_byte('A');
    EXPECT_TRUE(sigurdos_keyboard_is_shift());

    scan_keymode_byte('b');
    EXPECT_FALSE(sigurdos_keyboard_is_shift());
}

TEST_F(KeyboardTest, KeyModeTracksAltFromLegacyAltC) {
    init_with_ack();
    EXPECT_FALSE(sigurdos_keyboard_is_alt());

    scan_keymode_byte(0x0C);   // Alt+C in legacy C3 key-mode firmware
    EXPECT_TRUE(sigurdos_keyboard_is_alt());
}

TEST_F(KeyboardTest, KeyModeIgnoresZeroByte) {
    init_with_ack();
    scan_keymode_byte(0x00);

    EXPECT_FALSE(sigurdos_keyboard_has_event());
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeIgnores0xFF) {
    init_with_ack();
    scan_keymode_byte(0xFF);

    EXPECT_FALSE(sigurdos_keyboard_has_event());
    EXPECT_FALSE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMultipleKeysInSequence) {
    init_with_ack();

    scan_keymode_byte('h');
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'h');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();

    scan_keymode_byte('i');
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'i');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();

    scan_keymode_byte('!');
    EXPECT_EQ(sigurdos_keyboard_get_key(), '!');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeIsDefaultAfterInit) {
    // Verify key mode is the default after init (raw_mode_active == false).
    // In key mode, scan() reads 1 byte. We queue a valid byte and it should
    // be enqueued, not interpreted as a raw matrix column.
    init_with_ack();

    // Queue a single byte — in raw mode this would be a partial read and
    // would NOT enqueue. In key mode it should enqueue directly.
    scan_keymode_byte('X');
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'X');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

// ════════════════════════════════════════════════════════
// LAYOUT MAPPING TESTS (Phase 2)
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, LayoutNameReturnsTwoLetterCode) {
    EXPECT_STREQ(keyboardLayoutName(KeyboardLayoutId::EN), "EN");
    EXPECT_STREQ(keyboardLayoutName(KeyboardLayoutId::BG), "BG");
    EXPECT_STREQ(keyboardLayoutName(KeyboardLayoutId::RU), "RU");
    EXPECT_STREQ(keyboardLayoutName(KeyboardLayoutId::FR), "FR");
    EXPECT_STREQ(keyboardLayoutName(KeyboardLayoutId::DE), "DE");
}

TEST_F(KeyboardTest, LayoutDefaultIsEnglish) {
    EXPECT_EQ(keyboardLayoutsGetActive(), KeyboardLayoutId::EN);
}

TEST_F(KeyboardTest, LayoutSetAndGetActive) {
    keyboardLayoutsSetActive(KeyboardLayoutId::BG);
    EXPECT_EQ(keyboardLayoutsGetActive(), KeyboardLayoutId::BG);
    keyboardLayoutsSetActive(KeyboardLayoutId::EN);
    EXPECT_EQ(keyboardLayoutsGetActive(), KeyboardLayoutId::EN);
}

TEST_F(KeyboardTest, LayoutSetInvalidClamps) {
    keyboardLayoutsSetActive(static_cast<KeyboardLayoutId>(99));
    EXPECT_EQ(keyboardLayoutsGetActive(), KeyboardLayoutId::EN);  // unchanged
}

TEST_F(KeyboardTest, LayoutEnglishIsPassThrough) {
    // EN layout has no mapping tables — all keys pass through
    const char* m = keyboardLayoutMapHwKey(KeyboardLayoutId::EN, 'a', false);
    EXPECT_EQ(m, nullptr);
    m = keyboardLayoutMapHwKey(KeyboardLayoutId::EN, 'A', true);
    EXPECT_EQ(m, nullptr);
}

TEST_F(KeyboardTest, LayoutBulgarianLowercase) {
    const char* m = keyboardLayoutMapHwKey(KeyboardLayoutId::BG, 'a', false);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "а");  // Cyrillic a
}

TEST_F(KeyboardTest, LayoutBulgarianUppercase) {
    const char* m = keyboardLayoutMapHwKey(KeyboardLayoutId::BG, 'A', true);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "А");  // Cyrillic A
}

TEST_F(KeyboardTest, LayoutBulgarianDigitMapping) {
    const char* m = keyboardLayoutMapHwKey(KeyboardLayoutId::BG, '1', false);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "ш");  // sha on digit 1
}

TEST_F(KeyboardTest, LayoutFrenchAzertyRemap) {
    // On a US-QWERTY physical keyboard, pressing 'a' in French layout
    // should produce 'q' (AZERTY swap)
    const char* m = keyboardLayoutMapHwKey(KeyboardLayoutId::FR, 'a', false);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "q");

    m = keyboardLayoutMapHwKey(KeyboardLayoutId::FR, 'q', false);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "a");
}

TEST_F(KeyboardTest, LayoutGermanQwertzRemap) {
    const char* m = keyboardLayoutMapHwKey(KeyboardLayoutId::DE, 'y', false);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "z");

    m = keyboardLayoutMapHwKey(KeyboardLayoutId::DE, 'z', false);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "y");
}

TEST_F(KeyboardTest, LayoutRussianCoversAll33LettersViaKeysAndDigits) {
    // Spot-check a few Russian phonetic mappings
    const char* m = keyboardLayoutMapHwKey(KeyboardLayoutId::RU, 'a', false);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "а");

    m = keyboardLayoutMapHwKey(KeyboardLayoutId::RU, 's', false);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "с");

    // Digit 1 → ч
    m = keyboardLayoutMapHwKey(KeyboardLayoutId::RU, '1', false);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "ч");

    // Digit 6 → ю
    m = keyboardLayoutMapHwKey(KeyboardLayoutId::RU, '6', false);
    ASSERT_NE(m, nullptr);
    EXPECT_STREQ(m, "ю");
}

TEST_F(KeyboardTest, LayoutNonAlphaKeysPassThrough) {
    // Space, punctuation pass through unchanged
    const char* m = keyboardLayoutMapHwKey(KeyboardLayoutId::BG, ' ', false);
    EXPECT_EQ(m, nullptr);

    m = keyboardLayoutMapHwKey(KeyboardLayoutId::BG, '.', false);
    EXPECT_EQ(m, nullptr);
}

} // anonymous namespace
