#pragma once

#define LV_CONF_H

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


// SigurdOS LVGL Configuration — ST7789 320x240, 16-bit color

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
#define LV_FONT_MONTSERRAT_24     1

// The physical keyboard, trackball, and touch controller are LVGL input
// devices. They do not require the optional on-screen keyboard widget.
#define LV_USE_KEYBOARD           0

// Layout
#define LV_USE_FLEX               1
#define LV_USE_GRID               1
#define LV_USE_SNAPSHOT           1

// International keyboard layouts. Bidi ordering and Arabic contextual
// shaping are applied by LVGL before glyph lookup.
#define LV_USE_BIDI               1
#define LV_BIDI_BASE_DIR_DEF      LV_BASE_DIR_AUTO
#define LV_USE_ARABIC_PERSIAN_CHARS 1

// Memory — keep LVGL's TLSF pool in PSRAM, not internal DRAM.
//
// NOTE: the old `LV_MEM_CUSTOM_*` macros below are LVGL v8 names. This project
// is on LVGL v9, which ignores them entirely — so they were a silent no-op and
// LVGL placed its pool as a static array in internal DRAM (the big
// `work_mem_int` symbol). v9 instead uses LV_USE_STDLIB_MALLOC + a pool
// allocator: defining LV_MEM_POOL_ALLOC makes lv_mem_init() create the TLSF
// pool from PSRAM and drops the internal `work_mem_int` array completely.
//
// PSRAM fallback: if PSRAM is unavailable (faulty hardware, unpopulated chip),
// we fall back to internal DRAM so the firmware can still boot and render the
// UI when enough internal memory is available.
//
// Implementation: hal/lv_pool.cpp
#define LV_USE_STDLIB_MALLOC      LV_STDLIB_BUILTIN
#define LV_MEM_POOL_INCLUDE       <esp_heap_caps.h>
#define LV_MEM_SIZE               (128 * 1024U)

#ifndef __ASSEMBLER__
#ifdef __cplusplus
extern "C" {
#endif
void* sigurdos_lv_pool_alloc(unsigned long size);
void sigurdos_lv_assert_handler(void);
#ifdef __cplusplus
}
#endif
#define LV_MEM_POOL_ALLOC(size)   sigurdos_lv_pool_alloc(size)
#define LV_ASSERT_HANDLER         sigurdos_lv_assert_handler();
#endif
