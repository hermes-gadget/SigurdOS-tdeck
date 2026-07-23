// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "esp_heap_caps.h"

#include <gtest/gtest.h>
#include <cstdlib>

namespace {

struct HeapCallLog {
    uint32_t malloc_caps[4] = {};
    uint32_t realloc_caps[4] = {};
    int malloc_count = 0;
    int realloc_count = 0;
    int free_count = 0;
    bool fail_spiram_malloc = false;
    bool fail_spiram_realloc = false;
};

HeapCallLog g_heap;

void reset_heap_log()
{
    g_heap = {};
}

} // anonymous namespace

void* heap_caps_malloc(size_t size, uint32_t caps)
{
    if (g_heap.malloc_count < 4) {
        g_heap.malloc_caps[g_heap.malloc_count] = caps;
    }
    g_heap.malloc_count++;

    if ((caps & MALLOC_CAP_SPIRAM) && g_heap.fail_spiram_malloc) {
        return nullptr;
    }
    return std::malloc(size);
}

void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps)
{
    if (g_heap.realloc_count < 4) {
        g_heap.realloc_caps[g_heap.realloc_count] = caps;
    }
    g_heap.realloc_count++;

    if ((caps & MALLOC_CAP_SPIRAM) && g_heap.fail_spiram_realloc) {
        return nullptr;
    }
    return std::realloc(ptr, size);
}

void heap_caps_free(void* ptr)
{
    g_heap.free_count++;
    std::free(ptr);
}

#include "../../src/app/lodepng_alloc.cpp"

namespace {

TEST(LodepngAlloc, MallocUsesSpiramFirst) {
    reset_heap_log();

    void* ptr = lodepng_malloc(32);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(g_heap.malloc_count, 1);
    EXPECT_EQ(g_heap.malloc_caps[0], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    lodepng_free(ptr);
}

TEST(LodepngAlloc, MallocFallsBackToInternalMemory) {
    reset_heap_log();
    g_heap.fail_spiram_malloc = true;

    void* ptr = lodepng_malloc(32);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(g_heap.malloc_count, 2);
    EXPECT_EQ(g_heap.malloc_caps[0], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    EXPECT_EQ(g_heap.malloc_caps[1], MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    lodepng_free(ptr);
}

TEST(LodepngAlloc, LargeMallocNeverFallsBackToInternalMemory) {
    reset_heap_log();
    g_heap.fail_spiram_malloc = true;

    void* ptr = lodepng_malloc(SIGURDOS_MAP_INTERNAL_FALLBACK_MAX_BYTES + 1U);
    EXPECT_EQ(ptr, nullptr);
    EXPECT_EQ(g_heap.malloc_count, 1);
    EXPECT_EQ(g_heap.malloc_caps[0], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

TEST(LodepngAlloc, ReallocFallsBackToInternalMemory) {
    reset_heap_log();
    void* ptr = std::malloc(8);
    ASSERT_NE(ptr, nullptr);
    g_heap.fail_spiram_realloc = true;

    void* resized = lodepng_realloc(ptr, 64);
    ASSERT_NE(resized, nullptr);
    EXPECT_EQ(g_heap.realloc_count, 2);
    EXPECT_EQ(g_heap.realloc_caps[0], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    EXPECT_EQ(g_heap.realloc_caps[1], MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    lodepng_free(resized);
}

TEST(LodepngAlloc, LargeReallocNeverFallsBackToInternalMemory) {
    reset_heap_log();
    void* ptr = std::malloc(8);
    ASSERT_NE(ptr, nullptr);
    g_heap.fail_spiram_realloc = true;

    void* resized = lodepng_realloc(
        ptr, SIGURDOS_MAP_INTERNAL_FALLBACK_MAX_BYTES + 1U);
    EXPECT_EQ(resized, nullptr);
    EXPECT_EQ(g_heap.realloc_count, 1);
    EXPECT_EQ(g_heap.realloc_caps[0], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    heap_caps_free(ptr);
}

TEST(LodepngAlloc, FreeDelegatesToHeapCapsFree) {
    reset_heap_log();
    void* ptr = std::malloc(16);
    ASSERT_NE(ptr, nullptr);

    lodepng_free(ptr);
    EXPECT_EQ(g_heap.free_count, 1);
}

} // anonymous namespace
