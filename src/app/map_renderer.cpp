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


#include "map_renderer.h"
#include "tile_cache.h"
#include "../hal/tdeck_pins.h"
#include "../hal/sdcard.h"
#include "../hal/boot_watchdog.h"
#include <Arduino.h>
#include "../hal/prefs.h"
#include <lvgl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <cstdarg>
#include <strings.h>
#include <lodepng.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "../diagnostics/debug_cfg.h"
#include "../mesh/mesh_wrapper.h"
#include "../ui/theme.h"
#include "../hal/gps.h"

// Forward declare — the map screen sets this via sigurdos_map_contact_set_tap_cb
static map_contact_tap_cb_t g_contact_tap_cb = nullptr;

void sigurdos_map_contact_set_tap_cb(map_contact_tap_cb_t cb) {
    g_contact_tap_cb = cb;
}

extern void lodepng_free(void* ptr);

// ── Constants ─────────────────────────────────────────────
static constexpr int TILE_SIZE    = SIGURDOS_MAP_TILE_SIZE;
static constexpr int MAX_ZOOM     = SIGURDOS_MAP_MAX_ZOOM;
static constexpr int MIN_ZOOM     = SIGURDOS_MAP_MIN_ZOOM;
static constexpr double MAX_LAT   = SIGURDOS_MAP_MAX_LAT;
static constexpr double MIN_LAT   = SIGURDOS_MAP_MIN_LAT;
static constexpr double MAX_LON   = SIGURDOS_MAP_MAX_LON;
static constexpr double MIN_LON   = SIGURDOS_MAP_MIN_LON;

static lv_obj_t* map_canvas = nullptr;
static uint8_t*   canvas_pixels = nullptr;
static void render_own_position();

static double center_lat = 51.5074;  // London (fallback default)
static double center_lon = -0.1278;
static int    zoom_level = 10;
static bool   initialized = false;

struct TileCoverage {
    bool valid;
    bool wraps_x;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int sample_x;
    int sample_y;
};

static TileCoverage tile_coverage[MAX_ZOOM + 1];
static int min_available_zoom = MIN_ZOOM;
static int max_available_zoom = MAX_ZOOM;
static bool have_tile_coverage = false;
#if SIGURDOS_DEBUG_MAP
#define SIGURDOS_MAP_DIAGNOSTICS 1
#define MAP_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define MAP_DEBUG_PRINTLN(msg) Serial.println(msg)
#else
#define SIGURDOS_MAP_DIAGNOSTICS 0
#define MAP_DEBUG_PRINTF(...) do {} while (0)
#define MAP_DEBUG_PRINTLN(msg) do {} while (0)
#endif



// ── Web Mercator helpers ──────────────────────────────────
static double lon_to_tile_x(double lon, int z) {
    return sigurdos_map_lon_to_tile_x(lon, z);
}

static double lat_to_tile_y(double lat, int z) {
    return sigurdos_map_lat_to_tile_y(lat, z);
}

static double tile_x_to_lon(double tx, int z) {
    return sigurdos_map_tile_x_to_lon(tx, z);
}

static double tile_y_to_lat(double ty, int z) {
    return sigurdos_map_tile_y_to_lat(ty, z);
}

static int clamp(int val, int lo, int hi) {
    return sigurdos_map_clamp_int(val, lo, hi);
}

static double clamp_d(double val, double lo, double hi) {
    return sigurdos_map_clamp_double(val, lo, hi);
}

static void* map_alloc(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p && sigurdos_map_internal_fallback_allowed(size)) {
        p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return p;
}

static void map_free(void* p) {
    heap_caps_free(p);
}

#if SIGURDOS_MAP_DIAGNOSTICS
static void appendf(char* out, size_t out_sz, size_t* pos, const char* fmt, ...) {
    if (!out || out_sz == 0 || !pos || *pos >= out_sz) return;

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(out + *pos, out_sz - *pos, fmt, args);
    va_end(args);

    if (written < 0) return;
    size_t n = (size_t)written;
    if (n >= out_sz - *pos) {
        *pos = out_sz - 1;
        out[*pos] = '\0';
    } else {
        *pos += n;
    }
}

static char last_tile_status[128] = "load:not tried";

static void set_tile_status(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_tile_status, sizeof(last_tile_status), fmt, args);
    va_end(args);
}
#else
static void set_tile_status(const char*, ...) {}
#endif

static void reset_tile_coverage() {
    for (int i = MIN_ZOOM; i <= MAX_ZOOM; i++) {
        tile_coverage[i] = {false, false, 0, 0, 0, 0, 0, 0};
    }
    min_available_zoom = MIN_ZOOM;
    max_available_zoom = MAX_ZOOM;
    have_tile_coverage = false;
}

static void apply_preset_default_view() {
    const sigurdos::NodePrefs& prefs = sigurdos::prefs_get();
    const SigurdosMapDefaultView view =
        sigurdos_map_default_view_for_radio_profile(prefs.radio_profile);
    // Sentinel zoom of -1 means "no regional override — keep current center"
    if (view.zoom < 0) return;
    center_lat = clamp_d(view.lat, MIN_LAT, MAX_LAT);
    center_lon = sigurdos_map_wrap_lon(view.lon);
    zoom_level = clamp(view.zoom, MIN_ZOOM, MAX_ZOOM);
}

static void clamp_view_to_coverage() {
    if (!have_tile_coverage) return;

    zoom_level = sigurdos_map_select_available_zoom(
        zoom_level, 0, min_available_zoom, max_available_zoom,
        [](int zoom) { return tile_coverage[zoom].valid; });
    const TileCoverage& c = tile_coverage[zoom_level];
    if (!c.valid) return;

    double tx = lon_to_tile_x(center_lon, zoom_level);
    double ty = lat_to_tile_y(center_lat, zoom_level);

    double half_w_tiles = (TFT_WIDTH / 2.0) / TILE_SIZE;
    double half_h_tiles = (TFT_HEIGHT / 2.0) / TILE_SIZE;
    const int n = sigurdos_map_tiles_per_axis(zoom_level);
    const double coverage_width =
        sigurdos_map_wrap_tile_x((double)c.max_x - c.min_x, zoom_level) + 1.0;
    double min_tx = half_w_tiles;
    double max_tx = coverage_width - half_w_tiles;
    double min_ty = c.min_y + half_h_tiles;
    double max_ty = c.max_y + 1.0 - half_h_tiles;

    if (min_tx > max_tx) {
        tx = c.min_x + coverage_width / 2.0;
    } else {
        const double relative_tx =
            sigurdos_map_wrap_tile_x(tx - c.min_x, zoom_level);
        const double candidates[] = {
            relative_tx - n, relative_tx, relative_tx + n
        };
        double best_tx = clamp_d(candidates[0], min_tx, max_tx);
        double best_distance = fabs(candidates[0] - best_tx);
        for (int i = 1; i < 3; ++i) {
            const double candidate = clamp_d(candidates[i], min_tx, max_tx);
            const double distance = fabs(candidates[i] - candidate);
            if (distance < best_distance) {
                best_tx = candidate;
                best_distance = distance;
            }
        }
        tx = c.min_x + best_tx;
    }

    if (min_ty > max_ty) {
        ty = (c.min_y + c.max_y + 1.0) / 2.0;
    } else {
        ty = clamp_d(ty, min_ty, max_ty);
    }

    center_lon = tile_x_to_lon(tx, zoom_level);
    center_lat = tile_y_to_lat(ty, zoom_level);
    center_lat = clamp_d(center_lat, MIN_LAT, MAX_LAT);
    center_lon = sigurdos_map_wrap_lon(center_lon);
}

// ── Tile cache (LRU, 4 tiles = ~524KB PSRAM) ─────────────
// Types and functions defined in tile_cache.h / tile_cache.cpp
static CachedTile tile_cache[TILE_CACHE_SIZE];
static uint64_t   cache_clock = 0;
static MissingTileCacheEntry missing_tile_cache[MISSING_TILE_CACHE_SIZE];
static int last_load_attempts = 0;
static int last_negative_hits = 0;
static int last_deferred_tiles = 0;

