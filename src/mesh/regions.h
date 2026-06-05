// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Region management — thin wrapper over upstream MeshCore RegionMap.
// Provides flood-scope gating, hierarchical region tree, home/default
// region tracking, and persistence via /regions2 (upstream format).

#pragma once

#include <helpers/RegionMap.h>
#include <helpers/TransportKeyStore.h>

namespace sigurdos {
namespace mesh {

// ── Lifecycle ───────────────────────────────────────────

/// Initialize the global RegionMap. Must be called once at boot,
/// before any other region functions. The store must outlive the map.
void regionsInit(TransportKeyStore& store);

/// Load regions from SPIFFS (/regions2). Returns true on success.
bool regionsLoad();

/// Save regions to SPIFFS (/regions2). Returns true on success.
bool regionsSave();

// ── Access the underlying RegionMap ─────────────────────

/// Returns the global RegionMap instance (non-null after regionsInit).
RegionMap* getRegionMap();

// ── CRUD ────────────────────────────────────────────────

/// Add or update a region. For public (#) names, transport keys are
/// auto-derived via TransportKeyStore. For private ($) names, the
/// user must supply keys separately through TransportKeyStore.
/// If parent_name is non-null, the region is placed under that parent.
/// Returns the RegionEntry or nullptr on failure (full, bad name, etc).
::RegionEntry* addRegion(const char* name, const char* parent_name = nullptr);

/// Remove a region by name. Fails if the region has children.
bool removeRegion(const char* name);

/// Find a region by name (with or without # prefix).
::RegionEntry* findRegion(const char* name);

/// Find a region by name prefix (for tab-completion UX).
::RegionEntry* findRegionPrefix(const char* prefix);

/// List all region names into a buffer (comma-separated).
/// Filtered by mask: pass REGION_DENY_FLOOD to get deny-flood regions,
/// 0 to get allow-flood regions, with invert=true to reverse.
/// Returns the number of bytes written (excluding null terminator).
int listRegionNames(char* dest, int max_len, uint8_t mask = 0, bool invert = false);

/// Get the total number of regions (excluding wildcard root).
int getRegionCount();

/// Export the region tree to a string buffer.
size_t exportRegions(char* dest, size_t max_len);

// ── Flood flags ─────────────────────────────────────────

/// Check if a region allows flood forwarding.
bool regionAllowsFlood(const char* name);

/// Set the DENY_FLOOD flag on or off for a region.
bool setRegionFloodAllowed(const char* name, bool allowed);

/// Check if an incoming packet should be denied flood-forwarding.
/// Returns the matching region if denied, nullptr if allowed.
::RegionEntry* regionDeniesFlood(::mesh::Packet* packet);

// ── Home region ─────────────────────────────────────────

/// Get the home region name (or nullptr if none).
const char* getHomeRegionName();

/// Set the home region by name.
bool setHomeRegion(const char* name);

// ── Default scope ───────────────────────────────────────

/// Get the default flood-scope region name (or nullptr if none).
const char* getDefaultScopeName();

/// Set the default flood-scope region by name.
/// Auto-creates the region if it doesn't exist (like upstream).
bool setDefaultScope(const char* name);

// ── Active send scope (NodePrefs-backed) ────────────────

/// Get the active flood-scope region name (empty string = wildcard/unscoped).
const char* getActiveRegion();

/// Set the active flood-scope region name (NodePrefs only — does NOT
/// propagate to mesh; call mesh_wrapper::setActiveRegion for that).
bool setActiveRegionName(const char* name);

// ── Channel sync ────────────────────────────────────────

/// Auto-create #regions from #channels so the user doesn't need to
/// manually add each channel as a flood-scope region.
void syncRegionsFromChannels();

// ── Backward-compat helpers ─────────────────────────────

/// Struct for listing regions in UI-friendly format.
/// Mirrors the old SigurdRegion shape for easy UI migration.
struct RegionInfo {
    char       name[31];     // includes # or $ prefix
    uint16_t   id;
    uint16_t   parent_id;
    uint8_t    flags;
    bool       is_home;
    bool       is_default;
};

/// List all regions into a caller-provided array.
/// Returns count (≤ max, ≤ MAX_REGION_ENTRIES).
int listRegions(RegionInfo* out, int max);

} // namespace mesh
} // namespace sigurdos
