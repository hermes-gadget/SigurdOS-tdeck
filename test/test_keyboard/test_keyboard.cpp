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
        // Advance clock past the 5ms poll interval but keep the first raw
        // modifier sample in the future.
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

    void sample_raw(std::initializer_list<std::pair<uint8_t, uint8_t>> pressed) {
        arduino_mock::current_millis += 21;
        queue_raw(pressed);
        sigurdos_keyboard_scan();
    }

    void sample_legacy_single_byte(uint8_t key_byte) {
        arduino_mock::current_millis += 21;
        Wire.mock_queue_rx_byte(key_byte);
        sigurdos_keyboard_scan();
    }

    void release_raw() {
        sample_raw({});
    }

    void scan_keymode_byte(uint8_t key_byte) {
        arduino_mock::current_millis += 6;
        Wire.mock_queue_rx_byte(key_byte);
        sigurdos_keyboard_scan();
    }

    void scan_key_and_sample(
        uint8_t key_byte,
        std::initializer_list<std::pair<uint8_t, uint8_t>> pressed = {}) {
        arduino_mock::current_millis += 6;
        Wire.mock_queue_rx_byte(key_byte);
        queue_raw(pressed);
        sigurdos_keyboard_scan();
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
// KEY-MODE PRIMARY PATH + RAW MODIFIER COMPATIBILITY
// ════════════════════════════════════════════════════════

TEST_F(KeyboardTest, KeyModeMapsNormalLetters) {
    init_with_ack();
    scan_key_and_sample('q', {{0, 0}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'q');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeDoesNotRemapAsciiFromVariantMatrixPosition) {
    init_with_ack();
    // Model variants may report a different physical matrix position for the
    // same logical key. The C3 byte remains authoritative for normal typing.
    scan_key_and_sample('q', {{3, 0}}); // standard matrix position is U

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'q');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModePassesThroughPrintableAsciiWithoutMatrixLookup) {
    init_with_ack();

    for (uint8_t key = 0x20; key <= 0x7E; key++) {
        scan_key_and_sample(key);
        EXPECT_EQ(sigurdos_keyboard_get_key(), key) << "ASCII byte " << (int)key;
        EXPECT_TRUE(sigurdos_keyboard_consume_event());
        sigurdos_keyboard_consume_key();
    }
}

TEST_F(KeyboardTest, KeyModeAcceptsC3ShiftedLetters) {
    init_with_ack();
    scan_key_and_sample('U', {{1, 6}, {3, 0}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), 'U');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeAcceptsC3SymbolLayerCharacters) {
    init_with_ack();
    scan_key_and_sample('2', {{0, 2}, {1, 0}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), '2');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleRepairsC3ShiftSymbolEncoding) {
    init_with_ack();
    // Published C3 firmware subtracts 32 from the unshifted symbol. The raw
    // sample identifies F so the host restores the matrix driver's '^'.
    scan_key_and_sample(0x16, {{0, 2}, {1, 6}, {2, 6}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), '^');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleKeepsMicUsableAsZeroOnSymLayer) {
    init_with_ack();
    scan_key_and_sample('0', {{0, 2}, {0, 6}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), '0');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleRepairsShiftSymMicEncoding) {
    init_with_ack();
    scan_key_and_sample(0x10, {{0, 2}, {0, 6}, {1, 6}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), ')');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleMapsAltCToCharacterPicker) {
    init_with_ack();
    // The C3 encodes held Alt+C as 0x0C; raw modifier state disambiguates it
    // from the fallback channel-menu event.
    scan_key_and_sample(0x0C, {{0, 4}, {2, 5}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), (int)sigurdos_keyboard_char_picker_key('c'));
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleMapsShiftAltCToUpperCharacterPicker) {
    init_with_ack();
    scan_key_and_sample(0x0C, {{0, 4}, {1, 6}, {2, 5}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), (int)sigurdos_keyboard_char_picker_key('C'));
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleMapsAltSpaceToChannelShortcut) {
    init_with_ack();
    scan_key_and_sample(' ', {{0, 4}, {0, 5}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x0C);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, C3RetainsAltBBacklightOwnership) {
    init_with_ack();
    sample_raw({{0, 4}, {3, 4}}); // C3 deliberately emits no key byte

    EXPECT_FALSE(sigurdos_keyboard_has_event());
    EXPECT_FALSE(sigurdos_keyboard_consume_event());

    release_raw();
    scan_key_and_sample('u', {{3, 0}});
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'u');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMapsEnterAndBackspace) {
    init_with_ack();
    scan_key_and_sample(0x0D, {{3, 3}});
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x0D);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    release_raw();

    scan_key_and_sample(0x08, {{4, 3}});
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x08);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, KeyModeMapsDedicatedDollarKey) {
    init_with_ack();
    scan_key_and_sample('$', {{4, 4}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), '$');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleMapsMicLayerToAccentedLatin) {
    init_with_ack();
    scan_key_and_sample('u', {{0, 6}, {3, 0}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00FC); // u diaeresis
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleMapsShiftMicLayerToUpperAccentedLatin) {
    init_with_ack();
    scan_key_and_sample('U', {{0, 6}, {1, 6}, {3, 0}});

    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00DC); // U diaeresis
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleCoversFrenchAccentExamples) {
    init_with_ack();
    scan_key_and_sample('t', {{0, 6}, {2, 2}});
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00EA);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    release_raw();

    scan_key_and_sample('c', {{0, 6}, {2, 5}});
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00E7);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    release_raw();

    scan_key_and_sample('q', {{0, 6}, {0, 0}});
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00E0);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleSupportsOneShotMicLayer) {
    init_with_ack();
    sample_raw({{0, 6}});
    EXPECT_FALSE(sigurdos_keyboard_has_event());
    release_raw();

    scan_key_and_sample('u', {{3, 0}});
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00FC);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleSupportsOneShotAltLayer) {
    init_with_ack();
    sample_raw({{0, 4}});
    EXPECT_FALSE(sigurdos_keyboard_has_event());
    release_raw();

    scan_key_and_sample('u', {{3, 0}});
    EXPECT_EQ(sigurdos_keyboard_get_key(), (int)sigurdos_keyboard_char_picker_key('u'));
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, ModifierTransformDoesNotDuplicateBaseCharacter) {
    init_with_ack();
    scan_key_and_sample('u', {{0, 6}, {3, 0}});
    EXPECT_EQ(sigurdos_keyboard_get_key(), 0x00FC);
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
    sigurdos_keyboard_consume_key();
    EXPECT_FALSE(sigurdos_keyboard_has_event());
}

TEST_F(KeyboardTest, LegacyC3WithoutRawModeFallsBackToImmediateKeyMode) {
    init_with_ack();
    sample_legacy_single_byte(0x00);
    sample_legacy_single_byte(0x00);
    sample_legacy_single_byte(0x00);

    scan_keymode_byte('x');
    EXPECT_EQ(sigurdos_keyboard_get_key(), 'x');
    EXPECT_TRUE(sigurdos_keyboard_consume_event());
}

TEST_F(KeyboardTest, RawSampleAlwaysRestoresKeyMode) {
    init_with_ack();
    sample_raw({});

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

TEST_F(KeyboardTest, RawSampleSupportsOneShotSymLayer) {
    init_with_ack();
    sample_raw({{0, 2}});
    EXPECT_FALSE(sigurdos_keyboard_has_event());
    release_raw();

    scan_key_and_sample('e', {{1, 0}});
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