enum class TileLoadResult : uint8_t { Ready, Missing, Deferred };

// ── Tile loading (PNG) ─────────────────────────────────────

static TileLoadResult record_tile_failure(int zoom, int tx, int ty,
                                          uint32_t now_ms) {
    missing_tile_cache_record(missing_tile_cache, MISSING_TILE_CACHE_SIZE,
                              zoom, tx, ty, now_ms);
    return TileLoadResult::Missing;
}

static TileLoadResult load_tile(int zoom, int tx, int ty,
                                TileLoadBudget* budget, uint32_t now_ms,
                                CachedTile** out) {
    if (out) *out = nullptr;
    CachedTile* cached = tile_cache_lookup(
        tile_cache, TILE_CACHE_SIZE, zoom, tx, ty, &cache_clock);
    if (cached) {
        set_tile_status("load:cache %d/%d/%d", zoom, tx, ty);
        if (out) *out = cached;
        return TileLoadResult::Ready;
    }
    if (missing_tile_cache_contains(
            missing_tile_cache, MISSING_TILE_CACHE_SIZE, zoom, tx, ty,
            now_ms, MISSING_TILE_CACHE_TTL_MS)) {
        last_negative_hits++;
        set_tile_status("load:negative %d/%d/%d", zoom, tx, ty);
        return TileLoadResult::Missing;
    }
    if (!budget || !budget->consume()) {
        last_deferred_tiles++;
        set_tile_status("load:budget %d/%d/%d", zoom, tx, ty);
        return TileLoadResult::Deferred;
    }

    char path[64];
    snprintf(path, sizeof(path), SIGURDOS_SD_MOUNTPOINT "/tiles/%d/%d/%d.png", zoom, tx, ty);

    FILE* f = fopen(path, "rb");
    if (!f) {
        MAP_DEBUG_PRINTF("[map] tile miss: %s\n", path);
        set_tile_status("load:fopen fail %d/%d/%d", zoom, tx, ty);
        return record_tile_failure(zoom, tx, ty, now_ms);
    }
    MAP_DEBUG_PRINTF("[map] tile hit: %s\n", path);

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 ||
        static_cast<unsigned long>(fsize) > SIGURDOS_MAP_PNG_MAX_COMPRESSED_BYTES) {
        set_tile_status("load:size %ld %d/%d/%d", fsize, zoom, tx, ty);
        fclose(f); return record_tile_failure(zoom, tx, ty, now_ms);
    }

    uint8_t* png_buf = (uint8_t*)map_alloc((size_t)fsize);
    if (!png_buf) {
        set_tile_status("load:no png buf %ld", fsize);
        fclose(f); return record_tile_failure(zoom, tx, ty, now_ms);
    }

    if (fread(png_buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        set_tile_status("load:read fail %ld", fsize);
        fclose(f); map_free(png_buf);
        return record_tile_failure(zoom, tx, ty, now_ms);
    }
    fclose(f);

    unsigned w = 0;
    unsigned h = 0;
    LodePNGState header_state;
    lodepng_state_init(&header_state);
    const unsigned header_err = lodepng_inspect(
        &w, &h, &header_state, png_buf, (size_t)fsize);
    const unsigned source_bit_depth = header_state.info_png.color.bitdepth;
    const unsigned source_color_type = header_state.info_png.color.colortype;
    const bool header_supported = header_err == 0 &&
        sigurdos_map_png_ihdr_supported(png_buf, (size_t)fsize);
    lodepng_state_cleanup(&header_state);

    if (!header_supported) {
        set_tile_status("load:ihdr err %u %ux%u b%u c%u",
                        header_err, w, h, source_bit_depth, source_color_type);
        map_free(png_buf);
        return record_tile_failure(zoom, tx, ty, now_ms);
    }

    uint8_t* rgba = nullptr;
    LodePNGState decode_state;
    lodepng_state_init(&decode_state);
    decode_state.info_raw.colortype = LCT_RGBA;
    decode_state.info_raw.bitdepth = 8;
    decode_state.decoder.zlibsettings.max_output_size =
        SIGURDOS_MAP_PNG_MAX_DECOMPRESSED_BYTES;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
    decode_state.decoder.read_text_chunks = 0;
    decode_state.decoder.remember_unknown_chunks = 0;
#endif
    unsigned err = lodepng_decode(
        &rgba, &w, &h, &decode_state, png_buf, (size_t)fsize);
    lodepng_state_cleanup(&decode_state);
    map_free(png_buf);

    if (err != 0 || w != TILE_SIZE || h != TILE_SIZE) {
        set_tile_status("load:png err %u %ux%u", err, w, h);
        lodepng_free(rgba);
        return record_tile_failure(zoom, tx, ty, now_ms);
    }

    // Select and release the victim before requesting another full tile
    // buffer. A full cache therefore never needs a transient fifth RGB565
    // allocation, which keeps PSRAM pressure from spilling into internal RAM.
    CachedTile* slot = tile_cache_evict_slot(tile_cache, TILE_CACHE_SIZE);
    if (!slot) {
        set_tile_status("load:no cache slot");
        lodepng_free(rgba);
        return record_tile_failure(zoom, tx, ty, now_ms);
    }
    uint16_t* decoded_pixels = tile_cache_prepare_slot(
        slot, TILE_SIZE * TILE_SIZE * sizeof(uint16_t), map_alloc);
    if (!decoded_pixels) {
        set_tile_status("load:no tile buf");
        lodepng_free(rgba);
        return record_tile_failure(zoom, tx, ty, now_ms);
    }

    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++) {
            int i = (y * TILE_SIZE + x) * 4;
            uint16_t r = rgba[i] >> 3;
            uint16_t g = rgba[i + 1] >> 2;
            uint16_t b = rgba[i + 2] >> 3;
            decoded_pixels[y * TILE_SIZE + x] = (r << 11) | (g << 5) | b;
        }
    }

    lodepng_free(rgba);

    slot->zoom = zoom;
    slot->tx = tx;
    slot->ty = ty;
    slot->last_used = ++cache_clock;
    set_tile_status("load:ok %d/%d/%d %ldB", zoom, tx, ty, fsize);
    if (out) *out = slot;
    return TileLoadResult::Ready;
}

// ── Draw tile to canvas buffer ─────────────────────────────

static void draw_tile_from_cache(CachedTile* tile, int screen_x, int screen_y) {
    if (!tile || !tile->pixels || !canvas_pixels) return;

    // Clamp to screen bounds
    int x1 = screen_x;
    int y1 = screen_y;
    int x2 = screen_x + TILE_SIZE - 1;
    int y2 = screen_y + TILE_SIZE - 1;
    int src_x = 0;
    int src_y = 0;
    int src_w = TILE_SIZE;
    int src_h = TILE_SIZE;

    if (x1 < 0) { src_x -= x1; src_w += x1; x1 = 0; }
    if (y1 < 0) { src_y -= y1; src_h += y1; y1 = 0; }
    if (x2 >= TFT_WIDTH)  { src_w -= (x2 - TFT_WIDTH + 1);  x2 = TFT_WIDTH - 1; }
    if (y2 >= TFT_HEIGHT) { src_h -= (y2 - TFT_HEIGHT + 1); y2 = TFT_HEIGHT - 1; }
    if (src_w <= 0 || src_h <= 0) return;
    if (src_x < 0) src_x = 0;
    if (src_y < 0) src_y = 0;

    uint16_t* dst = (uint16_t*)canvas_pixels;
    for (int row = 0; row < src_h; row++) {
        uint16_t* dst_row = dst + (size_t)(y1 + row) * TFT_WIDTH + x1;
        const uint16_t* src_row = tile->pixels + (size_t)(src_y + row) * TILE_SIZE + src_x;
        memcpy(dst_row, src_row, (size_t)src_w * sizeof(uint16_t));
    }
}

// ── Tile discovery (auto-center from SD card contents) ────

