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
#include <lvgl.h>

// Initialize the map renderer with LVGL parent object
// Call after LVGL is initialized and SD card is mounted
void sigurdos_map_init();

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

// Convert screen pixel to lat/lon
void sigurdos_map_pixel_to_latlon(int px, int py, double* out_lat, double* out_lon);

// Convert lat/lon to screen pixel (inverse of above)
void sigurdos_map_latlon_to_pixel(double lat, double lon, int* out_px, int* out_py);

// Contact marker overlay (pool of pre-allocated dots)
// Call after map_init, before first render. parent = the map overlay object.
void sigurdos_map_contact_init(lv_obj_t* parent);
// Reposition markers for contacts that have location data
void sigurdos_map_contact_render(const void* contacts, int count);
// Free the marker pool
void sigurdos_map_contact_deinit();
// Set tap callback — called when user taps a contact dot
typedef void (*map_contact_tap_cb_t)(const char* name);
void sigurdos_map_contact_set_tap_cb(map_contact_tap_cb_t cb);
