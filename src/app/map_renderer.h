#pragma once

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
#include <cstddef>
#include <cmath>
#include <cstring>
#include <lvgl.h>

static constexpr int SIGURDOS_MAP_TILE_SIZE = 256;
static constexpr int SIGURDOS_MAP_MAX_ZOOM = 18;
static constexpr int SIGURDOS_MAP_MIN_ZOOM = 0;
static constexpr double SIGURDOS_MAP_MAX_LAT = 85.0511;
static constexpr double SIGURDOS_MAP_MIN_LAT = -85.0511;
static constexpr double SIGURDOS_MAP_MAX_LON = 180.0;
static constexpr double SIGURDOS_MAP_MIN_LON = -180.0;
static constexpr double SIGURDOS_MAP_PI = 3.14159265358979323846;
static constexpr double SIGURDOS_MAP_DEFAULT_US_LAT = 39.8283;
static constexpr double SIGURDOS_MAP_DEFAULT_US_LON = -98.5795;
static constexpr int SIGURDOS_MAP_DEFAULT_US_ZOOM = 4;
static constexpr double SIGURDOS_MAP_DEFAULT_CA_LAT = 56.1304;
static constexpr double SIGURDOS_MAP_DEFAULT_CA_LON = -106.3468;
static constexpr int SIGURDOS_MAP_DEFAULT_CA_ZOOM = 3;
static constexpr std::size_t SIGURDOS_MAP_PNG_IHDR_SIZE = 33;
static constexpr std::size_t SIGURDOS_MAP_PNG_MAX_DECOMPRESSED_BYTES =
    static_cast<std::size_t>(SIGURDOS_MAP_TILE_SIZE) *
    (SIGURDOS_MAP_TILE_SIZE * 4 + 1);

inline std::uint32_t sigurdos_map_png_read_u32(const std::uint8_t* value) {
    return (static_cast<std::uint32_t>(value[0]) << 24) |
           (static_cast<std::uint32_t>(value[1]) << 16) |
           (static_cast<std::uint32_t>(value[2]) << 8) |
           static_cast<std::uint32_t>(value[3]);
}

inline bool sigurdos_map_png_ihdr_supported(const std::uint8_t* png,
                                             std::size_t size) {
    if (!png || size < SIGURDOS_MAP_PNG_IHDR_SIZE) return false;
    if (png[0] != 137 || png[1] != 80 || png[2] != 78 || png[3] != 71 ||
        png[4] != 13 || png[5] != 10 || png[6] != 26 || png[7] != 10) {
        return false;
    }
    if (sigurdos_map_png_read_u32(png + 8) != 13 ||
        png[12] != 'I' || png[13] != 'H' || png[14] != 'D' || png[15] != 'R') {
        return false;
    }
    if (sigurdos_map_png_read_u32(png + 16) != SIGURDOS_MAP_TILE_SIZE ||
        sigurdos_map_png_read_u32(png + 20) != SIGURDOS_MAP_TILE_SIZE) {
        return false;
    }

    const unsigned bit_depth = png[24];
    const unsigned color_type = png[25];
    bool color_supported = false;
    switch (color_type) {
        case 0:  // Greyscale
        case 3:  // Palette
            color_supported = bit_depth == 1 || bit_depth == 2 ||
                              bit_depth == 4 || bit_depth == 8;
            break;
        case 2:  // RGB
        case 4:  // Greyscale + alpha
        case 6:  // RGBA
            color_supported = bit_depth == 8;
            break;
        default:
            return false;
    }

    return color_supported && png[26] == 0 && png[27] == 0 && png[28] == 0;
}

struct SigurdosMapDefaultView {
    double lat;
    double lon;
    int zoom;
};

inline SigurdosMapDefaultView sigurdos_map_default_view_for_radio_profile(
    const char* radio_profile_id) {
    if (radio_profile_id && std::strcmp(radio_profile_id, "ca_902_928") == 0) {
        return {SIGURDOS_MAP_DEFAULT_CA_LAT, SIGURDOS_MAP_DEFAULT_CA_LON,
                SIGURDOS_MAP_DEFAULT_CA_ZOOM};
    }
    if (radio_profile_id && std::strcmp(radio_profile_id, "us_902_928") == 0) {
        return {SIGURDOS_MAP_DEFAULT_US_LAT, SIGURDOS_MAP_DEFAULT_US_LON,
                SIGURDOS_MAP_DEFAULT_US_ZOOM};
    }
    // UK and EU profiles: London is already a reasonable default; no override needed.
    // Return a sentinel that apply_preset_default_view() will ignore.
    return {0.0, 0.0, -1};  // sentinel: caller keeps current center
}

