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
#include <lvgl.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <SD.h>

#ifdef ESP32_PLATFORM
#include <esp32/rom/tjpgd.h>
#endif

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

// ── JPEG tile decode buffer (PSRAM on ESP32) ──────────────
static uint16_t* tile_rgb565 = nullptr;  // 256×256×2 = 131KB
static uint8_t*  jpeg_inbuf  = nullptr;  // input buffer (~32KB typical JPEG)
static JDEC       jdec;

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

// ── JPEG decode helpers (TJpgDec callbacks) ───────────────

struct JpegInputCtx {
    File file;
    size_t total;
};

static UINT jpeg_input_func(JDEC* jd, BYTE* buf, UINT nbytes) {
    JpegInputCtx* ctx = (JpegInputCtx*)jd->device;
    if (!ctx || !ctx->file) return 0;
    if (buf) {
        return (UINT)ctx->file.read(buf, nbytes);
    } else {
        // Seek: skip nbytes
        ctx->file.seek(ctx->file.position() + nbytes);
        return nbytes;
    }
}

static UINT jpeg_output_func(JDEC* jd, void* bitmap, JRECT* rect) {
    uint16_t* src = (uint16_t*)bitmap;
    uint16_t* dst = tile_rgb565;
    // Bounds-check: reject rects outside the 256×256 tile buffer
    if (rect->left < 0 || rect->top < 0 ||
        rect->right >= TILE_SIZE || rect->bottom >= TILE_SIZE) {
        return 0;  // abort decode — malformed JPEG
    }
    for (int y = rect->top; y <= rect->bottom; y++) {
        for (int x = rect->left; x <= rect->right; x++) {
            dst[y * TILE_SIZE + x] = *src++;
        }
    }
    return 1;
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
    lv_free(tile_cache[lru].pixels);
    tile_cache[lru].pixels = nullptr;
    return &tile_cache[lru];
}

// ── Tile loading ──────────────────────────────────────────

static bool load_tile_jpeg(int zoom, int tx, int ty) {
    // Check cache first
    if (cache_lookup(zoom, tx, ty)) return true;

    // Allocate a cache slot
    CachedTile* slot = cache_evict_slot();
    if (!slot) return false;

    slot->pixels = (uint16_t*)lv_malloc(TILE_SIZE * TILE_SIZE * 2);
    if (!slot->pixels) return false;

    // Point tile_rgb565 at the cache buffer for the JPEG decoder callbacks
    uint16_t* saved_rgb565 = tile_rgb565;
    tile_rgb565 = slot->pixels;

    // Try .jpg first
    char path[64];
    snprintf(path, sizeof(path), "/tiles/%d/%d/%d.jpg", zoom, tx, ty);

    if (!SD.exists(path)) {
        snprintf(path, sizeof(path), "/tiles/%d/%d/%d.jpeg", zoom, tx, ty);
        if (!SD.exists(path)) {
            lv_free(slot->pixels);
            slot->pixels = nullptr;
            tile_rgb565 = saved_rgb565;
            return false;
        }
    }

    JpegInputCtx ctx;
    ctx.file = SD.open(path, FILE_READ);
    if (!ctx.file) {
        lv_free(slot->pixels);
        slot->pixels = nullptr;
        tile_rgb565 = saved_rgb565;
        return false;
    }
    ctx.total = ctx.file.size();

    JRESULT rc = jd_prepare(&jdec, jpeg_input_func, jpeg_inbuf, 4096, &ctx);
    if (rc != JDR_OK) {
        ctx.file.close();
        lv_free(slot->pixels);
        slot->pixels = nullptr;
        tile_rgb565 = saved_rgb565;
        return false;
    }

    // Reject JPEGs larger than tile dimensions (malformed map tiles)
    if (jdec.width > TILE_SIZE || jdec.height > TILE_SIZE) {
        ctx.file.close();
        lv_free(slot->pixels);
        slot->pixels = nullptr;
        tile_rgb565 = saved_rgb565;
        return false;
    }

    rc = jd_decomp(&jdec, jpeg_output_func, 0);
    ctx.file.close();
    tile_rgb565 = saved_rgb565;

    if (rc != JDR_OK) {
        lv_free(slot->pixels);
        slot->pixels = nullptr;
        return false;
    }

    slot->zoom = zoom;
    slot->tx = tx;
    slot->ty = ty;
    slot->last_used = ++cache_clock;
    return true;
}

// ── Draw tile to LVGL layer ───────────────────────────────

