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

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "hal/keyboard.h"
#include "hal/trackball.h"

namespace {

TEST(InputContractTest, TrackballEventUsesByteStorage) {
    using underlying = std::underlying_type<SigurdOSTrackballEvent>::type;
    EXPECT_TRUE((std::is_same<underlying, uint8_t>::value));
}

TEST(InputContractTest, TrackballEventValuesStayStableForUiDispatch) {
    EXPECT_EQ(static_cast<uint8_t>(SigurdOSTrackballEvent::None), 0u);
    EXPECT_EQ(static_cast<uint8_t>(SigurdOSTrackballEvent::Up), 1u);
    EXPECT_EQ(static_cast<uint8_t>(SigurdOSTrackballEvent::Down), 2u);
    EXPECT_EQ(static_cast<uint8_t>(SigurdOSTrackballEvent::Left), 3u);
    EXPECT_EQ(static_cast<uint8_t>(SigurdOSTrackballEvent::Right), 4u);
    EXPECT_EQ(static_cast<uint8_t>(SigurdOSTrackballEvent::Click), 5u);
}

TEST(InputContractTest, KeyboardRemoteHookSignaturesStayStable) {
    using init_fn = bool (*)();
    using scan_fn = void (*)();
    using key_fn = int (*)();
    using event_fn = bool (*)();
    using brightness_fn = void (*)(uint8_t);
    using state_fn = bool (*)();
    using inject_fn = void (*)(uint8_t);
    using inject_codepoint_fn = void (*)(uint32_t);

    (void)static_cast<init_fn>(sigurdos_keyboard_init);
    (void)static_cast<scan_fn>(sigurdos_keyboard_scan);
    (void)static_cast<key_fn>(sigurdos_keyboard_get_key);
    (void)static_cast<event_fn>(sigurdos_keyboard_has_event);
    (void)static_cast<event_fn>(sigurdos_keyboard_consume_event);
    (void)static_cast<brightness_fn>(sigurdos_keyboard_set_brightness);
    (void)static_cast<brightness_fn>(sigurdos_keyboard_set_default_brightness);
    (void)static_cast<state_fn>(sigurdos_keyboard_is_shift);
    (void)static_cast<state_fn>(sigurdos_keyboard_is_ctrl);
    (void)static_cast<state_fn>(sigurdos_keyboard_is_alt);
    (void)static_cast<scan_fn>(sigurdos_keyboard_reset_scan_state);
    (void)static_cast<scan_fn>(sigurdos_keyboard_consume_key);
    (void)static_cast<inject_fn>(sigurdos_keyboard_inject);
    (void)static_cast<inject_codepoint_fn>(sigurdos_keyboard_inject_codepoint);
    SUCCEED();
}

TEST(InputContractTest, KeyboardCharacterPickerEventEncodingStaysInternal) {
    uint32_t c_picker = sigurdos_keyboard_char_picker_key('c');
    uint32_t upper_picker = sigurdos_keyboard_char_picker_key('C');

    EXPECT_TRUE(sigurdos_keyboard_is_char_picker_key(c_picker));
    EXPECT_TRUE(sigurdos_keyboard_is_char_picker_key(upper_picker));
    EXPECT_FALSE(sigurdos_keyboard_is_char_picker_key((uint32_t)'c'));
    EXPECT_EQ(sigurdos_keyboard_char_picker_base(c_picker), 'c');
    EXPECT_EQ(sigurdos_keyboard_char_picker_base(upper_picker), 'C');
    EXPECT_GT(SIGURDOS_KEY_CHAR_PICKER_BASE, 0x10FFFFu);
}

TEST(InputContractTest, TrackballRemoteHookSignaturesStayStable) {
    using init_fn = bool (*)();
    using scan_fn = void (*)();
    using next_fn = bool (*)(SigurdOSTrackballEvent*);
    using inject_fn = void (*)(SigurdOSTrackballEvent);

    (void)static_cast<init_fn>(sigurdos_trackball_init);
    (void)static_cast<scan_fn>(sigurdos_trackball_scan);
    (void)static_cast<next_fn>(sigurdos_trackball_next_event);
    (void)static_cast<scan_fn>(sigurdos_trackball_reset_scan_state);
    (void)static_cast<inject_fn>(sigurdos_trackball_inject);
    SUCCEED();
}

} // namespace
