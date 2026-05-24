// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SlopOS-TDeck.
//
// SlopOS-TDeck is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SlopOS-TDeck is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SlopOS-TDeck.  If not, see <https://www.gnu.org/licenses/>.


#include "map_renderer.h"
#include "../hal/tdeck_pins.h"
#include "../hal/sdcard.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <cstdarg>
#include <strings.h>
#include <lodepng.h>
#include <esp_heap_caps.h>

extern void lodepng_free(void* ptr);

// ── Constants ─────────────────────────────────────────────
static constexpr int TILE_SIZE    = 256;   // standard tile size (pixels)
static constexpr int MAX_ZOOM     = 18;
static constexpr int MIN_ZOOM     = 0;
static constexpr double MAX_LAT   = 85.0511;
static constexpr double MIN_LAT   = -85.0511;
static constexpr double MAX_LON   = 180.0;
static constexpr double MIN_LON   = -180.0;

// Note: PI is defined by Arduino.h as a macro
static lv_obj_t* map_canvas = nullptr;
static uint8_t*   canvas_pixels = nullptr;

static double center_lat = 51.5074;  // London
static double center_lon = -0.1278;
static int    zoom_level = 10;
static bool   initialized = false;

struct TileCoverage {
    bool valid;
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
#if defined(SLOPOS_DEBUG) || defined(SLOPOS_MAP_DEBUG)
#define SLOPOS_MAP_DIAGNOSTICS 1
#define MAP_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define MAP_DEBUG_PRINTLN(msg) Serial.println(msg)
#else
#define SLOPOS_MAP_DIAGNOSTICS 0
#define MAP_DEBUG_PRINTF(...) do {} while (0)
#define MAP_DEBUG_PRINTLN(msg) do {} while (0)
#endif



// ── Web Mercator helpers ──────────────────────────────────
static double lon_to_tile_x(double lon, int z) {
    double n = (double)(1 << z);
    return (lon + 180.0) / 360.0 * n;
}

static double lat_to_tile_y(double lat, int z) {
    double n = (double)(1 << z);
    double lat_rad = lat * PI / 180.0;
    return (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / PI) / 2.0 * n;
}

static double tile_x_to_lon(double tx, int z) {
    double n = (double)(1 << z);
    return tx / n * 360.0 - 180.0;
}

static double tile_y_to_lat(double ty, int z) {
    double n = (double)(1 << z);
    return atan(sinh(PI * (1.0 - 2.0 * ty / n))) * 180.0 / PI;
}

static int clamp(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static double clamp_d(double val, double lo, double hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static void* map_alloc(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

static void map_free(void* p) {
    heap_caps_free(p);
}

#if SLOPOS_MAP_DIAGNOSTICS
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
        tile_coverage[i] = {false, 0, 0, 0, 0, 0, 0};
    }
    min_available_zoom = MIN_ZOOM;
    max_available_zoom = MAX_ZOOM;
    have_tile_coverage = false;
}

static void clamp_view_to_coverage() {
    if (!have_tile_coverage) return;

    zoom_level = clamp(zoom_level, min_available_zoom, max_available_zoom);
    const TileCoverage& c = tile_coverage[zoom_level];
    if (!c.valid) return;

    double tx = lon_to_tile_x(center_lon, zoom_level);
    double ty = lat_to_tile_y(center_lat, zoom_level);

    double half_w_tiles = (TFT_WIDTH / 2.0) / TILE_SIZE;
    double half_h_tiles = (TFT_HEIGHT / 2.0) / TILE_SIZE;
    double min_tx = c.min_x + half_w_tiles;
    double max_tx = c.max_x + 1.0 - half_w_tiles;
    double min_ty = c.min_y + half_h_tiles;
    double max_ty = c.max_y + 1.0 - half_h_tiles;

    if (min_tx > max_tx) {
        tx = (c.min_x + c.max_x + 1.0) / 2.0;
    } else {
        tx = clamp_d(tx, min_tx, max_tx);
    }

    if (min_ty > max_ty) {
        ty = (c.min_y + c.max_y + 1.0) / 2.0;
    } else {
        ty = clamp_d(ty, min_ty, max_ty);
    }

    center_lon = tile_x_to_lon(tx, zoom_level);
    center_lat = tile_y_to_lat(ty, zoom_level);
    center_lat = clamp_d(center_lat, MIN_LAT, MAX_LAT);
    center_lon = clamp_d(center_lon, MIN_LON, MAX_LON);
}

// ── Tile cache (LRU, 4 tiles = ~524KB PSRAM) ─────────────
struct CachedTile {
    int zoom, tx, ty;
    uint16_t* pixels;
    uint32_t last_used;
};
static constexpr int TILE_CACHE_SIZE = 4;
static CachedTile tile_cache[TILE_CACHE_SIZE];
static uint32_t   cache_clock = 0;

static CachedTile* cache_lookup(int zoom, int tx, int ty) {
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        if (tile_cache[i].pixels &&
            tile_cache[i].zoom == zoom &&
            tile_cache[i].tx == tx &&
            tile_cache[i].ty == ty) {
            tile_cache[i].last_used = ++cache_clock;
            return &tile_cache[i];
        }
    }
    return nullptr;
}

static CachedTile* cache_evict_slot() {
    int lru = 0;
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        if (!tile_cache[i].pixels) return &tile_cache[i];
        if (tile_cache[i].last_used < tile_cache[lru].last_used) lru = i;
    }
    map_free(tile_cache[lru].pixels);
    tile_cache[lru].pixels = nullptr;
    return &tile_cache[lru];
}

