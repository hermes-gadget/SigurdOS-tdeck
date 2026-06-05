// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Companion flood-scope regions — lightweight scope store for
// MeshCore transport-code scoped flooding (companion half only;
// does NOT implement repeater-side RegionMap deny-flood gating).

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace sigurdos {
namespace mesh {

#define SIGURD_MAX_REGIONS 8

struct SigurdRegion {
    char    name[31];     // "#london" or "$crew" — includes prefix
    uint8_t key[16];      // public: SHA256(prefix+name)[0..15]; private: user secret
};

namespace detail {

static constexpr size_t REGION_FILE_HEADER_SIZE = sizeof(uint32_t);

inline int regionFileLoadCount(uint32_t stored_count, size_t file_size, int max) {
    if (max <= 0 || stored_count == 0 || file_size < REGION_FILE_HEADER_SIZE) {
        return 0;
    }
    if (stored_count > SIGURD_MAX_REGIONS) {
        return 0;
    }

    const size_t count = static_cast<size_t>(stored_count);
    const size_t max_count =
        (static_cast<size_t>(-1) - REGION_FILE_HEADER_SIZE) / sizeof(SigurdRegion);
    if (count > max_count) {
        return 0;
    }

    const size_t expected_size = REGION_FILE_HEADER_SIZE + count * sizeof(SigurdRegion);
    if (file_size < expected_size) {
        return 0;
    }

    const int stored_as_int = static_cast<int>(stored_count);
    return stored_as_int < max ? stored_as_int : max;
}

inline bool normalizeRegionName(const char* name, char* out_name, size_t out_size) {
    if (!name || !out_name || out_size == 0) return false;
    out_name[0] = '\0';
    if (name[0] == '\0') return false;

    const bool has_prefix = name[0] == '#' || name[0] == '$';
    const size_t source_len = std::strlen(name);
    const size_t normalized_len = source_len + (has_prefix ? 0u : 1u);
    if (normalized_len >= out_size) return false;

    if (!has_prefix) {
        out_name[0] = '#';
        std::memcpy(out_name + 1, name, source_len + 1);
    } else {
        std::memcpy(out_name, name, source_len + 1);
    }
    return true;
}

inline bool regionListContainsName(const SigurdRegion* list, int count, const char* name) {
    if (!list || !name || count <= 0) return false;
    for (int i = 0; i < count; i++) {
        if (std::strcmp(list[i].name, name) == 0) return true;
    }
    return false;
}

} // namespace detail

// ── Persistence (SPIFFS /regions.dat) ──────────────────
// Returns number of regions loaded (≤ max).
int  loadRegions(SigurdRegion* out, int max);

// Saves the region list. Call after add/remove.
bool saveRegions(const SigurdRegion* list, int count);

// ── Key derivation for public (#) regions ─────────────
// Computes SHA256(name)[0..15]. Returns false if name is not a #
// public region (no key derivation needed) or on internal error.
bool deriveRegionKey(const char* name, uint8_t* out_key16);

} // namespace mesh
} // namespace sigurdos