static bool is_decimal_name(const char* name) {
    if (!name || !*name) return false;
    for (const char* p = name; *p; p++) {
        if (*p < '0' || *p > '9') return false;
    }
    return true;
}

static bool entry_is_png_tile(const struct dirent* e) {
    if (!e || e->d_name[0] == '.') return false;
    const char* ext = strrchr(e->d_name, '.');
    if (!ext || strcasecmp(ext, ".png") != 0) return false;

    // Require a numeric y basename; "12.png" is accepted, "12@2x.png" is not.
    size_t stem_len = (size_t)(ext - e->d_name);
    if (stem_len == 0) return false;
    for (size_t i = 0; i < stem_len; i++) {
        if (e->d_name[i] < '0' || e->d_name[i] > '9') return false;
    }
    return true;
}

static bool scan_y_range(int zoom, int x, int* min_y, int* max_y, int* sample_y) {
    if (!min_y || !max_y || !sample_y) return false;

    char y_path[64];
    snprintf(y_path, sizeof(y_path), SIGURDOS_SD_MOUNTPOINT "/tiles/%d/%d", zoom, x);

    DIR* yd = opendir(y_path);
    if (!yd) return false;

    int mn_y = -1;
    int mx_y = -1;
    struct dirent* ye;
    while ((ye = readdir(yd)) != nullptr) {
        if (!entry_is_png_tile(ye)) continue;
        const char* ext = strrchr(ye->d_name, '.');
        int y = -1;
        if (!ext || !sigurdos_map_parse_tile_index(
                        ye->d_name, static_cast<size_t>(ext - ye->d_name),
                        zoom, &y)) {
            continue;
        }
        if (mn_y < 0) {
            mn_y = mx_y = y;
            *sample_y = y;
        } else {
            if (y < mn_y) mn_y = y;
            if (y > mx_y) mx_y = y;
        }
    }
    closedir(yd);

    if (mn_y < 0) return false;
    *min_y = mn_y;
    *max_y = mx_y;
    return true;
}

// Cached per-X result used for bounded coverage and sample selection after the
// single Y-directory pass. The cache lives in PSRAM rather than loopTask stack.
struct XColCache {
    int x;
    int min_y;
    int max_y;
    int sample_y;
    bool valid;
};

static constexpr int MAX_XCOLS = 2048;
static XColCache* discovery_xcache = nullptr;

enum class DiscoveryPhase : uint8_t {
    Idle,
    CheckStorage,
    TryIndex,
    OpenZoom,
    ScanX,
    OpenY,
    ScanY,
    RecordColumn,
    SortColumns,
    DeduplicateColumns,
    FindLargestGap,
    PrepareSample,
    SelectSample,
    FinishZoom,
    FinishAll,
};

struct DiscoveryState {
    DiscoveryPhase phase = DiscoveryPhase::Idle;
    SigurdosMapDiscoveryResult result = SigurdosMapDiscoveryResult::Idle;
    int zoom = MIN_ZOOM;
    DIR* zoom_dir = nullptr;
    DIR* y_dir = nullptr;
    int current_x = -1;
    int min_y = -1;
    int max_y = -1;
    int sample_y = -1;
    TileCoverage coverage = {false, false, 0, 0, 0, 0, 0, 0};
    int xcache_count = 0;
    bool cache_overflow = false;
    int index = 0;
    int unique_count = 0;
    int largest_gap = -1;
    double mid_x = 0.0;
    double mid_y = 0.0;
    double best_distance = 0.0;
    bool have_sample = false;
    bool stop_after_zoom = false;
    uint32_t entries_processed = 0;
    bool used_index = false;
};

static DiscoveryState discovery;

static void close_discovery_directories() {
    if (discovery.y_dir) {
        closedir(discovery.y_dir);
        discovery.y_dir = nullptr;
    }
    if (discovery.zoom_dir) {
        closedir(discovery.zoom_dir);
        discovery.zoom_dir = nullptr;
    }
}

static void reset_discovery_state() {
    close_discovery_directories();
    discovery = DiscoveryState{};
}

static void record_discovery_column() {
    TileCoverage& coverage = discovery.coverage;
    const int x = discovery.current_x;
    if (!coverage.valid) {
        coverage = {true, false, x, x, discovery.min_y, discovery.max_y,
                    x, discovery.sample_y};
    } else {
        coverage.min_x = std::min(coverage.min_x, x);
        coverage.max_x = std::max(coverage.max_x, x);
        coverage.min_y = std::min(coverage.min_y, discovery.min_y);
        coverage.max_y = std::max(coverage.max_y, discovery.max_y);
    }

    if (discovery.xcache_count < MAX_XCOLS) {
        discovery_xcache[discovery.xcache_count++] = {
            x, discovery.min_y, discovery.max_y, discovery.sample_y, true};
    } else {
        discovery.cache_overflow = true;
    }
}

static void commit_discovery_zoom() {
    if (!discovery.coverage.valid || !discovery.have_sample) return;
    tile_coverage[discovery.zoom] = discovery.coverage;
    if (!have_tile_coverage) {
        min_available_zoom = max_available_zoom = discovery.zoom;
        have_tile_coverage = true;
    } else {
        min_available_zoom = std::min(min_available_zoom, discovery.zoom);
        max_available_zoom = std::max(max_available_zoom, discovery.zoom);
    }
}

static bool read_index_integer(JsonObjectConst object, const char* key,
                               int* out) {
    JsonVariantConst value = object[key];
    if (!out || !value.is<int>()) return false;
    *out = value.as<int>();
    return true;
}

static bool load_tile_index() {
    FILE* file = fopen(SIGURDOS_SD_MOUNTPOINT "/tiles/index.json", "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const long file_size = ftell(file);
    if (file_size <= 0 ||
        static_cast<unsigned long>(file_size) > SIGURDOS_MAP_INDEX_MAX_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    char* input = static_cast<char*>(
        map_alloc(static_cast<size_t>(file_size) + 1));
    if (!input) {
        fclose(file);
        return false;
    }
    const size_t bytes_read = fread(
        input, 1, static_cast<size_t>(file_size), file);
    fclose(file);
    if (bytes_read != static_cast<size_t>(file_size)) {
        map_free(input);
        return false;
    }
    input[bytes_read] = '\0';

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, input, bytes_read);
    map_free(input);
    if (error) return false;

    JsonObjectConst root = document.as<JsonObjectConst>();
    JsonVariantConst version = root["version"];
    JsonVariantConst tile_size = root["tile_size"];
    JsonArrayConst zooms = root["zooms"].as<JsonArrayConst>();
    if (!version.is<int>() || version.as<int>() != 1 ||
        !tile_size.is<int>() || tile_size.as<int>() != TILE_SIZE ||
        zooms.isNull() || zooms.size() == 0 || zooms.size() > MAX_ZOOM + 1) {
        return false;
    }

    TileCoverage indexed[MAX_ZOOM + 1] = {};
    bool seen[MAX_ZOOM + 1] = {};
    int indexed_min = MAX_ZOOM;
    int indexed_max = MIN_ZOOM;
    for (JsonObjectConst object : zooms) {
        SigurdosMapTileIndexEntry entry{};
        if (!read_index_integer(object, "z", &entry.zoom) ||
            !read_index_integer(object, "min_x", &entry.min_x) ||
            !read_index_integer(object, "max_x", &entry.max_x) ||
            !read_index_integer(object, "min_y", &entry.min_y) ||
            !read_index_integer(object, "max_y", &entry.max_y) ||
            !read_index_integer(object, "sample_x", &entry.sample_x) ||
            !read_index_integer(object, "sample_y", &entry.sample_y)) {
            return false;
        }
        JsonVariantConst count = object["count"];
        if (!count.is<uint32_t>()) return false;
        entry.tile_count = count.as<uint32_t>();
        if (!sigurdos_map_tile_index_entry_valid(entry) || seen[entry.zoom]) {
            return false;
        }

        char sample_path[72];
        snprintf(sample_path, sizeof(sample_path),
                 SIGURDOS_SD_MOUNTPOINT "/tiles/%d/%d/%d.png",
                 entry.zoom, entry.sample_x, entry.sample_y);
        FILE* sample = fopen(sample_path, "rb");
        if (!sample) return false;
        fclose(sample);

        seen[entry.zoom] = true;
        indexed[entry.zoom] = {true, false, entry.min_x, entry.max_x,
                               entry.min_y, entry.max_y,
                               entry.sample_x, entry.sample_y};
        indexed_min = std::min(indexed_min, entry.zoom);
        indexed_max = std::max(indexed_max, entry.zoom);
        ++discovery.entries_processed;
    }

    reset_tile_coverage();
    for (int zoom = indexed_min; zoom <= indexed_max; ++zoom) {
        if (seen[zoom]) tile_coverage[zoom] = indexed[zoom];
    }
    min_available_zoom = indexed_min;
    max_available_zoom = indexed_max;
    have_tile_coverage = true;
    discovery.used_index = true;
    return true;
}

static void apply_metadata_bounds() {
    FILE* file = fopen(SIGURDOS_SD_MOUNTPOINT "/tiles/metadata.json", "r");
    if (!file) return;
    char buffer[512];
    const size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    if (length == 0) return;
    buffer[length] = '\0';

    const char* cursor = strstr(buffer, "\"bounds\"");
    if (!cursor || !(cursor = strchr(cursor, '['))) return;
    ++cursor;
    double bounds[4];
    int count = 0;
    while (count < 4 && *cursor) {
        while (*cursor == ' ' || *cursor == ',' || *cursor == '\n') ++cursor;
        char* end = nullptr;
        bounds[count] = strtod(cursor, &end);
        if (end == cursor) break;
        cursor = end;
        ++count;
    }
    while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' ||
           *cursor == '\t') {
        ++cursor;
    }
    if (count == 4 && *cursor == ']' && sigurdos_map_bounds_valid(bounds)) {
        center_lat = (bounds[1] + bounds[3]) / 2.0;
        center_lon = (bounds[0] + bounds[2]) / 2.0;
        clamp_view_to_coverage();
    }
}