// ── Tile loading (PNG) ─────────────────────────────────────

static bool load_tile(int zoom, int tx, int ty) {
    if (cache_lookup(zoom, tx, ty)) {
        set_tile_status("load:cache %d/%d/%d", zoom, tx, ty);
        return true;
    }

    CachedTile* slot = cache_evict_slot();
    if (!slot) {
        set_tile_status("load:no slot");
        return false;
    }

    slot->pixels = (uint16_t*)map_alloc(TILE_SIZE * TILE_SIZE * 2);
    if (!slot->pixels) {
        set_tile_status("load:no tile buf");
        return false;
    }

    char path[64];
    snprintf(path, sizeof(path), SLOPOS_SD_MOUNTPOINT "/tiles/%d/%d/%d.png", zoom, tx, ty);

    FILE* f = fopen(path, "rb");
    if (!f) {
        MAP_DEBUG_PRINTF("[map] tile miss: %s\n", path);
        set_tile_status("load:fopen fail %d/%d/%d", zoom, tx, ty);
        map_free(slot->pixels); slot->pixels = nullptr; return false;
    }
    MAP_DEBUG_PRINTF("[map] tile hit: %s\n", path);

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 196 * 1024) {
        set_tile_status("load:size %ld %d/%d/%d", fsize, zoom, tx, ty);
        fclose(f); map_free(slot->pixels); slot->pixels = nullptr; return false;
    }

    uint8_t* png_buf = (uint8_t*)map_alloc((size_t)fsize);
    if (!png_buf) {
        set_tile_status("load:no png buf %ld", fsize);
        fclose(f); map_free(slot->pixels); slot->pixels = nullptr; return false;
    }

    if (fread(png_buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        set_tile_status("load:read fail %ld", fsize);
        fclose(f); map_free(png_buf); map_free(slot->pixels); slot->pixels = nullptr; return false;
    }
    fclose(f);

    unsigned w, h;
    uint8_t* rgba = nullptr;
    unsigned err = lodepng_decode_memory(&rgba, &w, &h, png_buf, (size_t)fsize, LCT_RGBA, 8);
    map_free(png_buf);

    if (err != 0 || w != TILE_SIZE || h != TILE_SIZE) {
        set_tile_status("load:png err %u %ux%u", err, w, h);
        lodepng_free(rgba); map_free(slot->pixels); slot->pixels = nullptr; return false;
    }

    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++) {
            int i = (y * TILE_SIZE + x) * 4;
            uint16_t r = rgba[i] >> 3;
            uint16_t g = rgba[i + 1] >> 2;
            uint16_t b = rgba[i + 2] >> 3;
            slot->pixels[y * TILE_SIZE + x] = (r << 11) | (g << 5) | b;
        }
    }

    lodepng_free(rgba);

    slot->zoom = zoom;
    slot->tx = tx;
    slot->ty = ty;
    slot->last_used = ++cache_clock;
    set_tile_status("load:ok %d/%d/%d %ldB", zoom, tx, ty, fsize);
    return true;
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
    snprintf(y_path, sizeof(y_path), SLOPOS_SD_MOUNTPOINT "/tiles/%d/%d", zoom, x);

    DIR* yd = opendir(y_path);
    if (!yd) return false;

    int mn_y = -1;
    int mx_y = -1;
    struct dirent* ye;
    while ((ye = readdir(yd)) != nullptr) {
        if (!entry_is_png_tile(ye)) continue;
        int y = atoi(ye->d_name);
        if (mn_y < 0) {
            mn_y = mx_y = y;
        } else {
            if (y < mn_y) mn_y = y;
            if (y > mx_y) mx_y = y;
        }
    }
    closedir(yd);

    if (mn_y < 0) return false;
    *min_y = mn_y;
    *max_y = mx_y;

    double mid_y = (mn_y + mx_y) / 2.0;
    int best_y = -1;
    double best_dist = 0.0;

    yd = opendir(y_path);
    if (!yd) return false;
    while ((ye = readdir(yd)) != nullptr) {
        if (!entry_is_png_tile(ye)) continue;
        int y = atoi(ye->d_name);
        double dist = fabs((double)y - mid_y);
        if (best_y < 0 || dist < best_dist) {
            best_y = y;
            best_dist = dist;
        }
    }
    closedir(yd);

    if (best_y < 0) return false;
    *sample_y = best_y;
    return true;
}

