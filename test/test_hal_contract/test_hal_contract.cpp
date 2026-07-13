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

#include <gtest/gtest.h>

#include "hal/display.h"
#include "hal/gps.h"

namespace {

TEST(HalContractTest, DisplayLifecycleAndPowerApisStayStable) {
    using init_fn = bool (*)();
    using input_init_fn = SigurdOSInputInitStatus (*)();
    using void_fn = void (*)();
    using millis_fn = uint32_t (*)();
    using brightness_fn = void (*)(uint8_t);
    using bool_fn = bool (*)();

    (void)static_cast<init_fn>(sigurdos_display_init);
    (void)static_cast<input_init_fn>(sigurdos_display_init_inputs);
    (void)static_cast<void_fn>(sigurdos_display_loop);
    (void)static_cast<void_fn>(sigurdos_display_render_now);
    (void)static_cast<millis_fn>(sigurdos_display_millis);
    (void)static_cast<void_fn>(sigurdos_display_wake);
    (void)static_cast<bool_fn>(sigurdos_display_is_on);
    (void)static_cast<brightness_fn>(sigurdos_display_set_brightness);
    (void)static_cast<void_fn>(sigurdos_display_reset_auto_off);
    SUCCEED();
}

TEST(HalContractTest, DisplayInputHealthRequiresEveryDeferredStep) {
    EXPECT_TRUE((SigurdOSInputInitStatus{true, true, true, true}).all_ready());
    EXPECT_FALSE((SigurdOSInputInitStatus{false, true, true, true}).all_ready());
    EXPECT_FALSE((SigurdOSInputInitStatus{true, true, false, true}).all_ready());
}

TEST(HalContractTest, DisplayAutoOffElapsedComparisonIsWrapSafe) {
    EXPECT_FALSE(sigurdos_display_auto_off_expired(1000, 500, 0));
    EXPECT_FALSE(sigurdos_display_auto_off_expired(1099, 1000, 100));
    EXPECT_TRUE(sigurdos_display_auto_off_expired(1100, 1000, 100));

    const uint32_t activity = UINT32_MAX - 49U;
    EXPECT_FALSE(sigurdos_display_auto_off_expired(49, activity, 100));
    EXPECT_TRUE(sigurdos_display_auto_off_expired(50, activity, 100));
}

TEST(HalContractTest, DisplayDebugCaptureApisStayStable) {
    using buffer_fn = void* (*)();
    using dimension_fn = uint32_t (*)();
    using void_fn = void (*)();
    using encode_fn = uint32_t (*)(uint32_t);

    (void)static_cast<buffer_fn>(sigurdos_display_get_buffer);
    (void)static_cast<dimension_fn>(sigurdos_display_get_width);
    (void)static_cast<dimension_fn>(sigurdos_display_get_height);
    (void)static_cast<encode_fn>(sigurdos_display_encode_text_key);
    (void)static_cast<void_fn>(sigurdos_display_capture_framebuffer);
    SUCCEED();
}

TEST(HalContractTest, DisplayTextKeyEncoderMatchesLvglUtf8Payloads) {
    EXPECT_EQ(sigurdos_display_encode_text_key('A'), 0x41u);
    EXPECT_EQ(sigurdos_display_encode_text_key(0x00FC), 0x0000BCC3u);     // u diaeresis
    EXPECT_EQ(sigurdos_display_encode_text_key(0x00E7), 0x0000A7C3u);     // c cedilla
    EXPECT_EQ(sigurdos_display_encode_text_key(0x00EA), 0x0000AAC3u);     // e circumflex
    EXPECT_EQ(sigurdos_display_encode_text_key(0x20AC), 0x00AC82E2u);     // euro sign
    EXPECT_EQ(sigurdos_display_encode_text_key(0x1F642), 0x82999FF0u);    // slight smile
    EXPECT_EQ(sigurdos_display_encode_text_key(0xD800), (uint32_t)'?');   // surrogate
    EXPECT_EQ(sigurdos_display_encode_text_key(0x110000), (uint32_t)'?'); // out of Unicode range
}

TEST(HalContractTest, GpsLifecycleAndFixApisStayStable) {
    using void_fn = void (*)();
    using float_fn = float (*)();
    using byte_fn = uint8_t (*)();
    using bool_fn = bool (*)();

    (void)static_cast<void_fn>(sigurdos_gps_init);
    (void)static_cast<void_fn>(sigurdos_gps_loop);
    (void)static_cast<float_fn>(sigurdos_gps_latitude);
    (void)static_cast<float_fn>(sigurdos_gps_longitude);
    (void)static_cast<float_fn>(sigurdos_gps_altitude_m);
    (void)static_cast<float_fn>(sigurdos_gps_speed_kn);
    (void)static_cast<float_fn>(sigurdos_gps_heading);
    (void)static_cast<byte_fn>(sigurdos_gps_satellites);
    (void)static_cast<byte_fn>(sigurdos_gps_fix_quality);
    (void)static_cast<bool_fn>(sigurdos_gps_has_fix);
    SUCCEED();
}

TEST(HalContractTest, GpsUtcTimeApisStayStable) {
    using byte_fn = uint8_t (*)();
    using bool_fn = bool (*)();
    using pending_fn = bool (*)(SigurdOSGpsUtcTime*);
    using void_fn = void (*)();

    (void)static_cast<byte_fn>(sigurdos_gps_hour);
    (void)static_cast<byte_fn>(sigurdos_gps_minute);
    (void)static_cast<byte_fn>(sigurdos_gps_second);
    (void)static_cast<bool_fn>(sigurdos_gps_time_synced);
    (void)static_cast<pending_fn>(sigurdos_gps_get_pending_time);
    (void)static_cast<void_fn>(sigurdos_gps_mark_time_synced);
    SUCCEED();
}

} // namespace
