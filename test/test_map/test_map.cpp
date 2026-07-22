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
 * Unit tests for offline map tile math and rendering contract
 * Tests: Web Mercator coordinate math, tile bounds checking,
 *        lat/lon ↔ tile conversion, zoom level validation, pan limits
 */
#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include "tile_cache.h"

namespace {

// Earth radius for Web Mercator
static constexpr double PI = 3.14159265358979323846;
static constexpr double EARTH_RADIUS_M = 6378137.0;

// ── Web Mercator math (EPSG:3857) ────────────────────────
// Convert longitude to meters
double lon_to_meters_x(double lon) {
    return lon * (PI / 180.0) * EARTH_RADIUS_M;
}

// Convert latitude to meters
double lat_to_meters_y(double lat) {
    double lat_rad = lat * PI / 180.0;
    return EARTH_RADIUS_M * log(tan(PI / 4.0 + lat_rad / 2.0));
}

// Convert lat/lon to tile coordinates at a given zoom level
void latlon_to_tile(double lat, double lon, int zoom,
                    int* tile_x, int* tile_y) {
    int n = 1 << zoom;
    *tile_x = (int)((lon + 180.0) / 360.0 * n);
    double lat_rad = lat * PI / 180.0;
    *tile_y = (int)((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / PI) / 2.0 * n);

    // Clamp
    if (*tile_x < 0) *tile_x = 0;
    if (*tile_x >= n) *tile_x = n - 1;
    if (*tile_y < 0) *tile_y = 0;
    if (*tile_y >= n) *tile_y = n - 1;
}

// Get the total number of tiles at a given zoom level
int tiles_at_zoom(int zoom) {
    int n = 1 << zoom;
    return n * n;
}

// Check if a tile is within valid range for a zoom level
bool tile_valid(int z, int x, int y) {
    int max_tile = (1 << z) - 1;
    return x >= 0 && x <= max_tile && y >= 0 && y <= max_tile;
}

// ── Tile path on SD card ──────────────────────────────────
void tile_to_path(int z, int x, int y, char* buf, size_t buf_sz) {
    snprintf(buf, buf_sz, "/sdcard/tiles/%d/%d/%d.png", z, x, y);
}

// ── LVGL canvas math (simplified) ─────────────────────────
struct MapViewport {
    int screen_w;     // display width
    int screen_h;     // display height
    double center_lon;
    double center_lat;
    int zoom;         // 0-18
};

void viewport_pixel_to_latlon(const MapViewport& vp, int px, int py,
                               double* out_lat, double* out_lon) {
    // Simplified: pixel offset from center as lat/lon delta
    // At zoom level z, each tile is 256×256 pixels
    double tile_size = 256.0;
    int tiles_per_dim = 1 << vp.zoom;
    double world_pixels = tile_size * tiles_per_dim;

    // Pixel position in world coordinates
    double center_px = world_pixels / 2.0;
    double center_py = world_pixels / 2.0;

    double offset_x = (px - vp.screen_w / 2.0);
    double offset_y = (py - vp.screen_h / 2.0);

    // Lon: linear with x
    double lon = vp.center_lon + (offset_x / world_pixels) * 360.0;

    // Lat: approximate (non-linear, simplified for test)
    double lat_range = 180.0 / (1 << vp.zoom) * 4.0; // rough at this zoom
    double lat = vp.center_lat - (offset_y / world_pixels) * 360.0;

    // Clamp
    if (lon > 180.0) lon = 180.0;
    if (lon < -180.0) lon = -180.0;
    if (lat > 85.0511) lat = 85.0511;
    if (lat < -85.0511) lat = -85.0511;

    *out_lat = lat;
    *out_lon = lon;
}

// ════════════════════════════════════════════════════════
// TESTS
// ════════════════════════════════════════════════════════
class MapTest : public ::testing::Test {
protected:
    MapViewport vp;
    void SetUp() override {
        vp = {320, 240, 0.0, 51.5, 10}; // London-ish
    }
};

// ── Web Mercator ──────────────────────────────────────────
TEST_F(MapTest, LonZeroIsMercatorCenter) {
    double x = lon_to_meters_x(0.0);
    EXPECT_NEAR(x, 0.0, 0.1);
}

TEST_F(MapTest, LatZeroIsMercatorEquator) {
    double y = lat_to_meters_y(0.0);
    EXPECT_NEAR(y, 0.0, 0.1);
}

TEST_F(MapTest, MercatorIsSymmetric) {
    double y1 = lat_to_meters_y(45.0);
    double y2 = lat_to_meters_y(-45.0);
    EXPECT_NEAR(y1, -y2, 1000.0);
}

// ── Tile coordinates ──────────────────────────────────────
TEST_F(MapTest, Zoom0HasOneTile) {
    EXPECT_EQ(tiles_at_zoom(0), 1);
}

TEST_F(MapTest, Zoom1HasFourTiles) {
    EXPECT_EQ(tiles_at_zoom(1), 4);
}

TEST_F(MapTest, Zoom10Has1MTiles) {
    EXPECT_EQ(tiles_at_zoom(10), 1024 * 1024);
}

TEST_F(MapTest, NullIslandAtZoom0) {
    int tx, ty;
    latlon_to_tile(0.0, 0.0, 0, &tx, &ty);
    EXPECT_EQ(tx, 0);
    EXPECT_EQ(ty, 0);
}

TEST_F(MapTest, LondonAtZoom10) {
    int tx, ty;
    latlon_to_tile(51.5074, -0.1278, 10, &tx, &ty);
    EXPECT_TRUE(tile_valid(10, tx, ty));
    // London should be roughly tile_x: ~511, tile_y: ~340 at zoom 10
    EXPECT_EQ(tx, 511);
    EXPECT_NEAR(ty, 340, 2);
}

TEST_F(MapTest, PoleClampedToValidRange) {
    int tx, ty;
    latlon_to_tile(90.0, 0.0, 5, &tx, &ty);
    EXPECT_TRUE(tile_valid(5, tx, ty));
    EXPECT_EQ(ty, 0); // North pole → top row
}

// ── Tile validity ─────────────────────────────────────────
TEST_F(MapTest, TileValidInRange) {
    EXPECT_TRUE(tile_valid(5, 10, 10));
    EXPECT_TRUE(tile_valid(0, 0, 0));
    EXPECT_TRUE(tile_valid(5, 0, 0));
    EXPECT_TRUE(tile_valid(5, 31, 31)); // max at zoom 5
}

TEST_F(MapTest, TileInvalidOutOfRange) {
    EXPECT_FALSE(tile_valid(5, 32, 10)); // x > 31
    EXPECT_FALSE(tile_valid(5, 10, 32)); // y > 31
    EXPECT_FALSE(tile_valid(5, -1, 10));
}

// ── Tile path ─────────────────────────────────────────────
TEST_F(MapTest, TilePathFormat) {
    char buf[64];
    tile_to_path(10, 512, 340, buf, sizeof(buf));
    EXPECT_STREQ(buf, "/sdcard/tiles/10/512/340.png");
}

// ── Viewport pan limits ───────────────────────────────────
TEST_F(MapTest, CenterPixelIsCenterLatLon) {
    double lat, lon;
    vp = {320, 240, -0.1278, 51.5074, 10};
    viewport_pixel_to_latlon(vp, 160, 120, &lat, &lon);
    EXPECT_NEAR(lat, 51.5074, 0.5);
    EXPECT_NEAR(lon, -0.1278, 0.5);
}

TEST_F(MapTest, ScreenCornerMapsToValidCoord) {
    double lat, lon;
    viewport_pixel_to_latlon(vp, 0, 0, &lat, &lon);
    EXPECT_GE(lat, -85.0511);
    EXPECT_LE(lat, 85.0511);
    EXPECT_GE(lon, -180.0);
    EXPECT_LE(lon, 180.0);
}

// ════════════════════════════════════════════════════════
// Tile cache LRU eviction
// ════════════════════════════════════════════════════════

class TileCacheTest : public ::testing::Test {
protected:
    CachedTile cache[TILE_CACHE_SIZE];
    uint64_t clock = 0;