static bool scan_zoom_coverage(int z, TileCoverage* out) {
    if (!out) return false;

    char x_path[48];
    snprintf(x_path, sizeof(x_path), SLOPOS_SD_MOUNTPOINT "/tiles/%d", z);
    DIR* xd = opendir(x_path);
    if (!xd) return false;

    TileCoverage c = {false, 0, 0, 0, 0, 0, 0};
    int scanned = 0;
    struct dirent* xe;
    while ((xe = readdir(xd)) != nullptr) {
        if (xe->d_name[0] == '.') continue;
        if (!is_decimal_name(xe->d_name)) continue;

        int x = atoi(xe->d_name);
        int mn_y = -1;
        int mx_y = -1;
        int sample_y = -1;
        if (!scan_y_range(z, x, &mn_y, &mx_y, &sample_y)) continue;

        if (!c.valid) {
            c.valid = true;
            c.min_x = c.max_x = x;
            c.min_y = mn_y;
            c.max_y = mx_y;
            c.sample_x = x;
            c.sample_y = sample_y;
        } else {
            if (x < c.min_x) c.min_x = x;
            if (x > c.max_x) c.max_x = x;
            if (mn_y < c.min_y) c.min_y = mn_y;
            if (mx_y > c.max_y) c.max_y = mx_y;
        }

        if ((++scanned % 16) == 0) delay(0);
    }
    closedir(xd);

    if (!c.valid) return false;

    double mid_x = (c.min_x + c.max_x) / 2.0;
    double mid_y = (c.min_y + c.max_y) / 2.0;
    double best_dist = 0.0;
    bool have_sample = false;

    xd = opendir(x_path);
    if (!xd) return false;
    while ((xe = readdir(xd)) != nullptr) {
        if (xe->d_name[0] == '.') continue;
        if (!is_decimal_name(xe->d_name)) continue;

        int x = atoi(xe->d_name);
        int mn_y = -1;
        int mx_y = -1;
        int sample_y = -1;
        if (!scan_y_range(z, x, &mn_y, &mx_y, &sample_y)) continue;

        double dist_x = (double)x - mid_x;
        double dist_y = (double)sample_y - mid_y;
        double dist = dist_x * dist_x + dist_y * dist_y;
        if (!have_sample || dist < best_dist) {
            c.sample_x = x;
            c.sample_y = sample_y;
            best_dist = dist;
            have_sample = true;
        }

        if ((++scanned % 16) == 0) delay(0);
    }
    closedir(xd);

    if (!have_sample) return false;
    *out = c;
    return true;
}

