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

#include <gtest/gtest.h>

#include "app/map_renderer.h"

namespace {

class MapRendererMathTest : public ::testing::Test {};

TEST_F(MapRendererMathTest, ConstantsMatchTDeckMapContract) {
    EXPECT_EQ(SIGURDOS_MAP_TILE_SIZE, 256);
    EXPECT_EQ(SIGURDOS_MAP_MIN_ZOOM, 0);
    EXPECT_EQ(SIGURDOS_MAP_MAX_ZOOM, 18);
    EXPECT_DOUBLE_EQ(SIGURDOS_MAP_MIN_LON, -180.0);
    EXPECT_DOUBLE_EQ(SIGURDOS_MAP_MAX_LON, 180.0);
}

TEST_F(MapRendererMathTest, ZoomValidationRejectsOutOfRangeValues) {
    EXPECT_TRUE(sigurdos_map_zoom_valid(0));
    EXPECT_TRUE(sigurdos_map_zoom_valid(18));
    EXPECT_FALSE(sigurdos_map_zoom_valid(-1));
    EXPECT_FALSE(sigurdos_map_zoom_valid(19));
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
    EXPECT_NEAR(sigurdos_map_lon_to_tile_x(180.0, 4), 16.0, 1e-9);
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

} // namespace
