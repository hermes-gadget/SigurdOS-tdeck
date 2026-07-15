// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#pragma once

#include <cstddef>
#include <cstdint>

#define MALLOC_CAP_SPIRAM   0x01u
#define MALLOC_CAP_INTERNAL 0x02u
#define MALLOC_CAP_8BIT     0x04u

void* heap_caps_malloc(size_t size, uint32_t caps);
void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps);
void heap_caps_free(void* ptr);