static void discover_tiles() {
    if (!slopos_sdcard_mounted()) {
        MAP_DEBUG_PRINTLN("[map] discover: SD not mounted");
        return;
    }

    reset_tile_coverage();

    const char* tiles_path = SLOPOS_SD_MOUNTPOINT "/tiles";
    DIR* tiles_dir = opendir(tiles_path);
    if (!tiles_dir) {
        MAP_DEBUG_PRINTF("[map] discover: opendir(%s) failed\n", tiles_path);
        return;
    }
    closedir(tiles_dir);

    for (int z = MIN_ZOOM; z <= MAX_ZOOM; z++) {
        TileCoverage c;
        if (!scan_zoom_coverage(z, &c)) {
            MAP_DEBUG_PRINTF("[map] discover: zoom %d has no png tiles\n", z);
            continue;
        }
        tile_coverage[z] = c;
        if (!have_tile_coverage) {
            min_available_zoom = z;
            max_available_zoom = z;
            have_tile_coverage = true;
        } else {
            if (z < min_available_zoom) min_available_zoom = z;
            if (z > max_available_zoom) max_available_zoom = z;
        }

        MAP_DEBUG_PRINTF("[map] discover: zoom=%d x=%d-%d y=%d-%d sample=%d/%d\n",
                         z, c.min_x, c.max_x, c.min_y, c.max_y,
                         c.sample_x, c.sample_y);
    }

    if (!have_tile_coverage) {
        MAP_DEBUG_PRINTLN("[map] discover: no png tiles found");
        return;
    }

    zoom_level = max_available_zoom;
    const TileCoverage& c = tile_coverage[zoom_level];
    center_lon = tile_x_to_lon((double)c.sample_x + 0.5, zoom_level);
    center_lat = tile_y_to_lat((double)c.sample_y + 0.5, zoom_level);
    clamp_view_to_coverage();

    MAP_DEBUG_PRINTF("[map] discover: center=%.4f,%.4f zoom=%d available=%d-%d\n",
                     center_lat, center_lon, zoom_level,
                     min_available_zoom, max_available_zoom);
}

// ── Metadata auto-center (from metadata.json, fallback to discover) ──
static void load_metadata() {
    if (!slopos_sdcard_mounted()) return;

    // Always discover tiles first — this correctly sets zoom_level = best_zoom
    discover_tiles();

    // If metadata.json has bounds, refine the center from those
    FILE* f = fopen(SLOPOS_SD_MOUNTPOINT "/tiles/metadata.json", "r");
    if (!f) return;
    char buf[512];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (len == 0) return;
    buf[len] = '\0';
    const char* p = strstr(buf, "\"bounds\"");
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    p++;
    double bounds[4];
    int n = 0;
    while (n < 4 && *p) {
        while (*p && (*p == ' ' || *p == ',' || *p == '\n')) p++;
        char* end;
        bounds[n] = strtod(p, &end);
        if (end == p) break;
        p = end;
        n++;
    }
    if (n >= 4) {
        center_lat = (bounds[1] + bounds[3]) / 2.0;
        center_lon = (bounds[0] + bounds[2]) / 2.0;
        clamp_view_to_coverage();
        MAP_DEBUG_PRINTF("[map] metadata: center overridden to %.4f,%.4f\n",
                         center_lat, center_lon);
    }
}

// ════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════