static void apply_fallback_view() {
    if (sigurdos_gps_has_fix()) {
        center_lat = sigurdos_gps_latitude();
        center_lon = sigurdos_gps_longitude();
        zoom_level = sigurdos_map_clamp_int(15, MIN_ZOOM, MAX_ZOOM);
        return;
    }
    const sigurdos::NodePrefs& prefs = sigurdos::prefs_get();
    if (prefs.radio_profile && prefs.radio_profile[0] != '\0') {
        apply_preset_default_view();
    }
}

static void finish_discovery() {
    close_discovery_directories();
    if (have_tile_coverage) {
        zoom_level = max_available_zoom;
        const TileCoverage& coverage = tile_coverage[zoom_level];
        center_lon = tile_x_to_lon(
            static_cast<double>(coverage.sample_x) + 0.5, zoom_level);
        center_lat = tile_y_to_lat(
            static_cast<double>(coverage.sample_y) + 0.5, zoom_level);
        clamp_view_to_coverage();
        apply_metadata_bounds();
        if (discovery.result == SigurdosMapDiscoveryResult::InProgress) {
            discovery.result = discovery.used_index
                ? SigurdosMapDiscoveryResult::IndexLoaded
                : SigurdosMapDiscoveryResult::Scanned;
        }
    } else {
        apply_fallback_view();
        if (discovery.result == SigurdosMapDiscoveryResult::InProgress) {
            discovery.result = SigurdosMapDiscoveryResult::NoTiles;
        }
    }
    discovery.phase = DiscoveryPhase::Idle;
    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Runtime);
}

static void stop_at_entry_limit() {
    discovery.result = SigurdosMapDiscoveryResult::LimitReached;
    discovery.stop_after_zoom = true;
    close_discovery_directories();
    discovery.phase = discovery.coverage.valid
        ? DiscoveryPhase::SortColumns
        : DiscoveryPhase::FinishAll;
}

static bool delete_cb_registered = false;