    void SetUp() override {
        tile_cache_init(cache, TILE_CACHE_SIZE);
        clock = 0;
    }
};

TEST_F(TileCacheTest, InitClearsAllSlots) {
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        EXPECT_EQ(cache[i].pixels, nullptr);
        EXPECT_EQ(cache[i].last_used, 0U);
    }
}

TEST_F(TileCacheTest, InitIgnoresInvalidInputs) {
    cache[0].pixels = (uint16_t*)1;
    cache[0].last_used = 99;

    tile_cache_init(nullptr, TILE_CACHE_SIZE);
    tile_cache_init(cache, 0);
    tile_cache_init(cache, -1);

    EXPECT_EQ(cache[0].pixels, (uint16_t*)1);
    EXPECT_EQ(cache[0].last_used, 99U);
}

TEST_F(TileCacheTest, LookupEmptyCacheReturnsNull) {
    EXPECT_EQ(tile_cache_lookup(cache, TILE_CACHE_SIZE, 10, 512, 340, &clock), nullptr);
    EXPECT_EQ(clock, 0U); // clock not incremented on miss
}

TEST_F(TileCacheTest, LookupRejectsInvalidInputs) {
    cache[0].pixels = (uint16_t*)1;
    cache[0].zoom = 10;
    cache[0].tx = 512;
    cache[0].ty = 340;

    EXPECT_EQ(tile_cache_lookup(nullptr, TILE_CACHE_SIZE, 10, 512, 340, &clock), nullptr);
    EXPECT_EQ(tile_cache_lookup(cache, 0, 10, 512, 340, &clock), nullptr);
    EXPECT_EQ(tile_cache_lookup(cache, -1, 10, 512, 340, &clock), nullptr);
    EXPECT_EQ(tile_cache_lookup(cache, TILE_CACHE_SIZE, 10, 512, 340, nullptr), nullptr);
    EXPECT_EQ(clock, 0U);
}

