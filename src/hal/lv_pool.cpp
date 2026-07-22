// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
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


// LVGL memory pool allocator with PSRAM fallback.
//
// lv_conf.h declares sigurdos_lv_pool_alloc() and sets LV_MEM_POOL_ALLOC
// to call it with LV_MEM_SIZE. This function tries to allocate LVGL's TLSF
// memory pool from PSRAM first; if PSRAM is unavailable (faulty/unpopulated
// chip), it falls back to internal DRAM so the firmware can still boot.
//
// The pool size is driven entirely by LV_MEM_SIZE in lv_conf.h — this file
// has no hardcoded size constant, eliminating the risk of a maintenance
// mismatch.

#include <cstddef>
#include <Arduino.h>
#include <esp_heap_caps.h>

#include "diagnostics/log.h"

extern "C" void* sigurdos_lv_pool_alloc(size_t size)
{
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
    // PSRAM unavailable — fall back to internal DRAM
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

extern "C" [[noreturn]] void sigurdos_lv_assert_handler(void)
{
    SIG_LOGE("LVGL assertion failed; halting");
#if !defined(SIGURDOS_COMPANION_USB) || !SIGURDOS_COMPANION_USB
    Serial.flush();
#endif
    while (true) delay(1000);
}