void slopos_map_init() {
    if (initialized) return;

    // Allocate draw buffer (320×240×2 = 153KB for RGB565).
    // DRAM first: LVGL canvas draw ops require CPU/DMA-accessible memory.
    size_t buf_size = (size_t)TFT_WIDTH * TFT_HEIGHT * 2;
    canvas_pixels = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!canvas_pixels) {
        canvas_pixels = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!canvas_pixels) return;

    // Discover tiles and set initial center/zoom.
    // Canvas is created later in slopos_map_reparent() once we have a real
    // screen parent — lv_canvas_create(nullptr) in LVGL v9 makes a screen
    // object, not an orphan widget, so reparenting it silently fails.
    load_metadata();

    initialized = true;
}
void slopos_map_reparent(lv_obj_t* new_parent) {
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

    static bool delete_cb_registered = false;
    if (!delete_cb_registered) {
        lv_obj_add_event_cb(new_parent, [](lv_event_t* e) {
            slopos_map_deinit();
        }, LV_EVENT_DELETE, nullptr);
        delete_cb_registered = true;
    }
}

void slopos_map_deinit() {
    // Free LRU tile cache pixels
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        if (tile_cache[i].pixels) {
            map_free(tile_cache[i].pixels);
            tile_cache[i].pixels = nullptr;
        }
    }

    if (canvas_pixels) { heap_caps_free(canvas_pixels); canvas_pixels = nullptr; }

    // Canvas widget is destroyed by LVGL via auto-delete —
    // null our pointer so the next map visit reinitializes
    map_canvas = nullptr;
    initialized = false;
}

void slopos_map_set_view(double lat, double lon, int zoom) {
    center_lat = clamp_d(lat, MIN_LAT, MAX_LAT);
    center_lon = clamp_d(lon, MIN_LON, MAX_LON);
    zoom_level = clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    clamp_view_to_coverage();
}

double slopos_map_get_lat()    { return center_lat; }
double slopos_map_get_lon()    { return center_lon; }
int    slopos_map_get_zoom()   { return zoom_level; }

void slopos_map_pan(int dx, int dy) {
    // Convert screen pixel delta to tile coordinate delta
    // This properly handles Web Mercator's non-linear latitude scaling
    double tx = lon_to_tile_x(center_lon, zoom_level);
    double ty = lat_to_tile_y(center_lat, zoom_level);
    tx += dx / (double)TILE_SIZE;
    ty += dy / (double)TILE_SIZE;   // screen y increases downward, tile y too
    center_lon = tile_x_to_lon(tx, zoom_level);
    center_lat = tile_y_to_lat(ty, zoom_level);
    center_lat = clamp_d(center_lat, MIN_LAT, MAX_LAT);
    center_lon = clamp_d(center_lon, MIN_LON, MAX_LON);
    clamp_view_to_coverage();
}

void slopos_map_zoom_in()  {
    zoom_level = clamp(zoom_level + 1, MIN_ZOOM, MAX_ZOOM);
    clamp_view_to_coverage();
}

void slopos_map_zoom_out() {
    zoom_level = clamp(zoom_level - 1, MIN_ZOOM, MAX_ZOOM);
    clamp_view_to_coverage();
}