void sigurdos_map_init() {
    if (initialized) return;

    // Reset monotonic clock for fresh cache entries
    cache_clock = 0;
    missing_tile_cache_init(missing_tile_cache, MISSING_TILE_CACHE_SIZE);
    last_load_attempts = 0;
    last_negative_hits = 0;
    last_deferred_tiles = 0;

    // Allocate draw buffer (320×240×2 = 153KB for RGB565).
    // PSRAM first: DRAM is scarce (~320KB free) and a 153KB allocation there
    // stresses the heap. LVGL canvas draw ops work fine with PSRAM on ESP32-S3
    // (CPU-cacheable), matching the display's own draw buffer pattern.
    size_t buf_size = (size_t)TFT_WIDTH * TFT_HEIGHT * 2;
    canvas_pixels = (uint8_t*)heap_caps_malloc(
        buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!canvas_pixels) return;

    // Canvas is created later in sigurdos_map_reparent() once we have a real
    // screen parent — lv_canvas_create(nullptr) in LVGL v9 makes a screen
    // object, not an orphan widget, so reparenting it silently fails.
    //
    // Tile discovery is deferred to first map screen visit to avoid blocking
    // boot with large SD card tile sets (30-120s on 2GB /tiles dir).
    initialized = true;
}
bool sigurdos_map_initialized() { return initialized; }
void sigurdos_map_reparent(lv_obj_t* new_parent) {
    if (!initialized || !new_parent || !canvas_pixels) return;

    if (!map_canvas) {
        // Create canvas with the real screen as parent so LVGL treats it as
        // a regular widget (not a screen). NULL parent in LVGL v9 = screen.
        map_canvas = lv_canvas_create(new_parent);
    } else {
        lv_obj_set_parent(map_canvas, new_parent);
        lv_obj_move_to_index(map_canvas, 0);
    }

    lv_obj_set_size(map_canvas, TFT_WIDTH, TFT_HEIGHT);
    lv_obj_align(map_canvas, LV_ALIGN_CENTER, 0, 0);
    lv_canvas_set_buffer(map_canvas, canvas_pixels,
                         TFT_WIDTH, TFT_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_move_to_index(map_canvas, 0);

    if (!delete_cb_registered) {
        lv_obj_add_event_cb(new_parent, [](lv_event_t* e) {
            sigurdos_map_deinit();
        }, LV_EVENT_DELETE, nullptr);
        delete_cb_registered = true;
    }
}

void sigurdos_map_discover_tiles() {
    reset_discovery_state();
    reset_tile_coverage();
    sigurdos::hal::boot_watchdog_progress(
        sigurdos::hal::BootStage::MapDiscovery);
    tile_cache_clear(tile_cache, TILE_CACHE_SIZE, map_free);
    cache_clock = 0;
    missing_tile_cache_init(missing_tile_cache, MISSING_TILE_CACHE_SIZE);
    discovery.result = SigurdosMapDiscoveryResult::InProgress;
    discovery.phase = DiscoveryPhase::CheckStorage;
}

bool sigurdos_map_discovery_step(int max_items, uint32_t max_ms) {
    if (discovery.phase == DiscoveryPhase::Idle) return false;
    if (max_items <= 0 || max_ms == 0) return true;

    SigurdosMapDiscoveryBudget budget(max_items);
    const uint32_t started_at = millis();
    const auto consume_operation = [&]() {
        if (budget.used() > 0 && sigurdos_map_discovery_deadline_reached(
                started_at, millis(), max_ms)) {
            return false;
        }
        return budget.consume();
    };

    while (discovery.phase != DiscoveryPhase::Idle) {
        if (discovery.entries_processed >= SIGURDOS_MAP_DISCOVERY_MAX_ENTRIES &&
            (discovery.phase == DiscoveryPhase::ScanX ||
             discovery.phase == DiscoveryPhase::ScanY ||
             discovery.phase == DiscoveryPhase::OpenY)) {
            stop_at_entry_limit();
        }

        switch (discovery.phase) {
            case DiscoveryPhase::Idle:
                return false;

            case DiscoveryPhase::CheckStorage:
                if (!consume_operation()) return true;
                discovery.phase =
                    (sigurdos_sdcard_mounted() || sigurdos_sdcard_retry())
                        ? DiscoveryPhase::TryIndex
                        : DiscoveryPhase::FinishAll;
                break;

            case DiscoveryPhase::TryIndex:
                if (!consume_operation()) return true;
                if (load_tile_index()) {
                    discovery.zoom = max_available_zoom;
                    discovery.phase = DiscoveryPhase::FinishAll;
                    break;
                }
                if (!discovery_xcache) {
                    discovery_xcache = static_cast<XColCache*>(
                        map_alloc(sizeof(XColCache) * MAX_XCOLS));
                }
                discovery.phase = discovery_xcache
                    ? DiscoveryPhase::OpenZoom
                    : DiscoveryPhase::FinishAll;
                break;

            case DiscoveryPhase::OpenZoom: {
                if (discovery.zoom > MAX_ZOOM || discovery.stop_after_zoom) {
                    discovery.phase = DiscoveryPhase::FinishAll;
                    break;
                }
                if (!consume_operation()) return true;
                discovery.coverage = {false, false, 0, 0, 0, 0, 0, 0};
                discovery.xcache_count = 0;
                discovery.cache_overflow = false;
                discovery.have_sample = false;
                char path[48];
                snprintf(path, sizeof(path),
                         SIGURDOS_SD_MOUNTPOINT "/tiles/%d", discovery.zoom);
                discovery.zoom_dir = opendir(path);
                if (discovery.zoom_dir) {
                    discovery.phase = DiscoveryPhase::ScanX;
                } else {
                    ++discovery.zoom;
                }
                break;
            }

            case DiscoveryPhase::ScanX: {
                if (!consume_operation()) return true;
                struct dirent* entry = readdir(discovery.zoom_dir);
                if (!entry) {
                    closedir(discovery.zoom_dir);
                    discovery.zoom_dir = nullptr;
                    discovery.phase = discovery.coverage.valid
                        ? DiscoveryPhase::SortColumns
                        : DiscoveryPhase::FinishZoom;
                    break;
                }
                ++discovery.entries_processed;
                if (entry->d_name[0] == '.') break;
                int x = -1;
                if (!sigurdos_map_parse_tile_index(
                        entry->d_name, strlen(entry->d_name),
                        discovery.zoom, &x)) {
                    break;
                }
                discovery.current_x = x;
                discovery.min_y = -1;
                discovery.max_y = -1;
                discovery.sample_y = -1;
                discovery.phase = DiscoveryPhase::OpenY;
                break;
            }

            case DiscoveryPhase::OpenY: {
                if (!consume_operation()) return true;
                char path[64];
                snprintf(path, sizeof(path),
                         SIGURDOS_SD_MOUNTPOINT "/tiles/%d/%d",
                         discovery.zoom, discovery.current_x);
                discovery.y_dir = opendir(path);
                discovery.phase = discovery.y_dir
                    ? DiscoveryPhase::ScanY
                    : DiscoveryPhase::ScanX;
                break;
            }

            case DiscoveryPhase::ScanY: {
                if (!consume_operation()) return true;
                struct dirent* entry = readdir(discovery.y_dir);
                if (!entry) {
                    closedir(discovery.y_dir);
                    discovery.y_dir = nullptr;
                    discovery.phase = discovery.sample_y >= 0
                        ? DiscoveryPhase::RecordColumn
                        : DiscoveryPhase::ScanX;
                    break;
                }
                ++discovery.entries_processed;
                if (!entry_is_png_tile(entry)) break;
                const char* extension = strrchr(entry->d_name, '.');
                int y = -1;
                if (!extension || !sigurdos_map_parse_tile_index(
                        entry->d_name,
                        static_cast<size_t>(extension - entry->d_name),
                        discovery.zoom, &y)) {
                    break;
                }
                if (discovery.sample_y < 0) {
                    discovery.min_y = discovery.max_y = discovery.sample_y = y;
                } else {
                    discovery.min_y = std::min(discovery.min_y, y);
                    discovery.max_y = std::max(discovery.max_y, y);
                }
                break;
            }

            case DiscoveryPhase::RecordColumn:
                record_discovery_column();
                discovery.phase = DiscoveryPhase::ScanX;
                break;

            case DiscoveryPhase::SortColumns:
                if (!consume_operation()) return true;
                std::sort(discovery_xcache,
                          discovery_xcache + discovery.xcache_count,
                          [](const XColCache& left, const XColCache& right) {
                              return left.x < right.x;
                          });
                discovery.index = 0;
                discovery.unique_count = 0;
                discovery.phase = DiscoveryPhase::DeduplicateColumns;
                break;

            case DiscoveryPhase::DeduplicateColumns:
                if (discovery.index >= discovery.xcache_count) {
                    discovery.xcache_count = discovery.unique_count;
                    discovery.index = 0;
                    discovery.largest_gap = -1;
                    discovery.phase = discovery.cache_overflow
                        ? DiscoveryPhase::PrepareSample
                        : DiscoveryPhase::FindLargestGap;
                    break;
                }
                if (!consume_operation()) return true;
                if (discovery.unique_count == 0 ||
                    discovery_xcache[discovery.index].x !=
                        discovery_xcache[discovery.unique_count - 1].x) {
                    discovery_xcache[discovery.unique_count++] =
                        discovery_xcache[discovery.index];
                }
                ++discovery.index;
                break;

            case DiscoveryPhase::FindLargestGap: {
                if (discovery.index >= discovery.xcache_count) {
                    discovery.coverage.wraps_x =
                        discovery.coverage.min_x > discovery.coverage.max_x;
                    discovery.phase = DiscoveryPhase::PrepareSample;
                    break;
                }
                if (!consume_operation()) return true;
                const int next_x =
                    discovery.index + 1 < discovery.xcache_count
                        ? discovery_xcache[discovery.index + 1].x
                        : discovery_xcache[0].x +
                              sigurdos_map_tiles_per_axis(discovery.zoom);
                const int gap =
                    next_x - discovery_xcache[discovery.index].x - 1;
                if (gap > discovery.largest_gap) {
                    discovery.largest_gap = gap;
                    discovery.coverage.min_x = next_x %
                        sigurdos_map_tiles_per_axis(discovery.zoom);
                    discovery.coverage.max_x =
                        discovery_xcache[discovery.index].x;
                }
                ++discovery.index;
                break;
            }

            case DiscoveryPhase::PrepareSample: {
                const double x_span = sigurdos_map_wrap_tile_x(
                    static_cast<double>(discovery.coverage.max_x) -
                        discovery.coverage.min_x,
                    discovery.zoom);
                discovery.mid_x = sigurdos_map_wrap_tile_x(
                    discovery.coverage.min_x + x_span / 2.0,
                    discovery.zoom);
                discovery.mid_y =
                    (discovery.coverage.min_y + discovery.coverage.max_y) / 2.0;
                discovery.index = 0;
                discovery.have_sample = false;
                discovery.phase = DiscoveryPhase::SelectSample;
                break;
            }

            case DiscoveryPhase::SelectSample: {
                if (discovery.index >= discovery.xcache_count) {
                    discovery.phase = DiscoveryPhase::FinishZoom;
                    break;
                }
                if (!consume_operation()) return true;
                const XColCache& column =
                    discovery_xcache[discovery.index++];
                const double dx = sigurdos_map_shortest_tile_x_delta(
                    discovery.mid_x, static_cast<double>(column.x),
                    discovery.zoom);
                const double dy = column.sample_y - discovery.mid_y;
                const double distance = dx * dx + dy * dy;
                if (!discovery.have_sample ||
                    distance < discovery.best_distance) {
                    discovery.coverage.sample_x = column.x;
                    discovery.coverage.sample_y = column.sample_y;
                    discovery.best_distance = distance;
                    discovery.have_sample = true;
                }
                break;
            }

            case DiscoveryPhase::FinishZoom:
                commit_discovery_zoom();
                ++discovery.zoom;
                discovery.phase = discovery.stop_after_zoom
                    ? DiscoveryPhase::FinishAll
                    : DiscoveryPhase::OpenZoom;
                break;

            case DiscoveryPhase::FinishAll:
                finish_discovery();
                return false;
        }
    }
    return false;
}

bool sigurdos_map_discovery_in_progress() {
    return discovery.phase != DiscoveryPhase::Idle;
}

void sigurdos_map_cancel_discovery() {
    if (discovery.phase == DiscoveryPhase::Idle) return;
    close_discovery_directories();
    reset_tile_coverage();
    discovery = DiscoveryState{};
    discovery.result = SigurdosMapDiscoveryResult::Cancelled;
    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Runtime);
}

