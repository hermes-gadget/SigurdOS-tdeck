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

#include <cstdlib>

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "app/qr_show.h"

namespace {

using sigurdos::app::SIGURDOS_QR_CANVAS_MAX_PX;
using sigurdos::app::SIGURDOS_QR_MODULE_BUFFER_BYTES;
using sigurdos::app::SIGURDOS_QR_MAX_PAYLOAD_BYTES;
using sigurdos::app::SIGURDOS_QR_QUIET_ZONE_MODULES;
using sigurdos::app::SIGURDOS_QR_VERSION;
using sigurdos::app::QrCanvasLayout;
using sigurdos::app::sigurdos_qr_module_buffer_bytes;
using sigurdos::app::sigurdos_qr_module_count;
using sigurdos::app::sigurdos_qr_canvas_layout;
using sigurdos::app::sigurdos_qr_canvas_buffer_bytes;
using sigurdos::app::sigurdos_qr_payload_fits;
using sigurdos::app::sigurdos_qr_render_dark_modules;
using sigurdos::app::sigurdos_qr_release_canvas_buffer;
using sigurdos::app::sigurdos_qr_select_smallest_version;

class QrShowLayoutTest : public ::testing::Test {};

TEST_F(QrShowLayoutTest, ConfiguredQrVersionHasExactModuleBufferSize) {
    EXPECT_EQ(SIGURDOS_QR_VERSION, 10);
    EXPECT_EQ(sigurdos_qr_module_count(SIGURDOS_QR_VERSION), 57);
    EXPECT_EQ(SIGURDOS_QR_MODULE_BUFFER_BYTES, 407);
    EXPECT_EQ(SIGURDOS_QR_MODULE_BUFFER_BYTES,
              sigurdos_qr_module_buffer_bytes(SIGURDOS_QR_VERSION));
}

TEST_F(QrShowLayoutTest, VersionTenMediumEccAccepts213BytePayload) {
    const std::string payload(SIGURDOS_QR_MAX_PAYLOAD_BYTES, 'a');

    EXPECT_TRUE(sigurdos_qr_payload_fits(payload.c_str()));
}

TEST_F(QrShowLayoutTest, VersionTenMediumEccRejects214BytePayload) {
    const std::string payload(SIGURDOS_QR_MAX_PAYLOAD_BYTES + 1, 'a');

    EXPECT_FALSE(sigurdos_qr_payload_fits(payload.c_str()));
}

TEST_F(QrShowLayoutTest, VersionTenMediumEccRejects400BytePayload) {
    const std::string payload(400, 'a');

    EXPECT_FALSE(sigurdos_qr_payload_fits(payload.c_str()));
}

TEST_F(QrShowLayoutTest, SelectsFirstVersionThatCanEncodePayload) {
    std::vector<int> attempted;
    const int selected = sigurdos_qr_select_smallest_version(
        [&attempted](int version) {
            attempted.push_back(version);
            return version >= 3;
        });

    EXPECT_EQ(selected, 3);
    EXPECT_EQ(attempted, (std::vector<int>{1, 2, 3}));
}

TEST_F(QrShowLayoutTest, SelectionFailsAfterMaximumVersion) {
    int attempts = 0;
    EXPECT_EQ(sigurdos_qr_select_smallest_version(
                  [&attempts](int) { ++attempts; return false; }),
              0);
    EXPECT_EQ(attempts, SIGURDOS_QR_VERSION);
}

TEST_F(QrShowLayoutTest, VersionZeroDoesNotProduceUsableBufferSize) {
    EXPECT_EQ(sigurdos_qr_module_count(0), 0);
    EXPECT_EQ(sigurdos_qr_module_buffer_bytes(0), 0);
}

TEST_F(QrShowLayoutTest, VersionOneQrUsesLargestScaleThatFitsContent) {
    QrCanvasLayout layout = sigurdos_qr_canvas_layout(21, 320, 199);

    EXPECT_TRUE(layout.fits);
    EXPECT_EQ(layout.scale, 6);
    EXPECT_EQ(layout.canvas_size, 174);
    EXPECT_LE(layout.canvas_size, SIGURDOS_QR_CANVAS_MAX_PX);
}

TEST_F(QrShowLayoutTest, VersionTenQrFitsWithinTDeckContentHeight) {
    QrCanvasLayout layout = sigurdos_qr_canvas_layout(57, 320, 199);

    EXPECT_TRUE(layout.fits);
    EXPECT_EQ(layout.scale, 2);
    EXPECT_EQ(layout.canvas_size, 130);
    EXPECT_LE(layout.canvas_size, SIGURDOS_QR_CANVAS_MAX_PX);
    EXPECT_EQ(sigurdos_qr_canvas_buffer_bytes(layout), 33800U);
}

TEST_F(QrShowLayoutTest, CanvasAllocationTracksSelectedLayoutExactly) {
    const QrCanvasLayout small = sigurdos_qr_canvas_layout(21, 320, 199);
    const QrCanvasLayout large = sigurdos_qr_canvas_layout(57, 320, 199);

    EXPECT_EQ(sigurdos_qr_canvas_buffer_bytes(small), 60552U);
    EXPECT_EQ(sigurdos_qr_canvas_buffer_bytes(large), 33800U);
    EXPECT_EQ(sigurdos_qr_canvas_buffer_bytes({0, 0, false}), 0U);
}

TEST_F(QrShowLayoutTest, LargestSupportedQrStillFitsFixedCanvasAtScaleOne) {
    QrCanvasLayout layout = sigurdos_qr_canvas_layout(169, 320, 199);

    EXPECT_TRUE(layout.fits);
    EXPECT_EQ(layout.scale, 1);
    EXPECT_EQ(layout.canvas_size, 177);
    EXPECT_LE(layout.canvas_size, SIGURDOS_QR_CANVAS_MAX_PX);
}

TEST_F(QrShowLayoutTest, OversizedQrIsRejectedBeforeCanvasAllocation) {
    QrCanvasLayout layout = sigurdos_qr_canvas_layout(181, 320, 320);

    EXPECT_FALSE(layout.fits);
    EXPECT_EQ(layout.scale, 0);
    EXPECT_EQ(layout.canvas_size, 0);
}

TEST_F(QrShowLayoutTest, InvalidInputsReturnNonFittingLayout) {
    EXPECT_FALSE(sigurdos_qr_canvas_layout(0, 320, 199).fits);
    EXPECT_FALSE(sigurdos_qr_canvas_layout(21, 20, 199).fits);
    EXPECT_FALSE(sigurdos_qr_canvas_layout(21, 320, 20).fits);
    EXPECT_FALSE(sigurdos_qr_canvas_layout(21, 320, 199, 20, 0).fits);
    EXPECT_FALSE(sigurdos_qr_canvas_layout(21, 320, 199, 20, 6, 0).fits);
}

TEST_F(QrShowLayoutTest, CustomCanvasLimitConstrainsScale) {
    QrCanvasLayout layout = sigurdos_qr_canvas_layout(50, 200, 200, 20, 4, 120);

    EXPECT_TRUE(layout.fits);
    EXPECT_EQ(layout.scale, 2);
    EXPECT_EQ(layout.canvas_size, 116);
}

TEST_F(QrShowLayoutTest, FinderPatternRendersDarkWithFourModuleQuietZone) {
    static const char* finder[7] = {
        "#######",
        "#.....#",
        "#.###.#",
        "#.###.#",
        "#.###.#",
        "#.....#",
        "#######",
    };
    const int canvas_size = 7 + 2 * SIGURDOS_QR_QUIET_ZONE_MODULES;
    std::vector<char> raster(canvas_size * canvas_size, '.');

    sigurdos_qr_render_dark_modules(
        7, 1,
        [](int x, int y) { return finder[y][x] == '#'; },
        [&raster, canvas_size](int x, int y) {
            raster[y * canvas_size + x] = '#';
        });

    std::string actual;
    for (int y = 0; y < canvas_size; ++y) {
        actual.append(&raster[y * canvas_size], canvas_size);
        actual.push_back('\n');
    }
    const std::string expected =
        "...............\n"
        "...............\n"
        "...............\n"
        "...............\n"
        "....#######....\n"
        "....#.....#....\n"
        "....#.###.#....\n"
        "....#.###.#....\n"
        "....#.###.#....\n"
        "....#.....#....\n"
        "....#######....\n"
        "...............\n"
        "...............\n"
        "...............\n"
        "...............\n";

    EXPECT_EQ(actual, expected);
}

TEST_F(QrShowLayoutTest, CanvasDeleteLifecycleReleasesScreenOwnedBuffer) {
    int allocations = 0;
    int frees = 0;
    uint8_t* canvas_buffer = static_cast<uint8_t*>(std::malloc(64800));
    ASSERT_NE(canvas_buffer, nullptr);
    ++allocations;

    EXPECT_TRUE(sigurdos_qr_release_canvas_buffer(
        canvas_buffer, [&frees](uint8_t* buffer) {
            ++frees;
            std::free(buffer);
        }));
    EXPECT_EQ(allocations, 1);
    EXPECT_EQ(frees, 1);

    EXPECT_FALSE(sigurdos_qr_release_canvas_buffer(
        static_cast<uint8_t*>(nullptr), [&frees](uint8_t* buffer) {
            ++frees;
            std::free(buffer);
        }));
    EXPECT_EQ(frees, 1);
}

} // namespace