TEST_F(TileCacheTest, LookupReturnsEntryOnHit) {
    cache[0].pixels = (uint16_t*)1; // non-null magic pointer
    cache[0].zoom = 10;
    cache[0].tx = 512;
    cache[0].ty = 340;

    CachedTile* result = tile_cache_lookup(cache, TILE_CACHE_SIZE, 10, 512, 340, &clock);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result, &cache[0]);
    EXPECT_EQ(clock, 1U); // clock incremented
}

TEST_F(TileCacheTest, LookupUpdatesLastUsed) {
    cache[1].pixels = (uint16_t*)1;
    cache[1].zoom = 10;
    cache[1].tx = 512;
    cache[1].ty = 340;

    tile_cache_lookup(cache, TILE_CACHE_SIZE, 10, 512, 340, &clock);
    EXPECT_EQ(cache[1].last_used, clock);
}

TEST_F(TileCacheTest, LookupNoMatchReturnsNull) {
    cache[0].pixels = (uint16_t*)1;
    cache[0].zoom = 10;
    cache[0].tx = 512;
    cache[0].ty = 340;

    // Different coordinates
    EXPECT_EQ(tile_cache_lookup(cache, TILE_CACHE_SIZE, 10, 511, 340, &clock), nullptr);
    EXPECT_EQ(clock, 0U);
}

TEST_F(TileCacheTest, EvictReturnsEmptySlotFirst) {
    cache[2].pixels = (uint16_t*)1;
    cache[2].zoom = 10;
    cache[2].tx = 512;
    cache[2].ty = 340;

    // Slot 2 is occupied, but slot 0 is empty — should return slot 0
    CachedTile* slot = tile_cache_evict_slot(cache, TILE_CACHE_SIZE);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot, &cache[0]);
    EXPECT_EQ(slot->pixels, nullptr);
}

TEST_F(TileCacheTest, EvictRejectsInvalidInputs) {
    EXPECT_EQ(tile_cache_evict_slot(nullptr, TILE_CACHE_SIZE), nullptr);
    EXPECT_EQ(tile_cache_evict_slot(cache, 0), nullptr);
    EXPECT_EQ(tile_cache_evict_slot(cache, -1), nullptr);
}

TEST_F(TileCacheTest, EvictReturnsFirstNullAmongOccupied) {
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        cache[i].pixels = (uint16_t*)1;
    }
    cache[1].pixels = nullptr; // second slot empty
    cache[3].pixels = nullptr; // fourth slot empty

    CachedTile* slot = tile_cache_evict_slot(cache, TILE_CACHE_SIZE);
    // Should return slot 1 (first null), not 3
    EXPECT_EQ(slot, &cache[1]);
}

TEST_F(TileCacheTest, EvictFullCacheReturnsLRU) {
    // Fill all slots with ascending last_used
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        cache[i].pixels = (uint16_t*)(intptr_t)(i + 1);
        cache[i].zoom = 10;
        cache[i].tx = i;
        cache[i].ty = 0;
        cache[i].last_used = (uint64_t)(i * 10); // 0, 10, 20, 30
    }

    CachedTile* slot = tile_cache_evict_slot(cache, TILE_CACHE_SIZE);
    ASSERT_NE(slot, nullptr);
    // LRU is slot 0 with last_used = 0
    EXPECT_EQ(slot, &cache[0]);
    EXPECT_EQ(slot->last_used, 0U);
}