inline bool sigurdos_map_zoom_valid(int zoom) {
    return zoom >= SIGURDOS_MAP_MIN_ZOOM && zoom <= SIGURDOS_MAP_MAX_ZOOM;
}

inline int sigurdos_map_tiles_per_axis(int zoom) {
    return sigurdos_map_zoom_valid(zoom) ? (1 << zoom) : 0;
}

inline int sigurdos_map_clamp_int(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

inline double sigurdos_map_clamp_double(double val, double lo, double hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

inline double sigurdos_map_clamp_lat(double lat) {
    return sigurdos_map_clamp_double(lat, SIGURDOS_MAP_MIN_LAT, SIGURDOS_MAP_MAX_LAT);
}

inline double sigurdos_map_clamp_lon(double lon) {
    return sigurdos_map_clamp_double(lon, SIGURDOS_MAP_MIN_LON, SIGURDOS_MAP_MAX_LON);
}

inline double sigurdos_map_wrap_lon(double lon) {
    if (!std::isfinite(lon)) return 0.0;
    double wrapped = std::fmod(lon - SIGURDOS_MAP_MIN_LON, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    return wrapped + SIGURDOS_MAP_MIN_LON;
}

inline int sigurdos_map_wrap_tile_x(int tile_x, int zoom) {
    const int n = sigurdos_map_tiles_per_axis(zoom);
    if (n <= 0) return 0;
    const int remainder = tile_x % n;
    return remainder < 0 ? remainder + n : remainder;
}

inline double sigurdos_map_wrap_tile_x(double tile_x, int zoom) {
    const int n = sigurdos_map_tiles_per_axis(zoom);
    if (n <= 0 || !std::isfinite(tile_x)) return 0.0;
    double wrapped = std::fmod(tile_x, static_cast<double>(n));
    if (wrapped < 0.0) wrapped += n;
    return wrapped;
}

inline int sigurdos_map_tile_x_index(double tile_x, int zoom) {
    const double wrapped = sigurdos_map_wrap_tile_x(tile_x, zoom);
    return static_cast<int>(std::floor(wrapped));
}

inline double sigurdos_map_shortest_tile_x_delta(double from_tile_x,
                                                  double to_tile_x,
                                                  int zoom) {
    const int n = sigurdos_map_tiles_per_axis(zoom);
    if (n <= 0 || !std::isfinite(from_tile_x) || !std::isfinite(to_tile_x)) {
        return 0.0;
    }
    double delta = std::fmod(to_tile_x - from_tile_x, static_cast<double>(n));
    const double half_world = n / 2.0;
    if (delta < -half_world) delta += n;
    if (delta >= half_world) delta -= n;
    return delta;
}

inline bool sigurdos_map_tile_x_in_coverage(int tile_x, int min_x, int max_x,
                                             bool wraps_x) {
    return wraps_x ? (tile_x >= min_x || tile_x <= max_x)
                   : (tile_x >= min_x && tile_x <= max_x);
}

inline bool sigurdos_map_tile_valid(int zoom, int x, int y) {
    const int n = sigurdos_map_tiles_per_axis(zoom);
    return n > 0 && x >= 0 && x < n && y >= 0 && y < n;
}

inline bool sigurdos_map_tile_intersects_viewport(int screen_x, int screen_y,
                                                   int viewport_w, int viewport_h) {
    return viewport_w > 0 && viewport_h > 0 &&
        screen_x + SIGURDOS_MAP_TILE_SIZE > 0 && screen_x < viewport_w &&
        screen_y + SIGURDOS_MAP_TILE_SIZE > 0 && screen_y < viewport_h;
}

inline double sigurdos_map_lon_to_tile_x(double lon, int zoom) {
    const int n = sigurdos_map_tiles_per_axis(zoom);
    if (n <= 0) return 0.0;
    return (sigurdos_map_wrap_lon(lon) + 180.0) / 360.0 * (double)n;
}

inline double sigurdos_map_lat_to_tile_y(double lat, int zoom) {
    const int n = sigurdos_map_tiles_per_axis(zoom);
    if (n <= 0) return 0.0;
    const double clamped_lat = sigurdos_map_clamp_lat(lat);
    const double lat_rad = clamped_lat * SIGURDOS_MAP_PI / 180.0;
    return (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) /
                      SIGURDOS_MAP_PI) /
           2.0 * (double)n;
}

inline double sigurdos_map_tile_x_to_lon(double tile_x, int zoom) {
    const int n = sigurdos_map_tiles_per_axis(zoom);
    if (n <= 0) return 0.0;
    return sigurdos_map_wrap_tile_x(tile_x, zoom) / (double)n * 360.0 - 180.0;
}

inline double sigurdos_map_tile_y_to_lat(double tile_y, int zoom) {
    const int n = sigurdos_map_tiles_per_axis(zoom);
    if (n <= 0) return 0.0;
    return std::atan(std::sinh(SIGURDOS_MAP_PI * (1.0 - 2.0 * tile_y / (double)n))) *
           180.0 / SIGURDOS_MAP_PI;
}

inline bool sigurdos_map_required_pointer_valid(const void* pointer) {
    return pointer != nullptr;
}

inline bool sigurdos_map_output_pair_valid(const void* first,
                                            const void* second) {
    return first != nullptr && second != nullptr;
}

inline bool sigurdos_map_contact_args_valid(const void* contacts, int count) {
    return count >= 0 && (count == 0 || contacts != nullptr);
}

template <typename T, typename FreeFn>
inline bool sigurdos_map_release_owned_buffer(T*& buffer, FreeFn free_fn) {
    if (!buffer) return false;
    free_fn(buffer);
    buffer = nullptr;
    return true;
}

// Initialize the map renderer with LVGL parent object
// Call after LVGL is initialized and SD card is mounted
void sigurdos_map_init();

// Discover available tile zoom levels (deferred from boot)
void sigurdos_map_discover_tiles();

// Set the map viewport center (lat/lon) and zoom level
void sigurdos_map_set_view(double lat, double lon, int zoom);

// Get current view state
double sigurdos_map_get_lat();
double sigurdos_map_get_lon();
int    sigurdos_map_get_zoom();

// Pan the map by screen pixel deltas
void sigurdos_map_pan(int dx, int dy);

// Zoom in/out by one level
void sigurdos_map_zoom_in();
void sigurdos_map_zoom_out();

// Render the current map view to the LVGL canvas
void sigurdos_map_render();

// Reparent the map canvas to a new screen (call from map_screen_show)
void sigurdos_map_reparent(lv_obj_t* new_parent);

// Free all map allocations (call on screen delete to prevent PSRAM leaks)
void sigurdos_map_deinit();

// Check if map tiles are available on SD card
bool sigurdos_map_tiles_available();

// Last render's bounded tile-I/O diagnostics.
int sigurdos_map_last_load_attempts();
int sigurdos_map_last_negative_hits();
int sigurdos_map_last_deferred_tiles();
int sigurdos_map_missing_cache_count();

// Convert screen pixel to lat/lon. Both output pointers are required.
void sigurdos_map_pixel_to_latlon(int px, int py, double* out_lat, double* out_lon);

// Convert lat/lon to screen pixel (inverse of above). Both outputs are required.
void sigurdos_map_latlon_to_pixel(double lat, double lon, int* out_px, int* out_py);

// Contact marker overlay (pool of pre-allocated dots)
// Call after map_init, before first render. parent = the non-null map overlay object.
void sigurdos_map_contact_init(lv_obj_t* parent);
// Reposition markers for contacts that have location data. contacts may be
// null only when count is zero.
void sigurdos_map_contact_render(const void* contacts, int count);
// Free the marker pool
void sigurdos_map_contact_deinit();
// Set tap callback — called when user taps a contact dot
typedef void (*map_contact_tap_cb_t)(const char* name);
void sigurdos_map_contact_set_tap_cb(map_contact_tap_cb_t cb);