static void draw_tile_from_cache(lv_layer_t* layer, CachedTile* tile,
                                  int screen_x, int screen_y,
                                  int src_x, int src_y, int src_w, int src_h) {
    if (!tile || !tile->pixels) return;

    // Clamp to screen bounds
    int x1 = screen_x;
    int y1 = screen_y;
    int x2 = screen_x + TILE_SIZE - 1;
    int y2 = screen_y + TILE_SIZE - 1;

    if (x1 < 0) { src_x -= x1; src_w += x1; x1 = 0; }
    if (y1 < 0) { src_y -= y1; src_h += y1; y1 = 0; }
    if (x2 >= TFT_WIDTH)  { src_w -= (x2 - TFT_WIDTH + 1);  x2 = TFT_WIDTH - 1; }
    if (y2 >= TFT_HEIGHT) { src_h -= (y2 - TFT_HEIGHT + 1); y2 = TFT_HEIGHT - 1; }
    if (src_w <= 0 || src_h <= 0) return;
    if (src_x < 0) src_x = 0;
    if (src_y < 0) src_y = 0;

    // Create image buffer for the visible portion
    size_t px_count = (size_t)src_w * src_h;
    size_t buf_size = px_count * 2;
    uint16_t* img_buf = (uint16_t*)lv_malloc(buf_size);
    if (!img_buf) return;

    int idx = 0;
    for (int y = src_y; y < src_y + src_h; y++) {
        for (int x = src_x; x < src_x + src_w; x++) {
            img_buf[idx++] = tile->pixels[y * TILE_SIZE + x];
        }
    }

    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    img_dsc.opa = LV_OPA_COVER;

    lv_area_t area;
    area.x1 = x1; area.y1 = y1;
    area.x2 = x2; area.y2 = y2;

    lv_image_dsc_t img;
    img.header.magic = LV_IMAGE_HEADER_MAGIC;
    img.header.cf = LV_COLOR_FORMAT_RGB565;
    img.header.w = (uint32_t)src_w;
    img.header.h = (uint32_t)src_h;
    img.header.stride = (uint32_t)(src_w * 2);
    img.data = (const uint8_t*)img_buf;
    img.data_size = (uint32_t)buf_size;

    img_dsc.src = &img;
    lv_draw_image(layer, &img_dsc, &area);
    // Safe to free now — lv_draw_image on canvas layer is synchronous;
    // pixel data is consumed by the draw call before it returns.
    lv_free(img_buf);
}

// ── Metadata auto-center ──────────────────────────────────
static void load_metadata() {
    if (!slopos_sdcard_mounted()) return;

    File f = SD.open("/tiles/metadata.json", FILE_READ);
    if (!f) return;

    char buf[512];
    int len = f.read((uint8_t*)buf, sizeof(buf) - 1);
    f.close();
    if (len <= 0) return;
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
        center_lat = (bounds[1] + bounds[3]) / 2.0;  // south + north
        center_lon = (bounds[0] + bounds[2]) / 2.0;  // west + east
        // Pick zoom that shows full region width (east-west extent)
        double lon_span = bounds[2] - bounds[0];  // east - west degrees
        for (int z = MAX_ZOOM; z >= MIN_ZOOM; z--) {
            double tile_span = lon_span / 360.0 * (1 << z);
            if (tile_span * TILE_SIZE <= TFT_WIDTH * 0.8) {
                zoom_level = z;
                break;
            }
        }
    }
}

// ════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════

void slopos_map_init() {
    if (initialized) return;

    // Create canvas without parent — reparented when map screen is shown.
    // Using lv_scr_act() would attach to the splash screen, which is
    // destroyed 2s later by home_screen_create(), leaving a dangling pointer.
    map_canvas = lv_canvas_create(nullptr);
    lv_obj_set_size(map_canvas, TFT_WIDTH, TFT_HEIGHT);
    lv_obj_align(map_canvas, LV_ALIGN_CENTER, 0, 0);

    // Allocate draw buffer (320×240×2 = 153KB for RGB565)
    size_t buf_size = (size_t)TFT_WIDTH * TFT_HEIGHT * 2;
    canvas_pixels = (uint8_t*)lv_malloc(buf_size);
    if (!canvas_pixels) {
        Serial.println("[map] ERROR: Failed to alloc canvas buffer");
        lv_obj_del(map_canvas);
        map_canvas = nullptr;
        initialized = false;
        return;
    }
    lv_canvas_set_buffer(map_canvas, canvas_pixels,
                         TFT_WIDTH, TFT_HEIGHT, LV_COLOR_FORMAT_RGB565);

    // Allocate JPEG tile decode buffers (use PSRAM if available)
    tile_rgb565 = (uint16_t*)lv_malloc(TILE_SIZE * TILE_SIZE * 2);  // 131KB
    jpeg_inbuf  = (uint8_t*)lv_malloc(4096);  // 4KB input buffer

    if (!tile_rgb565 || !jpeg_inbuf) {
        Serial.println("[map] ERROR: Failed to alloc JPEG decode buffers");
        slopos_map_deinit();  // frees everything including canvas_pixels
        return;
    }

    // Auto-center on downloaded region from metadata.json
    load_metadata();

    initialized = true;
}