TEST_F(TileCacheTest, EvictFullCacheReturnsLRUAfterLookups) {
    // Populate all slots
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        cache[i].pixels = (uint16_t*)(intptr_t)(i + 1);
        cache[i].zoom = 10;
        cache[i].tx = i;
        cache[i].ty = 0;
    }
    // Establish initial LRU order via lookups: 1, 2, 3, 4
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        tile_cache_lookup(cache, TILE_CACHE_SIZE, 10, i, 0, &clock);
    }
    // Now: cache[0].last_used=1, [1]=2, [2]=3, [3]=4

    // Look up slot 0 again — makes it MRU (clock=5)
    tile_cache_lookup(cache, TILE_CACHE_SIZE, 10, 0, 0, &clock);
    // Now: cache[0].last_used=5, [1]=2, [2]=3, [3]=4

    // LRU is slot 1 (last_used=2)
    CachedTile* slot = tile_cache_evict_slot(cache, TILE_CACHE_SIZE);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot, &cache[1]);
    EXPECT_EQ(slot->last_used, 2U);
}

TEST_F(TileCacheTest, EvictDoesNotFreeMemory) {
    // Fill all slots
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        cache[i].pixels = (uint16_t*)(intptr_t)(i + 1);
        cache[i].last_used = (uint64_t)i;
    }

    CachedTile* slot = tile_cache_evict_slot(cache, TILE_CACHE_SIZE);
    ASSERT_NE(slot, nullptr);
    // Evict should NOT have freed the pixels or set them to nullptr
    // That's the caller's responsibility
    EXPECT_NE(slot->pixels, nullptr);
}

TEST_F(TileCacheTest, ClearReleasesEveryOwnedBufferAndResetsEntries) {
    for (int i = 0; i < TILE_CACHE_SIZE; ++i) {
        cache[i] = {i + 1, i + 2, i + 3,
                    reinterpret_cast<uint16_t*>(static_cast<intptr_t>(i + 1)),
                    static_cast<uint32_t>(10 + i)};
    }

    int releases = 0;
    tile_cache_clear(cache, TILE_CACHE_SIZE,
                     [&releases](uint16_t*) { ++releases; });

    EXPECT_EQ(releases, TILE_CACHE_SIZE);
    for (const auto& entry : cache) {
        EXPECT_EQ(entry.pixels, nullptr);
        EXPECT_EQ(entry.last_used, 0U);
    }
}

TEST_F(TileCacheTest, PrepareSlotReusesVictimAllocation) {
    uint16_t old_pixel = 0;
    uint16_t replacement_pixel = 0;
    CachedTile slot = {10, 12, 34, &old_pixel, 99};
    int allocations = 0;

    uint16_t* replacement = tile_cache_prepare_slot(
        &slot, sizeof(uint16_t),
        [&allocations, &replacement_pixel](std::size_t) -> void* {
            allocations++;
            return &replacement_pixel;
        });

    EXPECT_EQ(allocations, 0);
    EXPECT_EQ(replacement, &old_pixel);
    EXPECT_EQ(slot.pixels, &old_pixel);
    EXPECT_EQ(slot.last_used, 0U);
}

TEST_F(TileCacheTest, PrepareEmptySlotAllocatesOnce) {
    uint16_t replacement_pixel = 0;
    CachedTile slot = {10, 12, 34, nullptr, 99};
    int allocations = 0;

    uint16_t* replacement = tile_cache_prepare_slot(
        &slot, 128,
        [&allocations, &replacement_pixel](std::size_t) -> void* {
            allocations++;
            return &replacement_pixel;
        });

    EXPECT_EQ(replacement, &replacement_pixel);
    EXPECT_EQ(slot.pixels, &replacement_pixel);
    EXPECT_EQ(slot.last_used, 0U);
    EXPECT_EQ(allocations, 1);
}