#if SLOPOS_MAP_DIAGNOSTICS
static void slopos_map_debug_summary(char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';

    size_t pos = 0;
    appendf(out, out_sz, &pos, "SD:%d init:%d z:%d\n",
            slopos_sdcard_mounted() ? 1 : 0,
            initialized ? 1 : 0,
            zoom_level);

    appendf(out, out_sz, &pos, "cov:%d", have_tile_coverage ? 1 : 0);
    if (have_tile_coverage) {
        appendf(out, out_sz, &pos, " z:%d-%d", min_available_zoom, max_available_zoom);
    }
    appendf(out, out_sz, &pos, "\n");
    appendf(out, out_sz, &pos, "%s\n", last_tile_status);

    DIR* root = opendir(SLOPOS_SD_MOUNTPOINT);
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

    const char* tiles_path = SLOPOS_SD_MOUNTPOINT "/tiles";
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
        snprintf(z_path, sizeof(z_path), SLOPOS_SD_MOUNTPOINT "/tiles/%d", best_z);
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
                     SLOPOS_SD_MOUNTPOINT "/tiles/%d/%d/%d.png",
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

void slopos_map_render() {
    if (!initialized || !map_canvas || !canvas_pixels) return;

    MAP_DEBUG_PRINTF("[map] render: zoom=%d center=%.4f,%.4f\n",
                     zoom_level, center_lat, center_lon);
    lv_canvas_fill_bg(map_canvas, lv_color_hex(0x0f3460), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(map_canvas, &layer);

    // ── Draw tile grid ────────────────────────────────
    double center_tx = lon_to_tile_x(center_lon, zoom_level);
    double center_ty = lat_to_tile_y(center_lat, zoom_level);

    double px_per_tile = TILE_SIZE;
    int tiles_across = 1 + TFT_WIDTH / TILE_SIZE + 1;
    int tiles_down   = 1 + TFT_HEIGHT / TILE_SIZE + 1;

    int center_px = TFT_WIDTH / 2;
    int center_py = TFT_HEIGHT / 2;

    bool any_tile_loaded = false;

    for (int ty = -1; ty <= tiles_down; ty++) {
        for (int tx = -1; tx <= tiles_across; tx++) {
            int tile_x = (int)(center_tx + tx);
            int tile_y = (int)(center_ty + ty);

            // Clamp tile coordinates to valid range
            int n = 1 << zoom_level;
            if (tile_x < 0 || tile_x >= n || tile_y < 0 || tile_y >= n) continue;

            if (have_tile_coverage && tile_coverage[zoom_level].valid) {
                const TileCoverage& c = tile_coverage[zoom_level];
                if (tile_x < c.min_x || tile_x > c.max_x ||
                    tile_y < c.min_y || tile_y > c.max_y) {
                    continue;
                }
            }

            int screen_x = (int)(center_px + (tx * px_per_tile) -
                                 (center_tx - (int)center_tx) * px_per_tile);
            int screen_y = (int)(center_py + (ty * px_per_tile) -
                                 (center_ty - (int)center_ty) * px_per_tile);

            // Skip if completely off-screen
            if (screen_x + TILE_SIZE < 0 || screen_x > TFT_WIDTH ||
                screen_y + TILE_SIZE < 0 || screen_y > TFT_HEIGHT) {
                continue;
            }

            // Try to load and render the tile (cache-aware, PNG)
            if (slopos_sdcard_mounted() && load_tile(zoom_level, tile_x, tile_y)) {
                CachedTile* ct = cache_lookup(zoom_level, tile_x, tile_y);
                if (ct) {
                    draw_tile_from_cache(ct, screen_x, screen_y);
                    any_tile_loaded = true;
                }
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
            }
        }
    }

    // If no tiles loaded at all, show status message
    if (!any_tile_loaded) {
#if SLOPOS_MAP_DIAGNOSTICS
        static char status[512];
        slopos_map_debug_summary(status, sizeof(status));
#endif

        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = lv_color_hex(0x8e9297);
#if SLOPOS_MAP_DIAGNOSTICS
        label_dsc.text = status;
#else
        label_dsc.text = slopos_sdcard_mounted() ?
            "No map tiles found\nCopy tiles/ folder\nto SD card root" :
            "No SD card\nInsert SD card with\ntiles/ folder";
#endif
        label_dsc.text_local = true;

        lv_area_t label_area;
#if SLOPOS_MAP_DIAGNOSTICS
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
}

bool slopos_map_tiles_available() {
    if (!slopos_sdcard_mounted()) return false;
    int tx = (int)lon_to_tile_x(center_lon, zoom_level);
    int ty = (int)lat_to_tile_y(center_lat, zoom_level);
    char path[64];
    snprintf(path, sizeof(path), SLOPOS_SD_MOUNTPOINT "/tiles/%d/%d/%d.png", zoom_level, tx, ty);
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return true; }
    return false;
}

void slopos_map_pixel_to_latlon(int px, int py, double* out_lat, double* out_lon) {
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
