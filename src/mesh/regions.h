// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Companion flood-scope regions — lightweight scope store for
// MeshCore transport-code scoped flooding (companion half only;
// does NOT implement repeater-side RegionMap deny-flood gating).

#pragma once
#include <cstdint>
#include <cstddef>

namespace sigurdos {
namespace mesh {

#define SIGURD_MAX_REGIONS 8

struct SigurdRegion {
    char    name[31];     // "#london" or "$crew" — includes prefix
    uint8_t key[16];      // public: SHA256(prefix+name)[0..15]; private: user secret
};

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