TEST_F(TileCacheTest, SearchSmallerCache) {
    // Test with a smaller cache size
    CachedTile small_cache[2];
    tile_cache_init(small_cache, 2);
    uint64_t small_clock = 0;

    small_cache[0].pixels = (uint16_t*)1;
    small_cache[0].zoom = 5;
    small_cache[0].tx = 10;
    small_cache[0].ty = 20;

    small_cache[1].pixels = (uint16_t*)2;
    small_cache[1].zoom = 5;
    small_cache[1].tx = 11;
    small_cache[1].ty = 20;

    // Establish LRU order via lookups: slot 0 (1), slot 1 (2)
    tile_cache_lookup(small_cache, 2, 5, 10, 20, &small_clock);
    tile_cache_lookup(small_cache, 2, 5, 11, 20, &small_clock);
    // Now: cache[0].last_used=1, cache[1].last_used=2

    // Lookup miss — no clock increment
    EXPECT_EQ(tile_cache_lookup(small_cache, 2, 5, 10, 21, &small_clock), nullptr);
    EXPECT_EQ(small_clock, 2U);

    // Lookup hit on slot 0 — makes it MRU (clock=3)
    CachedTile* hit = tile_cache_lookup(small_cache, 2, 5, 10, 20, &small_clock);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit, &small_cache[0]);
    EXPECT_EQ(small_clock, 3U);
    EXPECT_EQ(small_cache[0].last_used, 3U);

    // Now: slot 0 last_used=3 (MRU), slot 1 last_used=2 (LRU)
    // Evict → should pick slot 1
    CachedTile* evicted = tile_cache_evict_slot(small_cache, 2);
    EXPECT_EQ(evicted, &small_cache[1]);
}

// ════════════════════════════════════════════════════════
// Missing tile cache + per-render load budget
// ════════════════════════════════════════════════════════

TEST(MissingTileCacheTest, HitExpiresAfterBoundedTtl) {
    MissingTileCacheEntry cache[3];
    missing_tile_cache_init(cache, 3);
    missing_tile_cache_record(cache, 3, 10, 512, 340, 1000);

    EXPECT_TRUE(missing_tile_cache_contains(cache, 3, 10, 512, 340,
                                            1001, 30000));
    EXPECT_EQ(1, missing_tile_cache_active_count(cache, 3, 2000, 30000));
    EXPECT_FALSE(missing_tile_cache_contains(cache, 3, 10, 512, 340,
                                             31000, 30000));
    EXPECT_EQ(0, missing_tile_cache_active_count(cache, 3, 31000, 30000));
}

TEST(MissingTileCacheTest, ExpiryMathHandlesMillisWrap) {
    MissingTileCacheEntry cache[1];
    missing_tile_cache_init(cache, 1);
    missing_tile_cache_record(cache, 1, 5, 1, 2, UINT32_MAX - 100);

    EXPECT_TRUE(missing_tile_cache_contains(cache, 1, 5, 1, 2, 50, 200));
    EXPECT_FALSE(missing_tile_cache_contains(cache, 1, 5, 1, 2, 100, 200));
}

TEST(MissingTileCacheTest, FullCacheEvictsOldestFailure) {
    MissingTileCacheEntry cache[2];
    missing_tile_cache_init(cache, 2);
    missing_tile_cache_record(cache, 2, 1, 1, 1, 100);
    missing_tile_cache_record(cache, 2, 1, 2, 2, 200);
    missing_tile_cache_record(cache, 2, 1, 3, 3, 300);

    EXPECT_FALSE(missing_tile_cache_contains(cache, 2, 1, 1, 1, 301, 1000));
    EXPECT_TRUE(missing_tile_cache_contains(cache, 2, 1, 2, 2, 301, 1000));
    EXPECT_TRUE(missing_tile_cache_contains(cache, 2, 1, 3, 3, 301, 1000));
}

TEST(MissingTileCacheTest, DuplicateFailureRefreshesTimestampWithoutGrowing) {
    MissingTileCacheEntry cache[2];
    missing_tile_cache_init(cache, 2);
    missing_tile_cache_record(cache, 2, 1, 2, 3, 100);
    missing_tile_cache_record(cache, 2, 1, 2, 3, 500);

    EXPECT_EQ(1, missing_tile_cache_active_count(cache, 2, 600, 1000));
    EXPECT_TRUE(missing_tile_cache_contains(cache, 2, 1, 2, 3, 1400, 1000));
}

TEST(TileLoadBudgetTest, AttemptsNeverExceedConfiguredLimit) {
    TileLoadBudget budget(MAP_TILE_LOAD_BUDGET_PER_RENDER);
    EXPECT_EQ(2, budget.remaining());
    EXPECT_TRUE(budget.consume());
    EXPECT_TRUE(budget.consume());
    EXPECT_FALSE(budget.consume());
    EXPECT_EQ(2, budget.used);
    EXPECT_EQ(0, budget.remaining());

    TileLoadBudget disabled(0);
    EXPECT_FALSE(disabled.consume());
}

} // anonymous namespace
