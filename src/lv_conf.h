#pragma once

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


// SlopOS LVGL Configuration — ST7789 320x240, 16-bit color

#define LV_COLOR_DEPTH          16
#define LV_DPI_DEF              130

#define LV_DRAW_BUF_STRIDE_ALIGN  4
#define LV_DRAW_BUF_ALIGN         4

#define LV_USE_LOG                0
#define LV_USE_PERF_MONITOR       0

#define LV_USE_DRAW_SW            1
#define LV_DRAW_SW_SUPPORT_RGB565 1

// Fonts
#define LV_FONT_MONTSERRAT_10     1
#define LV_FONT_MONTSERRAT_12     1
#define LV_FONT_MONTSERRAT_14     1
#define LV_FONT_MONTSERRAT_16     1
#define LV_FONT_MONTSERRAT_18     1
#define LV_FONT_MONTSERRAT_20     1
#define LV_FONT_MONTSERRAT_24     1
#define LV_FONT_MONTSERRAT_28     1

// Input
#define LV_USE_KEYBOARD           1
#define LV_USE_POINTER            1

// Layout
#define LV_USE_FLEX               1
#define LV_USE_GRID               1
#define LV_USE_ANIMATION          1
#define LV_USE_SNAPSHOT           1

// Memory — use PSRAM for all LVGL allocations (ESP32-S3 with 8MB PSRAM)
#define LV_MEM_CUSTOM             1
#define LV_MEM_CUSTOM_INCLUDE     <esp_heap_caps.h>
#define LV_MEM_CUSTOM_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE(p)     heap_caps_free(p)
#define LV_MEM_CUSTOM_REALLOC(p,s) heap_caps_realloc(p, s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