void slopos_map_reparent(lv_obj_t* new_parent) {
    if (map_canvas && new_parent) {
        lv_obj_set_parent(map_canvas, new_parent);
        lv_obj_set_size(map_canvas, TFT_WIDTH, TFT_HEIGHT);
        lv_obj_align(map_canvas, LV_ALIGN_CENTER, 0, 0);
        // When parent screen is auto-deleted (e.g. navigate away),
        // LVGL recursively deletes map_canvas — free all PSRAM
        // allocations so repeated map visits don't leak memory.
        lv_obj_add_event_cb(new_parent, [](lv_event_t* e) {
            slopos_map_deinit();
        }, LV_EVENT_DELETE, nullptr);
    }
}

void slopos_map_deinit() {
    // Free JPEG decode buffers
    if (tile_rgb565) { lv_free(tile_rgb565); tile_rgb565 = nullptr; }
    if (jpeg_inbuf)  { lv_free(jpeg_inbuf);  jpeg_inbuf  = nullptr; }

    // Free LRU tile cache pixels
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        if (tile_cache[i].pixels) {
            lv_free(tile_cache[i].pixels);
            tile_cache[i].pixels = nullptr;
        }
    }

    // Free canvas pixel buffer (LVGL may have already freed it,
    // but our pointer tracks it separately — safe double-check)
    if (canvas_pixels) { lv_free(canvas_pixels); canvas_pixels = nullptr; }

    // Canvas widget is destroyed by LVGL via auto-delete —
    // null our pointer so the next map visit reinitializes
    map_canvas = nullptr;
    initialized = false;
}

void slopos_map_set_view(double lat, double lon, int zoom) {
    center_lat = clamp_d(lat, MIN_LAT, MAX_LAT);
    center_lon = clamp_d(lon, MIN_LON, MAX_LON);
    zoom_level = clamp(zoom, MIN_ZOOM, MAX_ZOOM);
}

double slopos_map_get_lat()  { return center_lat; }
double slopos_map_get_lon()  { return center_lon; }
int    slopos_map_get_zoom() { return zoom_level; }

void slopos_map_pan(int dx, int dy) {
    // Convert screen pixel delta to tile coordinate delta
    // This properly handles Web Mercator's non-linear latitude scaling
    double tx = lon_to_tile_x(center_lon, zoom_level);
    double ty = lat_to_tile_y(center_lat, zoom_level);
    tx -= dx / (double)TILE_SIZE;
    ty -= dy / (double)TILE_SIZE;   // screen y increases downward, tile y too
    center_lon = tile_x_to_lon(tx, zoom_level);
    center_lat = tile_y_to_lat(ty, zoom_level);
    center_lat = clamp_d(center_lat, MIN_LAT, MAX_LAT);
    center_lon = clamp_d(center_lon, MIN_LON, MAX_LON);
}

void slopos_map_zoom_in()  { zoom_level = clamp(zoom_level + 1, MIN_ZOOM, MAX_ZOOM); }
void slopos_map_zoom_out() { zoom_level = clamp(zoom_level - 1, MIN_ZOOM, MAX_ZOOM); }

void slopos_map_render() {
    if (!initialized || !map_canvas || !canvas_pixels) return;

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

            int screen_x = (int)(center_px + (tx * px_per_tile) -
                                 (center_tx - (int)center_tx) * px_per_tile);
            int screen_y = (int)(center_py + (ty * px_per_tile) -
                                 (center_ty - (int)center_ty) * px_per_tile);

            // Skip if completely off-screen
            if (screen_x + TILE_SIZE < 0 || screen_x > TFT_WIDTH ||
                screen_y + TILE_SIZE < 0 || screen_y > TFT_HEIGHT) {
                continue;
            }

            // Try to load and render the JPEG tile (cache-aware)
            if (slopos_sdcard_mounted() && load_tile_jpeg(zoom_level, tile_x, tile_y)) {
                CachedTile* ct = cache_lookup(zoom_level, tile_x, tile_y);
                if (ct) {
                    draw_tile_from_cache(&layer, ct, screen_x, screen_y,
                                         0, 0, TILE_SIZE, TILE_SIZE);
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
        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = lv_color_hex(0x8e9297);
        label_dsc.text = slopos_sdcard_mounted() ?
            "No map tiles found\nCopy tiles/ folder\nto SD card root" :
            "No SD card\nInsert SD card with\ntiles/ folder";
        label_dsc.text_local = true;

        lv_area_t label_area;
        label_area.x1 = 20;
        label_area.y1 = TFT_HEIGHT / 2 - 24;
        label_area.x2 = TFT_WIDTH - 20;
        label_area.y2 = TFT_HEIGHT / 2 + 24;

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
}

bool slopos_map_tiles_available() {
    if (!slopos_sdcard_mounted()) return false;
    // Check for at least one tile at current zoom
    int tx = (int)lon_to_tile_x(center_lon, zoom_level);
    int ty = (int)lat_to_tile_y(center_lat, zoom_level);
    char path[64];
    snprintf(path, sizeof(path), "/tiles/%d/%d/%d.jpg", zoom_level, tx, ty);
    return SD.exists(path);
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