SigurdosMapDiscoveryProgress sigurdos_map_discovery_progress() {
    return {discovery.result, discovery.zoom,
            discovery.entries_processed, discovery.used_index};
}

void sigurdos_map_deinit() {
    sigurdos_map_cancel_discovery();
    sigurdos_map_release_owned_buffer(discovery_xcache, map_free);
    if (!initialized) return;
    delete_cb_registered = false;

    // Clean up contact marker dots so dangling LVGL pointers don't
    // cause a use-after-free crash on the next map visit.
    sigurdos_map_contact_deinit();

    tile_cache_clear(tile_cache, TILE_CACHE_SIZE, map_free);
    cache_clock = 0;
    missing_tile_cache_init(missing_tile_cache, MISSING_TILE_CACHE_SIZE);

    if (canvas_pixels) { heap_caps_free(canvas_pixels); canvas_pixels = nullptr; }

    // Canvas widget is destroyed by LVGL via auto-delete —
    // null our pointer so the next map visit reinitializes
    map_canvas = nullptr;
    initialized = false;
}

void sigurdos_map_set_view(double lat, double lon, int zoom) {
    center_lat = clamp_d(lat, MIN_LAT, MAX_LAT);
    center_lon = sigurdos_map_wrap_lon(lon);
    zoom_level = clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    clamp_view_to_coverage();
}

double sigurdos_map_get_lat()    { return center_lat; }
double sigurdos_map_get_lon()    { return center_lon; }
int    sigurdos_map_get_zoom()   { return zoom_level; }

void sigurdos_map_pan(int dx, int dy) {
    // Convert screen pixel delta to tile coordinate delta
    // This properly handles Web Mercator's non-linear latitude scaling
    double tx = lon_to_tile_x(center_lon, zoom_level);
    double ty = lat_to_tile_y(center_lat, zoom_level);
    tx += dx / (double)TILE_SIZE;
    ty += dy / (double)TILE_SIZE;   // screen y increases downward, tile y too
    center_lon = tile_x_to_lon(tx, zoom_level);
    center_lat = tile_y_to_lat(ty, zoom_level);
    center_lat = clamp_d(center_lat, MIN_LAT, MAX_LAT);
    center_lon = sigurdos_map_wrap_lon(center_lon);
    clamp_view_to_coverage();
}

void sigurdos_map_zoom_in()  {
    zoom_level = have_tile_coverage
        ? sigurdos_map_select_available_zoom(
              zoom_level, 1, min_available_zoom, max_available_zoom,
              [](int zoom) { return tile_coverage[zoom].valid; })
        : clamp(zoom_level + 1, MIN_ZOOM, MAX_ZOOM);
    clamp_view_to_coverage();
}

void sigurdos_map_zoom_out() {
    zoom_level = have_tile_coverage
        ? sigurdos_map_select_available_zoom(
              zoom_level, -1, min_available_zoom, max_available_zoom,
              [](int zoom) { return tile_coverage[zoom].valid; })
        : clamp(zoom_level - 1, MIN_ZOOM, MAX_ZOOM);
    clamp_view_to_coverage();
}

