// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// Tile cache — LRU eviction for offline map tiles.
// 4 entries default, uint64_t monotonic clock (safe from wrap).

#include "tile_cache.h"

void tile_cache_init(CachedTile* cache, int count) {
    if (!cache || count <= 0) return;

    for (int i = 0; i < count; i++) {
        cache[i].pixels = nullptr;
        cache[i].last_used = 0;
    }
}

CachedTile* tile_cache_lookup(CachedTile* cache, int count,
                               int zoom, int tx, int ty,
                               uint64_t* clock) {
    if (!cache || count <= 0 || !clock) return nullptr;

    for (int i = 0; i < count; i++) {
        if (cache[i].pixels &&
            cache[i].zoom == zoom &&
            cache[i].tx == tx &&
            cache[i].ty == ty) {
            cache[i].last_used = ++(*clock);
            return &cache[i];
        }
    }
    return nullptr;
}

CachedTile* tile_cache_evict_slot(CachedTile* cache, int count) {
    if (!cache || count <= 0) return nullptr;

    int lru = 0;
    for (int i = 0; i < count; i++) {
        if (!cache[i].pixels) return &cache[i];
        if (cache[i].last_used < cache[lru].last_used) lru = i;
    }
    return &cache[lru];
}

void missing_tile_cache_init(MissingTileCacheEntry* cache, int count) {
    if (!cache || count <= 0) return;
    for (int i = 0; i < count; i++) cache[i] = {0, 0, 0, 0, false};
}

bool missing_tile_cache_contains(MissingTileCacheEntry* cache, int count,
                                 int zoom, int tx, int ty,
                                 uint32_t now_ms, uint32_t ttl_ms) {
    if (!cache || count <= 0 || ttl_ms == 0) return false;
    for (int i = 0; i < count; i++) {
        if (!cache[i].valid) continue;
        if ((uint32_t)(now_ms - cache[i].failed_at_ms) >= ttl_ms) {
            cache[i].valid = false;
            continue;
        }
        if (cache[i].zoom == zoom && cache[i].tx == tx && cache[i].ty == ty) {
            return true;
        }
    }
    return false;
}

void missing_tile_cache_record(MissingTileCacheEntry* cache, int count,
                               int zoom, int tx, int ty, uint32_t now_ms) {
    if (!cache || count <= 0) return;
    int first_empty = -1;
    int oldest = 0;
    uint32_t oldest_age = 0;
    for (int i = 0; i < count; i++) {
        if (cache[i].valid && cache[i].zoom == zoom &&
            cache[i].tx == tx && cache[i].ty == ty) {
            cache[i].failed_at_ms = now_ms;
            return;
        }
        if (!cache[i].valid) {
            if (first_empty < 0) first_empty = i;
            continue;
        }
        const uint32_t age = (uint32_t)(now_ms - cache[i].failed_at_ms);
        if (age > oldest_age) {
            oldest = i;
            oldest_age = age;
        }
    }
    const int slot = first_empty >= 0 ? first_empty : oldest;
    cache[slot] = {zoom, tx, ty, now_ms, true};
}

int missing_tile_cache_active_count(MissingTileCacheEntry* cache, int count,
                                    uint32_t now_ms, uint32_t ttl_ms) {
    if (!cache || count <= 0 || ttl_ms == 0) return 0;
    int active = 0;
    for (int i = 0; i < count; i++) {
        if (!cache[i].valid) continue;
        if ((uint32_t)(now_ms - cache[i].failed_at_ms) >= ttl_ms) {
            cache[i].valid = false;
        } else {
            active++;
        }
    }
    return active;
}
