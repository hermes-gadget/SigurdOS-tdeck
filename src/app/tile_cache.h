#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Tile cache — LRU eviction for offline map tiles.
// Default 4 entries (~524 KB PSRAM at 256×256 RGB565 each).

#include <cstdint>
#include <cstddef>

// A single tile cache entry
struct CachedTile {
    int      zoom;
    int      tx;
    int      ty;
    uint16_t* pixels;    // RGBA565 pixel data (or nullptr if empty)
    uint64_t  last_used; // monotonic clock stamp (set by cache_lookup)
};

// Default tile cache size
static constexpr int TILE_CACHE_SIZE = 4;
static constexpr int MISSING_TILE_CACHE_SIZE = 24;
static constexpr uint32_t MISSING_TILE_CACHE_TTL_MS = 30000;
static constexpr int MAP_TILE_LOAD_BUDGET_PER_RENDER = 2;

struct MissingTileCacheEntry {
    int zoom;
    int tx;
    int ty;
    uint32_t failed_at_ms;
    bool valid;
};

struct TileLoadBudget {
    int limit;
    int used;

    explicit TileLoadBudget(int max_attempts)
        : limit(max_attempts > 0 ? max_attempts : 0), used(0) {}

    bool consume() {
        if (used >= limit) return false;
        used++;
        return true;
    }

    int remaining() const { return limit - used; }
};

// ── Lifecycle ───────────────────────────────────────────────

// Zero-initialise all entries in the cache array (pixels = nullptr)
void tile_cache_init(CachedTile* cache, int count);

// ── Lookup ──────────────────────────────────────────────────

// Look up a tile by coordinates.
// On hit: updates last_used, increments *clock, returns the entry.
// On miss: returns nullptr.
CachedTile* tile_cache_lookup(CachedTile* cache, int count,
                              int zoom, int tx, int ty,
                              uint64_t* clock);

// ── Eviction ────────────────────────────────────────────────

// Find a slot for a new tile.
// Returns the first empty slot (pixels == nullptr), or the LRU entry
// (oldest last_used) if all slots are full.
// Does NOT free pixels — the caller must free the returned entry's
// pixels pointer before overwriting it.
CachedTile* tile_cache_evict_slot(CachedTile* cache, int count);

// Prepare a selected slot for new pixels. Fixed-size tile buffers are retained
// across eviction; allocation is needed only while initially filling the cache.
template <typename AllocateFn>
uint16_t* tile_cache_prepare_slot(CachedTile* slot, std::size_t bytes,
                                  AllocateFn allocate) {
    if (!slot || bytes == 0) return nullptr;
    slot->zoom = 0;
    slot->tx = 0;
    slot->ty = 0;
    slot->last_used = 0;
    if (!slot->pixels) {
        slot->pixels = static_cast<uint16_t*>(allocate(bytes));
    }
    return slot->pixels;
}

// ── Missing-tile negative cache ───────────────────────────

void missing_tile_cache_init(MissingTileCacheEntry* cache, int count);
bool missing_tile_cache_contains(MissingTileCacheEntry* cache, int count,
                                 int zoom, int tx, int ty,
                                 uint32_t now_ms, uint32_t ttl_ms);
void missing_tile_cache_record(MissingTileCacheEntry* cache, int count,
                               int zoom, int tx, int ty, uint32_t now_ms);
int missing_tile_cache_active_count(MissingTileCacheEntry* cache, int count,
                                    uint32_t now_ms, uint32_t ttl_ms);