#if SIGURDOS_MAP_DIAGNOSTICS
static void sigurdos_map_debug_summary(char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';

    size_t pos = 0;
    appendf(out, out_sz, &pos, "SD:%d init:%d z:%d\n",
            sigurdos_sdcard_mounted() ? 1 : 0,
            initialized ? 1 : 0,
            zoom_level);

    appendf(out, out_sz, &pos, "cov:%d", have_tile_coverage ? 1 : 0);
    if (have_tile_coverage) {
        appendf(out, out_sz, &pos, " z:%d-%d", min_available_zoom, max_available_zoom);
    }
    appendf(out, out_sz, &pos, "\n");
    appendf(out, out_sz, &pos, "%s\n", last_tile_status);

    DIR* root = opendir(SIGURDOS_SD_MOUNTPOINT);
    appendf(out, out_sz, &pos, "root:%s", root ? "ok" : "fail");
    if (root) {
        int shown = 0;
        struct dirent* e;
        while (shown < 3 && (e = readdir(root)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            appendf(out, out_sz, &pos, " %s", e->d_name);
            shown++;
        }
        closedir(root);
    }
    appendf(out, out_sz, &pos, "\n");

    const char* tiles_path = SIGURDOS_SD_MOUNTPOINT "/tiles";
    DIR* tiles = opendir(tiles_path);
    appendf(out, out_sz, &pos, "tiles:%s", tiles ? "ok" : "fail");
    int best_z = -1;
    if (tiles) {
        bool zoom_seen[MAX_ZOOM + 1] = {};
        int shown = 0;
        struct dirent* e;
        while ((e = readdir(tiles)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            if (shown < 6) {
                appendf(out, out_sz, &pos, " %s", e->d_name);
                shown++;
            }
            if (!is_decimal_name(e->d_name)) continue;
            int z = atoi(e->d_name);
            if (z < MIN_ZOOM || z > MAX_ZOOM) continue;
            zoom_seen[z] = true;
            if (z > best_z) best_z = z;
        }
        closedir(tiles);

        appendf(out, out_sz, &pos, "\nz:");
        for (int z = MIN_ZOOM; z <= MAX_ZOOM; z++) {
            if (zoom_seen[z]) appendf(out, out_sz, &pos, " %d", z);
        }
    }
    appendf(out, out_sz, &pos, "\n");

    if (best_z >= 0) {
        char z_path[48];
        snprintf(z_path, sizeof(z_path), SIGURDOS_SD_MOUNTPOINT "/tiles/%d", best_z);
        DIR* zd = opendir(z_path);
        appendf(out, out_sz, &pos, "z%d:%s", best_z, zd ? "ok" : "fail");

        int best_x = -1;
        int best_min_y = -1;
        int best_max_y = -1;
        int best_sample_y = -1;
        if (zd) {
            int shown = 0;
            struct dirent* e;
            while ((e = readdir(zd)) != nullptr) {
                if (e->d_name[0] == '.') continue;
                if (!is_decimal_name(e->d_name)) continue;
                int x = atoi(e->d_name);
                if (shown < 4) {
                    appendf(out, out_sz, &pos, " %d", x);
                    shown++;
                }
                int mn_y = -1;
                int mx_y = -1;
                int sample_y = -1;
                if (best_x < 0 && scan_y_range(best_z, x, &mn_y, &mx_y, &sample_y)) {
                    best_x = x;
                    best_min_y = mn_y;
                    best_max_y = mx_y;
                    best_sample_y = sample_y;
                }
            }
            closedir(zd);
        }
        appendf(out, out_sz, &pos, "\n");

        if (best_x >= 0) {
            char sample_path[80];
            snprintf(sample_path, sizeof(sample_path),
                     SIGURDOS_SD_MOUNTPOINT "/tiles/%d/%d/%d.png",
                     best_z, best_x, best_sample_y);
            FILE* f = fopen(sample_path, "rb");
            appendf(out, out_sz, &pos, "x:%d y:%d-%d\nopen:%s\n",
                    best_x, best_min_y, best_max_y, f ? "ok" : "fail");
            if (f) fclose(f);
        } else {
            appendf(out, out_sz, &pos, "png:no numeric .png\n");
        }
    }

    if (pos >= out_sz) out[out_sz - 1] = '\0';
}
#endif

void sigurdos_map_render() {
    if (!initialized || !map_canvas || !canvas_pixels) return;

    MAP_DEBUG_PRINTF("[map] render: zoom=%d center=%.4f,%.4f\n",
                     zoom_level, center_lat, center_lon);
    lv_canvas_fill_bg(map_canvas, lv_color_hex(0x0f3460), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(map_canvas, &layer);

    // ── Draw tile grid ────────────────────────────────
    double center_tx = lon_to_tile_x(center_lon, zoom_level);
    double center_ty = lat_to_tile_y(center_lat, zoom_level);
    const int center_tile_x = (int)floor(center_tx);
    const int center_tile_y = (int)floor(center_ty);
    const double center_frac_x = center_tx - center_tile_x;
    const double center_frac_y = center_ty - center_tile_y;

    double px_per_tile = TILE_SIZE;
    int tiles_across = 1 + TFT_WIDTH / TILE_SIZE + 1;
    int tiles_down   = 1 + TFT_HEIGHT / TILE_SIZE + 1;

    int center_px = TFT_WIDTH / 2;
    int center_py = TFT_HEIGHT / 2;

    bool any_tile_loaded = false;
    const bool sd_mounted = sigurdos_sdcard_mounted();
    const uint32_t render_now_ms = millis();
    TileLoadBudget load_budget(MAP_TILE_LOAD_BUDGET_PER_RENDER);
    last_negative_hits = 0;
    last_deferred_tiles = 0;

    for (int ty = -1; ty <= tiles_down; ty++) {
        for (int tx = -1; tx <= tiles_across; tx++) {
            int tile_x = sigurdos_map_wrap_tile_x(center_tile_x + tx, zoom_level);
            int tile_y = center_tile_y + ty;

            // Y stops at the Mercator limits; X repeats around the world.
            int n = 1 << zoom_level;
            if (tile_y < 0 || tile_y >= n) continue;

            if (have_tile_coverage && tile_coverage[zoom_level].valid) {
                const TileCoverage& c = tile_coverage[zoom_level];
                if (!sigurdos_map_tile_x_in_coverage(
                        tile_x, c.min_x, c.max_x, c.wraps_x) ||
                    tile_y < c.min_y || tile_y > c.max_y) {
                    continue;
                }
            }

            int screen_x = (int)(center_px + (tx * px_per_tile) -
                                 center_frac_x * px_per_tile);
            int screen_y = (int)(center_py + (ty * px_per_tile) -
                                 center_frac_y * px_per_tile);

            // Skip if completely off-screen
            if (!sigurdos_map_tile_intersects_viewport(
                    screen_x, screen_y, TFT_WIDTH, TFT_HEIGHT)) {
                continue;
            }

            // Try to load and render the tile. Positive/negative cache hits do
            // not consume the per-render file/decode budget.
            CachedTile* cached = nullptr;
            const TileLoadResult result = sd_mounted
                ? load_tile(zoom_level, tile_x, tile_y, &load_budget,
                            render_now_ms, &cached)
                : TileLoadResult::Missing;
            if (result == TileLoadResult::Ready && cached) {
                draw_tile_from_cache(cached, screen_x, screen_y);
                any_tile_loaded = true;
            } else {
                // Fallback: placeholder grid
                lv_draw_rect_dsc_t rect_dsc;
                lv_draw_rect_dsc_init(&rect_dsc);
                rect_dsc.bg_color = lv_color_hex(
                    ((tile_x + tile_y) & 1) ? 0x1a1a2e : 0x16213e);
                rect_dsc.bg_opa = LV_OPA_COVER;
                rect_dsc.radius = 0;

                lv_area_t area;
                area.x1 = screen_x;
                area.y1 = screen_y;
                area.x2 = screen_x + TILE_SIZE - 1;
                area.y2 = screen_y + TILE_SIZE - 1;

                lv_draw_rect(&layer, &rect_dsc, &area);

                // A red X distinguishes a confirmed missing/corrupt tile from
                // a neutral tile deferred by this render's load budget.
                if (sd_mounted && result == TileLoadResult::Missing) {
                    const int left = screen_x < 0 ? 0 : screen_x;
                    const int top = screen_y < 0 ? 0 : screen_y;
                    const int right = screen_x + TILE_SIZE - 1 >= TFT_WIDTH
                        ? TFT_WIDTH - 1 : screen_x + TILE_SIZE - 1;
                    const int bottom = screen_y + TILE_SIZE - 1 >= TFT_HEIGHT
                        ? TFT_HEIGHT - 1 : screen_y + TILE_SIZE - 1;
                    const int marker_x = (left + right) / 2;
                    const int marker_y = (top + bottom) / 2;
                    lv_draw_line_dsc_t missing_dsc;
                    lv_draw_line_dsc_init(&missing_dsc);
                    missing_dsc.color = lv_color_hex(0xed4245);
                    missing_dsc.width = 2;
                    missing_dsc.opa = LV_OPA_70;
                    missing_dsc.p1.x = marker_x - 6;
                    missing_dsc.p1.y = marker_y - 6;
                    missing_dsc.p2.x = marker_x + 6;
                    missing_dsc.p2.y = marker_y + 6;
                    lv_draw_line(&layer, &missing_dsc);
                    missing_dsc.p1.y = marker_y + 6;
                    missing_dsc.p2.y = marker_y - 6;
                    lv_draw_line(&layer, &missing_dsc);
                }
            }
        }
    }
    last_load_attempts = load_budget.used;

    // If no tiles loaded at all, show status message
    if (!any_tile_loaded) {
#if SIGURDOS_MAP_DIAGNOSTICS
        static char status[512];
        sigurdos_map_debug_summary(status, sizeof(status));
#endif

        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = lv_color_hex(0x8e9297);
#if SIGURDOS_MAP_DIAGNOSTICS
        label_dsc.text = status;
#else
        label_dsc.text = sigurdos_sdcard_mounted() ?
            "No map tiles found\nCopy tiles/ folder\nto SD card root" :
            "No SD card\nInsert SD card with\ntiles/ folder";
#endif
        label_dsc.text_local = true;

        lv_area_t label_area;
#if SIGURDOS_MAP_DIAGNOSTICS
        label_area.x1 = 8;
        label_area.y1 = 38;
        label_area.x2 = TFT_WIDTH - 8;
        label_area.y2 = TFT_HEIGHT - 34;
#else
        label_area.x1 = 20;
        label_area.y1 = TFT_HEIGHT / 2 - 24;
        label_area.x2 = TFT_WIDTH - 20;
        label_area.y2 = TFT_HEIGHT / 2 + 24;
#endif

        lv_draw_label(&layer, &label_dsc, &label_area);
    }

    // ── Center crosshair ──────────────────────────────
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x5865f2);
    line_dsc.width = 1;
    line_dsc.opa = LV_OPA_50;

    line_dsc.p1.x = center_px - 12; line_dsc.p1.y = center_py;
    line_dsc.p2.x = center_px + 12; line_dsc.p2.y = center_py;
    lv_draw_line(&layer, &line_dsc);
    line_dsc.p1.x = center_px; line_dsc.p1.y = center_py - 12;
    line_dsc.p2.x = center_px; line_dsc.p2.y = center_py + 12;
    lv_draw_line(&layer, &line_dsc);

    lv_canvas_finish_layer(map_canvas, &layer);
    lv_obj_invalidate(map_canvas);
    render_own_position();
}

bool sigurdos_map_tiles_available() {
    if (!sigurdos_sdcard_mounted()) return false;
    int tx = sigurdos_map_tile_x_index(lon_to_tile_x(center_lon, zoom_level),
                                       zoom_level);
    int ty = (int)floor(lat_to_tile_y(center_lat, zoom_level));
    char path[64];
    snprintf(path, sizeof(path), SIGURDOS_SD_MOUNTPOINT "/tiles/%d/%d/%d.png", zoom_level, tx, ty);
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return true; }
    return false;
}

int sigurdos_map_last_load_attempts() { return last_load_attempts; }
int sigurdos_map_last_negative_hits() { return last_negative_hits; }
int sigurdos_map_last_deferred_tiles() { return last_deferred_tiles; }
int sigurdos_map_missing_cache_count() {
    return missing_tile_cache_active_count(
        missing_tile_cache, MISSING_TILE_CACHE_SIZE, millis(),
        MISSING_TILE_CACHE_TTL_MS);
}

void sigurdos_map_pixel_to_latlon(int px, int py, double* out_lat, double* out_lon) {
    if (!sigurdos_map_output_pair_valid(out_lat, out_lon)) {
        MAP_DEBUG_PRINTLN("[map] pixel_to_latlon: null output");
        return;
    }
    double center_tx = lon_to_tile_x(center_lon, zoom_level);
    double center_ty = lat_to_tile_y(center_lat, zoom_level);

    double px_per_tile = TILE_SIZE;
    int center_px = TFT_WIDTH / 2;
    int center_py = TFT_HEIGHT / 2;

    double tile_x = center_tx + (px - center_px) / px_per_tile;
    double tile_y = center_ty + (py - center_py) / px_per_tile;

    *out_lon = tile_x_to_lon(tile_x, zoom_level);
    *out_lat = tile_y_to_lat(tile_y, zoom_level);
}

void sigurdos_map_latlon_to_pixel(double lat, double lon, int* out_px, int* out_py) {
    if (!sigurdos_map_output_pair_valid(out_px, out_py)) {
        MAP_DEBUG_PRINTLN("[map] latlon_to_pixel: null output");
        return;
    }
    double center_tx = lon_to_tile_x(center_lon, zoom_level);
    double center_ty = lat_to_tile_y(center_lat, zoom_level);
    double tile_x = lon_to_tile_x(lon, zoom_level);
    double tile_y = lat_to_tile_y(lat, zoom_level);
    double px_per_tile = TILE_SIZE;
    int center_px = TFT_WIDTH / 2;
    int center_py = TFT_HEIGHT / 2;
    const double delta_x = sigurdos_map_shortest_tile_x_delta(
        center_tx, tile_x, zoom_level);
    *out_px = center_px + (int)(delta_x * px_per_tile);
    *out_py = center_py + (int)((tile_y - center_ty) * px_per_tile);
}

// ── Contact marker pool ─────────────────────────────────────
static constexpr int MAX_CONTACT_DOTS = 32;
static constexpr int CONTACT_DOT_SIZE = 8;

struct ContactDot {
    lv_obj_t* obj;
    char name[32];
};

static ContactDot g_contact_dots[MAX_CONTACT_DOTS];
static bool g_contact_pool_init = false;
static lv_obj_t* g_own_position_dot = nullptr;
static int g_contact_parent_screen_y = 0;

static void render_own_position() {
    if (!g_own_position_dot) return;

    const double own_lat = sigurdos_gps_latitude();
    const double own_lon = sigurdos_gps_longitude();
    if (!sigurdos_map_position_valid(
            sigurdos_gps_has_fix(), own_lat, own_lon)) {
        lv_obj_add_flag(g_own_position_dot, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    int own_px = 0;
    int own_py = 0;
    sigurdos_map_latlon_to_pixel(own_lat, own_lon, &own_px, &own_py);
    if (own_px < -12 || own_px > TFT_WIDTH + 12 ||
        own_py < g_contact_parent_screen_y - 12 || own_py > TFT_HEIGHT + 12) {
        lv_obj_add_flag(g_own_position_dot, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_set_pos(g_own_position_dot,
                   sigurdos_map_marker_origin(own_px, 0, 12),
                   sigurdos_map_marker_origin(
                       own_py, g_contact_parent_screen_y, 12));
    lv_obj_clear_flag(g_own_position_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(g_own_position_dot, -1);
}

void sigurdos_map_contact_init(lv_obj_t* parent, int parent_screen_y) {
    if (!sigurdos_map_required_pointer_valid(parent)) {
        MAP_DEBUG_PRINTLN("[map] contact_init: null parent");
        return;
    }
    g_contact_parent_screen_y = parent_screen_y;
    for (int i = 0; i < MAX_CONTACT_DOTS; i++) {
        lv_obj_t* dot = lv_obj_create(parent);
        lv_obj_set_size(dot, CONTACT_DOT_SIZE, CONTACT_DOT_SIZE);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF4444), 0);  // red for visibility
        lv_obj_set_style_radius(dot, CONTACT_DOT_SIZE / 2, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        g_contact_dots[i].obj = dot;
        g_contact_dots[i].name[0] = '\0';

        lv_obj_add_event_cb(dot, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            for (int j = 0; j < MAX_CONTACT_DOTS; j++) {
                if (g_contact_dots[j].obj == target && g_contact_dots[j].name[0]) {
                    if (g_contact_tap_cb) g_contact_tap_cb(g_contact_dots[j].name);
                    break;
                }
            }
        }, LV_EVENT_CLICKED, nullptr);
    }
    g_own_position_dot = lv_obj_create(parent);
    lv_obj_set_size(g_own_position_dot, 12, 12);
    lv_obj_set_style_bg_color(
        g_own_position_dot, lv_color_hex(sigurdos::theme::ACCENT), 0);
    lv_obj_set_style_radius(g_own_position_dot, 6, 0);
    lv_obj_set_style_border_width(g_own_position_dot, 2, 0);
    lv_obj_set_style_border_color(
        g_own_position_dot, lv_color_hex(sigurdos::theme::TEXT_PRIMARY), 0);
    lv_obj_add_flag(g_own_position_dot, LV_OBJ_FLAG_HIDDEN);
    g_contact_pool_init = true;
}

void sigurdos_map_contact_render(const void* contacts_ptr, int count) {
    if (!g_contact_pool_init) return;
    if (!sigurdos_map_contact_args_valid(contacts_ptr, count)) {
        MAP_DEBUG_PRINTLN("[map] contact_render: invalid contacts/count");
        return;
    }
    const sigurdos::mesh::ContactInfo* contacts = (const sigurdos::mesh::ContactInfo*)contacts_ptr;
    int slot = 0;

    for (int i = 0; i < count && slot < MAX_CONTACT_DOTS; i++) {
        if (!contacts[i].has_location) continue;
        if (contacts[i].latitude == 0.0 && contacts[i].longitude == 0.0) continue;

        int px, py;
        sigurdos_map_latlon_to_pixel(contacts[i].latitude, contacts[i].longitude, &px, &py);

        // Skip if off-screen (with margin)
        if (px < -20 || px > TFT_WIDTH + 20 || py < -20 || py > TFT_HEIGHT + 20) continue;

        lv_obj_clear_flag(g_contact_dots[slot].obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(
            g_contact_dots[slot].obj,
            sigurdos_map_marker_origin(px, 0, CONTACT_DOT_SIZE),
            sigurdos_map_marker_origin(
                py, g_contact_parent_screen_y, CONTACT_DOT_SIZE));
        strncpy(g_contact_dots[slot].name, contacts[i].name, sizeof(g_contact_dots[slot].name) - 1);
        g_contact_dots[slot].name[sizeof(g_contact_dots[slot].name) - 1] = '\0';
        slot++;
    }

    // Hide unused
    for (int i = slot; i < MAX_CONTACT_DOTS; i++) {
        lv_obj_add_flag(g_contact_dots[i].obj, LV_OBJ_FLAG_HIDDEN);
        g_contact_dots[i].name[0] = '\0';
    }
}

void sigurdos_map_contact_deinit() {
    if (g_own_position_dot) {
        lv_obj_del(g_own_position_dot);
        g_own_position_dot = nullptr;
    }
    for (int i = 0; i < MAX_CONTACT_DOTS; i++) {
        if (g_contact_dots[i].obj) {
            lv_obj_del(g_contact_dots[i].obj);
            g_contact_dots[i].obj = nullptr;
        }
    }
    g_contact_pool_init = false;
    g_contact_parent_screen_y = 0;
}
