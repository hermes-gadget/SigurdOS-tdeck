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

#include <cmath>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "app/map_renderer.h"

namespace {

std::string read_project_file(const char* path) {
    const char* prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    for (const char* prefix : prefixes) {
        std::ifstream input(std::string(prefix) + path);
        if (input.good()) {
            return std::string(std::istreambuf_iterator<char>(input), {});
        }
    }
    return {};
}

using PngIhdrFixture = std::array<std::uint8_t, SIGURDOS_MAP_PNG_IHDR_SIZE>;

void write_u32(PngIhdrFixture* png, std::size_t offset, std::uint32_t value) {
    (*png)[offset] = static_cast<std::uint8_t>(value >> 24);
    (*png)[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    (*png)[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    (*png)[offset + 3] = static_cast<std::uint8_t>(value);
}

PngIhdrFixture make_png_ihdr(std::uint32_t width, std::uint32_t height,
                             std::uint8_t bit_depth, std::uint8_t color_type) {
    PngIhdrFixture png{};
    png[0] = 137;
    png[1] = 80;
    png[2] = 78;
    png[3] = 71;
    png[4] = 13;
    png[5] = 10;
    png[6] = 26;
    png[7] = 10;
    write_u32(&png, 8, 13);
    png[12] = 'I';
    png[13] = 'H';
    png[14] = 'D';
    png[15] = 'R';
    write_u32(&png, 16, width);
    write_u32(&png, 20, height);
    png[24] = bit_depth;
    png[25] = color_type;
    return png;
}

class MapRendererMathTest : public ::testing::Test {};

TEST_F(MapRendererMathTest, ConstantsMatchTDeckMapContract) {
    EXPECT_EQ(SIGURDOS_MAP_TILE_SIZE, 256);
    EXPECT_EQ(SIGURDOS_MAP_MIN_ZOOM, 0);
    EXPECT_EQ(SIGURDOS_MAP_MAX_ZOOM, 18);
    EXPECT_DOUBLE_EQ(SIGURDOS_MAP_MIN_LON, -180.0);
    EXPECT_DOUBLE_EQ(SIGURDOS_MAP_MAX_LON, 180.0);
}

TEST_F(MapRendererMathTest, InternalFallbackIsLimitedToSmallAllocations) {
    EXPECT_TRUE(sigurdos_map_internal_fallback_allowed(32U * 1024U));
    EXPECT_FALSE(sigurdos_map_internal_fallback_allowed(32U * 1024U + 1U));
    EXPECT_FALSE(sigurdos_map_internal_fallback_allowed(320U * 240U * 2U));
}

TEST_F(MapRendererMathTest, CraftedRgbaTileIhdrIsAccepted) {
    const PngIhdrFixture png = make_png_ihdr(256, 256, 8, 6);

    EXPECT_TRUE(sigurdos_map_png_ihdr_supported(png.data(), png.size()));
    EXPECT_GE(SIGURDOS_MAP_PNG_MAX_COMPRESSED_BYTES, 256U * 1024U);
    EXPECT_EQ(SIGURDOS_MAP_PNG_MAX_DECOMPRESSED_BYTES, 262400U);
}

TEST_F(MapRendererMathTest, CraftedPaletteTileIhdrIsAccepted) {
    const PngIhdrFixture png = make_png_ihdr(256, 256, 4, 3);

    EXPECT_TRUE(sigurdos_map_png_ihdr_supported(png.data(), png.size()));
}

TEST_F(MapRendererMathTest, CraftedWrongDimensionsAreRejected) {
    const PngIhdrFixture oversized = make_png_ihdr(8192, 8192, 8, 6);
    const PngIhdrFixture wrong_height = make_png_ihdr(256, 255, 8, 6);

    EXPECT_FALSE(sigurdos_map_png_ihdr_supported(oversized.data(), oversized.size()));
    EXPECT_FALSE(sigurdos_map_png_ihdr_supported(
        wrong_height.data(), wrong_height.size()));
}

TEST_F(MapRendererMathTest, CraftedUnsupportedColorModesAreRejected) {
    const PngIhdrFixture sixteen_bit = make_png_ihdr(256, 256, 16, 6);
    const PngIhdrFixture invalid_rgb_depth = make_png_ihdr(256, 256, 4, 2);
    const PngIhdrFixture unknown_color = make_png_ihdr(256, 256, 8, 1);

    EXPECT_FALSE(sigurdos_map_png_ihdr_supported(
        sixteen_bit.data(), sixteen_bit.size()));
    EXPECT_FALSE(sigurdos_map_png_ihdr_supported(
        invalid_rgb_depth.data(), invalid_rgb_depth.size()));
    EXPECT_FALSE(sigurdos_map_png_ihdr_supported(
        unknown_color.data(), unknown_color.size()));
}

TEST_F(MapRendererMathTest, CraftedTruncatedOrInterlacedHeadersAreRejected) {
    PngIhdrFixture interlaced = make_png_ihdr(256, 256, 8, 6);
    interlaced[28] = 1;

    EXPECT_FALSE(sigurdos_map_png_ihdr_supported(
        interlaced.data(), interlaced.size()));
    EXPECT_FALSE(sigurdos_map_png_ihdr_supported(
        interlaced.data(), SIGURDOS_MAP_PNG_IHDR_SIZE - 1));
    EXPECT_FALSE(sigurdos_map_png_ihdr_supported(nullptr, interlaced.size()));
}

TEST_F(MapRendererMathTest, ZoomValidationRejectsOutOfRangeValues) {
    EXPECT_TRUE(sigurdos_map_zoom_valid(0));
    EXPECT_TRUE(sigurdos_map_zoom_valid(18));
    EXPECT_FALSE(sigurdos_map_zoom_valid(-1));
    EXPECT_FALSE(sigurdos_map_zoom_valid(19));
}

TEST_F(MapRendererMathTest, TileIndexParsingRejectsOverflowAndWorldRange) {
    int value = -1;
    EXPECT_TRUE(sigurdos_map_parse_tile_index("0", 1, 0, &value));
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(sigurdos_map_parse_tile_index("255", 3, 8, &value));
    EXPECT_EQ(value, 255);

    EXPECT_FALSE(sigurdos_map_parse_tile_index("256", 3, 8, &value));
    EXPECT_FALSE(sigurdos_map_parse_tile_index(
        "999999999999999999999999", 24, 8, &value));
    EXPECT_FALSE(sigurdos_map_parse_tile_index("12x", 3, 8, &value));
    EXPECT_FALSE(sigurdos_map_parse_tile_index("0", 1, 19, &value));
    EXPECT_FALSE(sigurdos_map_parse_tile_index("0", 1, 0, nullptr));
}

TEST_F(MapRendererMathTest, SparseZoomNavigationSkipsUnavailableLevels) {
    bool available[19] = {};
    available[8] = true;
    available[10] = true;
    auto has_zoom = [&available](int zoom) { return available[zoom]; };

    EXPECT_EQ(sigurdos_map_select_available_zoom(8, 1, 8, 10, has_zoom), 10);
    EXPECT_EQ(sigurdos_map_select_available_zoom(10, -1, 8, 10, has_zoom), 8);
    EXPECT_EQ(sigurdos_map_select_available_zoom(10, 1, 8, 10, has_zoom), 10);
}

TEST_F(MapRendererMathTest, InvalidCurrentZoomUsesNearestLowerTieBreak) {
    bool available[19] = {};
    available[8] = true;
    available[10] = true;
    auto has_zoom = [&available](int zoom) { return available[zoom]; };

    EXPECT_EQ(sigurdos_map_select_available_zoom(9, 0, 8, 10, has_zoom), 8);
}

TEST_F(MapRendererMathTest, TilesPerAxisUsesZoomPowerOfTwo) {
    EXPECT_EQ(sigurdos_map_tiles_per_axis(0), 1);
    EXPECT_EQ(sigurdos_map_tiles_per_axis(1), 2);
    EXPECT_EQ(sigurdos_map_tiles_per_axis(10), 1024);
    EXPECT_EQ(sigurdos_map_tiles_per_axis(18), 262144);
    EXPECT_EQ(sigurdos_map_tiles_per_axis(19), 0);
}

TEST_F(MapRendererMathTest, TileValidationChecksZoomAndCoordinates) {
    EXPECT_TRUE(sigurdos_map_tile_valid(0, 0, 0));
    EXPECT_TRUE(sigurdos_map_tile_valid(5, 31, 31));

    EXPECT_FALSE(sigurdos_map_tile_valid(5, 32, 31));
    EXPECT_FALSE(sigurdos_map_tile_valid(5, 31, 32));
    EXPECT_FALSE(sigurdos_map_tile_valid(5, -1, 0));
    EXPECT_FALSE(sigurdos_map_tile_valid(19, 0, 0));
}

TEST_F(MapRendererMathTest, ViewportIntersectionRejectsBoundaryOnlyTiles) {
    EXPECT_TRUE(sigurdos_map_tile_intersects_viewport(-255, 0, 320, 240));
    EXPECT_TRUE(sigurdos_map_tile_intersects_viewport(319, 239, 320, 240));
    EXPECT_FALSE(sigurdos_map_tile_intersects_viewport(-256, 0, 320, 240));
    EXPECT_FALSE(sigurdos_map_tile_intersects_viewport(320, 0, 320, 240));
    EXPECT_FALSE(sigurdos_map_tile_intersects_viewport(0, -256, 320, 240));
    EXPECT_FALSE(sigurdos_map_tile_intersects_viewport(0, 240, 320, 240));
}

TEST_F(MapRendererMathTest, LongitudeConvertsToExpectedTileX) {
    EXPECT_NEAR(sigurdos_map_lon_to_tile_x(0.0, 0), 0.5, 1e-9);
    EXPECT_NEAR(sigurdos_map_lon_to_tile_x(0.0, 1), 1.0, 1e-9);
    EXPECT_NEAR(sigurdos_map_lon_to_tile_x(-180.0, 4), 0.0, 1e-9);
    EXPECT_NEAR(sigurdos_map_lon_to_tile_x(180.0, 4), 0.0, 1e-9);
    EXPECT_LT(sigurdos_map_lon_to_tile_x(179.999999, 4), 16.0);
    EXPECT_GT(sigurdos_map_lon_to_tile_x(-180.000001, 4), 15.999999);
}

TEST_F(MapRendererMathTest, LongitudeNormalizesToHalfOpenWorldInterval) {
    EXPECT_DOUBLE_EQ(sigurdos_map_wrap_lon(-180.0), -180.0);
    EXPECT_DOUBLE_EQ(sigurdos_map_wrap_lon(180.0), -180.0);
    EXPECT_DOUBLE_EQ(sigurdos_map_wrap_lon(540.0), -180.0);
    EXPECT_DOUBLE_EQ(sigurdos_map_wrap_lon(-540.0), -180.0);
    EXPECT_DOUBLE_EQ(sigurdos_map_wrap_lon(181.0), -179.0);
    EXPECT_DOUBLE_EQ(sigurdos_map_wrap_lon(-181.0), 179.0);
}

TEST_F(MapRendererMathTest, TileXWrapUsesModuloAndFloorAtWorldEdges) {
    EXPECT_EQ(sigurdos_map_wrap_tile_x(-1, 4), 15);
    EXPECT_EQ(sigurdos_map_wrap_tile_x(16, 4), 0);
    EXPECT_EQ(sigurdos_map_wrap_tile_x(33, 4), 1);
    EXPECT_EQ(sigurdos_map_tile_x_index(-0.25, 4), 15);
    EXPECT_EQ(sigurdos_map_tile_x_index(15.75, 4), 15);
    EXPECT_EQ(sigurdos_map_tile_x_index(16.25, 4), 0);
}

TEST_F(MapRendererMathTest, WrappedMarkerDeltaTakesShortestRoute) {
    EXPECT_NEAR(sigurdos_map_shortest_tile_x_delta(15.9, 0.1, 4), 0.2, 1e-9);
    EXPECT_NEAR(sigurdos_map_shortest_tile_x_delta(0.1, 15.9, 4), -0.2, 1e-9);
    EXPECT_NEAR(sigurdos_map_shortest_tile_x_delta(3.0, 5.0, 4), 2.0, 1e-9);
}

TEST_F(MapRendererMathTest, CoverageCanCrossAntimeridian) {
    EXPECT_TRUE(sigurdos_map_tile_x_in_coverage(15, 14, 1, true));
    EXPECT_TRUE(sigurdos_map_tile_x_in_coverage(0, 14, 1, true));
    EXPECT_TRUE(sigurdos_map_tile_x_in_coverage(1, 14, 1, true));
    EXPECT_FALSE(sigurdos_map_tile_x_in_coverage(2, 14, 1, true));
    EXPECT_FALSE(sigurdos_map_tile_x_in_coverage(13, 14, 1, true));
}

TEST_F(MapRendererMathTest, LatitudeConvertsToExpectedTileY) {
    EXPECT_NEAR(sigurdos_map_lat_to_tile_y(0.0, 0), 0.5, 1e-9);
    EXPECT_NEAR(sigurdos_map_lat_to_tile_y(0.0, 1), 1.0, 1e-9);
    EXPECT_NEAR(sigurdos_map_lat_to_tile_y(51.5074, 10), 340.506, 0.01);
}

TEST_F(MapRendererMathTest, TileCoordinatesConvertBackToLatLon) {
    EXPECT_NEAR(sigurdos_map_tile_x_to_lon(0.5, 0), 0.0, 1e-9);
    EXPECT_NEAR(sigurdos_map_tile_y_to_lat(0.5, 0), 0.0, 1e-9);
    EXPECT_NEAR(sigurdos_map_tile_x_to_lon(1.0, 1), 0.0, 1e-9);
    EXPECT_NEAR(sigurdos_map_tile_x_to_lon(16.0, 4), -180.0, 1e-9);
    EXPECT_NEAR(sigurdos_map_tile_x_to_lon(-1.0, 4), 157.5, 1e-9);
    EXPECT_NEAR(sigurdos_map_tile_y_to_lat(1.0, 1), 0.0, 1e-9);
}

TEST_F(MapRendererMathTest, MercatorRoundTripPreservesRepresentativePoint) {
    const double lat = 51.5074;
    const double lon = -0.1278;
    const int zoom = 10;

    const double tile_x = sigurdos_map_lon_to_tile_x(lon, zoom);
    const double tile_y = sigurdos_map_lat_to_tile_y(lat, zoom);

    EXPECT_NEAR(sigurdos_map_tile_x_to_lon(tile_x, zoom), lon, 1e-9);
    EXPECT_NEAR(sigurdos_map_tile_y_to_lat(tile_y, zoom), lat, 1e-9);
}

TEST_F(MapRendererMathTest, LatitudeInputIsClampedBeforeMercatorProjection) {
    const double north = sigurdos_map_lat_to_tile_y(90.0, 10);
    const double south = sigurdos_map_lat_to_tile_y(-90.0, 10);

    EXPECT_TRUE(std::isfinite(north));
    EXPECT_TRUE(std::isfinite(south));
    EXPECT_NEAR(north, sigurdos_map_lat_to_tile_y(SIGURDOS_MAP_MAX_LAT, 10), 1e-9);
    EXPECT_NEAR(south, sigurdos_map_lat_to_tile_y(SIGURDOS_MAP_MIN_LAT, 10), 1e-9);
}

TEST_F(MapRendererMathTest, InvalidZoomReturnsNeutralCoordinate) {
    EXPECT_DOUBLE_EQ(sigurdos_map_lon_to_tile_x(0.0, -1), 0.0);
    EXPECT_DOUBLE_EQ(sigurdos_map_lat_to_tile_y(0.0, 19), 0.0);
    EXPECT_DOUBLE_EQ(sigurdos_map_tile_x_to_lon(0.0, 19), 0.0);
    EXPECT_DOUBLE_EQ(sigurdos_map_tile_y_to_lat(0.0, -1), 0.0);
}

TEST_F(MapRendererMathTest, PublicConversionApisRequireBothOutputPointers) {
    double latitude = 0.0;
    double longitude = 0.0;

    EXPECT_TRUE(sigurdos_map_output_pair_valid(&latitude, &longitude));
    EXPECT_FALSE(sigurdos_map_output_pair_valid(nullptr, &longitude));
    EXPECT_FALSE(sigurdos_map_output_pair_valid(&latitude, nullptr));
    EXPECT_FALSE(sigurdos_map_output_pair_valid(nullptr, nullptr));
}

TEST_F(MapRendererMathTest, ContactApisRejectInvalidPointerCountCombinations) {
    int contact_storage = 0;

    EXPECT_TRUE(sigurdos_map_required_pointer_valid(&contact_storage));
    EXPECT_FALSE(sigurdos_map_required_pointer_valid(nullptr));
    EXPECT_TRUE(sigurdos_map_contact_args_valid(nullptr, 0));
    EXPECT_TRUE(sigurdos_map_contact_args_valid(&contact_storage, 0));
    EXPECT_TRUE(sigurdos_map_contact_args_valid(&contact_storage, 1));
    EXPECT_FALSE(sigurdos_map_contact_args_valid(nullptr, 1));
    EXPECT_FALSE(sigurdos_map_contact_args_valid(nullptr, -1));
    EXPECT_FALSE(sigurdos_map_contact_args_valid(&contact_storage, -1));
}

TEST_F(MapRendererMathTest, PositionMarkerRequiresFiniteInRangeFix) {
    EXPECT_TRUE(sigurdos_map_position_valid(true, 0.0, 0.0));
    EXPECT_FALSE(sigurdos_map_position_valid(false, 51.5, -0.1));
    EXPECT_FALSE(sigurdos_map_position_valid(true, NAN, -0.1));
    EXPECT_FALSE(sigurdos_map_position_valid(true, 90.0, -0.1));
    EXPECT_FALSE(sigurdos_map_position_valid(true, 51.5, 181.0));
}

TEST_F(MapRendererMathTest, MetadataBoundsMustBeFiniteOrderedAndGeographic) {
    const double valid[] = {-1.0, 50.0, 1.0, 52.0};
    const double nan_bounds[] = {-1.0, NAN, 1.0, 52.0};
    const double reversed[] = {1.0, 52.0, -1.0, 50.0};
    const double out_of_range[] = {-181.0, 50.0, 1.0, 52.0};

    EXPECT_TRUE(sigurdos_map_bounds_valid(valid));
    EXPECT_FALSE(sigurdos_map_bounds_valid(nan_bounds));
    EXPECT_FALSE(sigurdos_map_bounds_valid(reversed));
    EXPECT_FALSE(sigurdos_map_bounds_valid(out_of_range));
    EXPECT_FALSE(sigurdos_map_bounds_valid(nullptr));
}

TEST_F(MapRendererMathTest, MarkerOriginAccountsForContentOffset) {
    EXPECT_EQ(sigurdos_map_marker_origin(160, 0, 8), 156);
    EXPECT_EQ(sigurdos_map_marker_origin(120, 22, 8), 94);
}

TEST_F(MapRendererMathTest, DiscoveryBudgetCapsOperationsPerTick) {
    SigurdosMapDiscoveryBudget budget(3);

    EXPECT_TRUE(budget.consume());
    EXPECT_TRUE(budget.consume());
    EXPECT_TRUE(budget.consume());
    EXPECT_FALSE(budget.consume());
    EXPECT_EQ(budget.used(), 3);
    EXPECT_EQ(budget.remaining(), 0);
}

TEST_F(MapRendererMathTest, DiscoveryDeadlineHandlesClockWrap) {
    EXPECT_FALSE(sigurdos_map_discovery_deadline_reached(100, 104, 5));
    EXPECT_TRUE(sigurdos_map_discovery_deadline_reached(100, 105, 5));
    EXPECT_FALSE(sigurdos_map_discovery_deadline_reached(
        UINT32_MAX - 2U, 1U, 5U));
    EXPECT_TRUE(sigurdos_map_discovery_deadline_reached(
        UINT32_MAX - 2U, 2U, 5U));
}

TEST_F(MapRendererMathTest, SlowTileReadsHitWorkerDeadlineIncludingClockWrap) {
    EXPECT_FALSE(sigurdos_map_tile_worker_deadline_reached(100, 849, 750));
    EXPECT_TRUE(sigurdos_map_tile_worker_deadline_reached(100, 850, 750));
    EXPECT_FALSE(sigurdos_map_tile_worker_deadline_reached(
        UINT32_MAX - 20U, 10U, 32U));
    EXPECT_TRUE(sigurdos_map_tile_worker_deadline_reached(
        UINT32_MAX - 20U, 11U, 32U));
}

TEST_F(MapRendererMathTest, TilePipelineHasBoundedWorkAndMemory) {
    EXPECT_EQ(SIGURDOS_MAP_TILE_REQUEST_QUEUE_LENGTH, 4);
    EXPECT_EQ(SIGURDOS_MAP_TILE_COMPLETION_QUEUE_LENGTH, 2);
    EXPECT_EQ(SIGURDOS_MAP_TILE_READ_CHUNK_BYTES, 4096U);
    EXPECT_EQ(SIGURDOS_MAP_TILE_RGB565_BYTES, 128U * 1024U);
    EXPECT_LE(SIGURDOS_MAP_TILE_WORK_MAX_MS, 1000U);
}

TEST_F(MapRendererMathTest, OomCompletionCannotPublishPixels) {
    const SigurdosMapTileCompletion completion{
        10, 511, 340, 7,
        SigurdosMapTileCompletionStatus::OutOfMemory, nullptr};
    EXPECT_EQ(completion.status,
              SigurdosMapTileCompletionStatus::OutOfMemory);
    EXPECT_EQ(completion.pixels, nullptr);
}

TEST_F(MapRendererMathTest, RapidPanAndZoomInvalidateOlderGeneration) {
    const std::uint32_t initial = 41;
    const std::uint32_t after_pan = initial + 1;
    const std::uint32_t after_zoom = after_pan + 1;

    EXPECT_FALSE(sigurdos_map_generation_owns(after_pan, initial));
    EXPECT_FALSE(sigurdos_map_generation_owns(after_zoom, after_pan));
    EXPECT_TRUE(sigurdos_map_generation_owns(after_zoom, after_zoom));
}

TEST_F(MapRendererMathTest, StaleCompletionIsNotOwnedByCurrentView) {
    const SigurdosMapTileCompletion stale{
        10, 511, 340, 12, SigurdosMapTileCompletionStatus::Ready,
        reinterpret_cast<std::uint16_t*>(1)};

    EXPECT_FALSE(sigurdos_map_generation_owns(13, stale.generation));
}

TEST_F(MapRendererMathTest, TileIndexEntriesRequireBoundedRealSample) {
    const SigurdosMapTileIndexEntry valid = {8, 100, 110, 80, 90,
                                              103, 84, 42};
    EXPECT_TRUE(sigurdos_map_tile_index_entry_valid(valid));

    SigurdosMapTileIndexEntry invalid = valid;
    invalid.sample_x = 111;
    EXPECT_FALSE(sigurdos_map_tile_index_entry_valid(invalid));
    invalid = valid;
    invalid.max_y = 256;
    EXPECT_FALSE(sigurdos_map_tile_index_entry_valid(invalid));
    invalid = valid;
    invalid.tile_count = 0;
    EXPECT_FALSE(sigurdos_map_tile_index_entry_valid(invalid));
}

TEST_F(MapRendererMathTest, MapScreenShowsBeforeStartingCancellableDiscovery) {
    const std::string source =
        read_project_file("src/ui/screens/screen_map.cpp");
    ASSERT_FALSE(source.empty());

    const size_t discover = source.find("sigurdos_map_discover_tiles();");
    const size_t show = source.rfind("show_screen(scr);", discover);
    const size_t lifetime = source.find(
        "g_map_lifetime.trackTimer(&g_map_discovery_timer);");
    const size_t cancel = source.find("sigurdos_map_cancel_discovery();");
    ASSERT_NE(show, std::string::npos);
    ASSERT_NE(discover, std::string::npos);
    ASSERT_NE(lifetime, std::string::npos);
    ASSERT_NE(cancel, std::string::npos);
    EXPECT_LT(show, discover);
    EXPECT_LT(lifetime, cancel);
}

TEST_F(MapRendererMathTest, TileIoAndDecodeAreOutsideLvglRenderCallback) {
    const std::string source =
        read_project_file("src/app/map_renderer.cpp");
    ASSERT_FALSE(source.empty());

    const size_t render = source.find("void sigurdos_map_render()");
    const size_t next_function =
        source.find("bool sigurdos_map_tiles_available()", render);
    ASSERT_NE(render, std::string::npos);
    ASSERT_NE(next_function, std::string::npos);
    const std::string render_body =
        source.substr(render, next_function - render);
    EXPECT_EQ(render_body.find("fopen("), std::string::npos);
    EXPECT_EQ(render_body.find("fread("), std::string::npos);
    EXPECT_EQ(render_body.find("lodepng_decode("), std::string::npos);
    EXPECT_NE(render_body.find("request_tile("), std::string::npos);
}

TEST_F(MapRendererMathTest, NavigationTeardownOwnsTilePollAndCancelsWork) {
    const std::string screen =
        read_project_file("src/ui/screens/screen_map.cpp");
    const std::string renderer =
        read_project_file("src/app/map_renderer.cpp");
    ASSERT_FALSE(screen.empty());
    ASSERT_FALSE(renderer.empty());

    EXPECT_NE(screen.find("g_map_lifetime.trackTimer(&g_map_tile_timer);"),
              std::string::npos);
    EXPECT_NE(screen.find("sigurdos_map_process_tile_completions()"),
              std::string::npos);
    const size_t deinit = renderer.find("void sigurdos_map_deinit()");
    const size_t generation =
        renderer.find("advance_tile_generation();", deinit);
    const size_t initialized_guard =
        renderer.find("if (!initialized) return;", deinit);
    ASSERT_NE(deinit, std::string::npos);
    ASSERT_NE(generation, std::string::npos);
    ASSERT_NE(initialized_guard, std::string::npos);
    EXPECT_LT(generation, initialized_guard);
}

TEST_F(MapRendererMathTest, OwnedDiscoveryBufferIsFreedExactlyOnce) {
    int allocations = 0;
    int frees = 0;
    void* buffer = std::malloc(40960);
    ASSERT_NE(buffer, nullptr);
    ++allocations;

    EXPECT_TRUE(sigurdos_map_release_owned_buffer(
        buffer, [&frees](void* allocation) {
            ++frees;
            std::free(allocation);
        }));
    EXPECT_EQ(buffer, nullptr);
    EXPECT_EQ(allocations, 1);
    EXPECT_EQ(frees, 1);

    EXPECT_FALSE(sigurdos_map_release_owned_buffer(
        buffer, [&frees](void* allocation) {
            ++frees;
            std::free(allocation);
        }));
    EXPECT_EQ(frees, 1);
}

} // namespace
