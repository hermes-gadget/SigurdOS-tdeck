// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "regions.h"
#include "../hal/prefs.h"
#include <SPIFFS.h>
#include <cstring>

namespace sigurdos {
namespace mesh {

// ── Global state ────────────────────────────────────────

static RegionMap*       g_region_map = nullptr;
static TransportKeyStore* g_region_store = nullptr;

// Active scope name cache (avoids repeated NodePrefs reads).
// Empty string = wildcard/unscoped.
static char g_active_name[31] = {0};

// ── Lifecycle ───────────────────────────────────────────

void regionsInit(TransportKeyStore& store) {
    g_region_store = &store;

    static RegionMap s_map(store);
    g_region_map = &s_map;
}

RegionMap* getRegionMap() {
    return g_region_map;
}

bool regionsLoad() {
    if (!g_region_map) return false;
    if (!SPIFFS.begin(true)) return false;

    bool ok = g_region_map->load(&SPIFFS, "/regions2");
    SPIFFS.end();

    // Restore active scope from NodePrefs
    NodePrefs np = prefs_get();
    if (np.active_region[0]) {
        strncpy(g_active_name, np.active_region, sizeof(g_active_name) - 1);
        g_active_name[sizeof(g_active_name) - 1] = '\0';
    } else {
        g_active_name[0] = '\0';
    }

    return ok;
}

bool regionsSave() {
    if (!g_region_map) return false;
    if (!SPIFFS.begin(true)) return false;

    bool ok = g_region_map->save(&SPIFFS, "/regions2");
    SPIFFS.end();
    return ok;
}

// ── CRUD ────────────────────────────────────────────────

::RegionEntry* addRegion(const char* name, const char* parent_name) {
    if (!g_region_map || !name || !name[0]) return nullptr;

    uint16_t parent_id = 0;  // root/wildcard
    if (parent_name && parent_name[0]) {
        ::RegionEntry* parent = g_region_map->findByName(parent_name);
        if (parent) {
            parent_id = parent->id;
        } else {
            // Parent doesn't exist — create it first
            parent = g_region_map->putRegion(parent_name, 0);
            if (!parent) return nullptr;
            parent_id = parent->id;
        }
    }

    ::RegionEntry* region = g_region_map->putRegion(name, parent_id);
    if (!region) return nullptr;

    // New regions default to ALLOW flood (flags = 0), matching upstream
    // behaviour since commit d131e8ae ("companion: RegionMap now used in Datastore")
    region->flags = 0;

    // Persist
    regionsSave();

    return region;
}

bool removeRegion(const char* name) {
    if (!g_region_map || !name || !name[0]) return false;

    ::RegionEntry* region = g_region_map->findByName(name);
    if (!region) return false;

    // If removing the active region, clear the scope
    if (g_active_name[0] && strcmp(g_active_name, region->name) == 0) {
        setActiveRegionName("");
    }

    bool ok = g_region_map->removeRegion(*region);
    if (!ok) return false;

    // Persist
    regionsSave();
    return true;
}

::RegionEntry* findRegion(const char* name) {
    if (!g_region_map || !name) return nullptr;
    return g_region_map->findByName(name);
}

::RegionEntry* findRegionPrefix(const char* prefix) {
    if (!g_region_map || !prefix) return nullptr;
    return g_region_map->findByNamePrefix(prefix);
}

int listRegionNames(char* dest, int max_len, uint8_t mask, bool invert) {
    if (!g_region_map || !dest || max_len <= 0) return 0;
    return g_region_map->exportNamesTo(dest, max_len, mask, invert);
}

int getRegionCount() {
    if (!g_region_map) return 0;
    return g_region_map->getCount();
}

size_t exportRegions(char* dest, size_t max_len) {
    if (!g_region_map || !dest || max_len == 0) return 0;
    return g_region_map->exportTo(dest, max_len);
}

// ── Flood flags ─────────────────────────────────────────

bool regionAllowsFlood(const char* name) {
    ::RegionEntry* r = findRegion(name);
    if (!r) return true;  // unknown regions: default allow
    return (r->flags & REGION_DENY_FLOOD) == 0;
}

bool setRegionFloodAllowed(const char* name, bool allowed) {
    ::RegionEntry* r = findRegion(name);
    if (!r) return false;

    if (allowed) {
        r->flags &= ~REGION_DENY_FLOOD;
    } else {
        r->flags |= REGION_DENY_FLOOD;
    }

    // Persist
    regionsSave();
    return true;
}

::RegionEntry* regionDeniesFlood(::mesh::Packet* packet) {
    if (!g_region_map || !packet) return nullptr;
    return g_region_map->findMatch(packet, REGION_DENY_FLOOD);
}

// ── Home region ─────────────────────────────────────────

const char* getHomeRegionName() {
    if (!g_region_map) return nullptr;
    ::RegionEntry* home = g_region_map->getHomeRegion();
    return home ? home->name : nullptr;
}

bool setHomeRegion(const char* name) {
    if (!g_region_map) return false;

    if (!name || !name[0]) {
        g_region_map->setHomeRegion(nullptr);
    } else {
        ::RegionEntry* r = g_region_map->findByName(name);
        if (!r) return false;
        g_region_map->setHomeRegion(r);
    }

    // Persist
    regionsSave();
    return true;
}

// ── Default scope ───────────────────────────────────────

const char* getDefaultScopeName() {
    if (!g_region_map) return nullptr;
    ::RegionEntry* def = g_region_map->getDefaultRegion();
    return def ? def->name : nullptr;
}

bool setDefaultScope(const char* name) {
    if (!g_region_map) return false;

    if (!name || !name[0] || strcmp(name, "<null>") == 0) {
        g_region_map->setDefaultRegion(nullptr);
    } else {
        // Auto-create the region if it doesn't exist (matches upstream CLI)
        ::RegionEntry* def = g_region_map->findByName(name);
        if (!def) {
            def = g_region_map->putRegion(name, 0);
        }
        if (!def) return false;
        def->flags &= ~REGION_DENY_FLOOD;  // default scope must allow flood
        g_region_map->setDefaultRegion(def);
    }

    // Persist
    regionsSave();
    return true;
}

// ── Active send scope ───────────────────────────────────

const char* getActiveRegion() {
    return g_active_name;
}

bool setActiveRegionName(const char* name) {
    // Update NodePrefs (called by mesh_wrapper after g_mesh propagation)
    NodePrefs np = prefs_get();
    if (name && name[0]) {
        strncpy(np.active_region, name, sizeof(np.active_region) - 1);
        np.active_region[sizeof(np.active_region) - 1] = '\0';
        strncpy(g_active_name, name, sizeof(g_active_name) - 1);
        g_active_name[sizeof(g_active_name) - 1] = '\0';
    } else {
        np.active_region[0] = '\0';
        g_active_name[0] = '\0';
    }
    prefs_set(np);

    return true;
}

// ── Channel sync ────────────────────────────────────────
// Defined in mesh_wrapper.cpp (needs channel list access)

// ── Backward-compat list helper ─────────────────────────

int listRegions(RegionInfo* out, int max) {
    if (!g_region_map || !out || max <= 0) return 0;

    int count = g_region_map->getCount();
    if (count > max) count = max;

    ::RegionEntry* home = g_region_map->getHomeRegion();
    ::RegionEntry* def  = g_region_map->getDefaultRegion();

    for (int i = 0; i < count; i++) {
        const ::RegionEntry* src = g_region_map->getByIdx(i);
        if (!src) continue;

        RegionInfo& dst = out[i];
        strncpy(dst.name, src->name, sizeof(dst.name) - 1);
        dst.name[sizeof(dst.name) - 1] = '\0';
        dst.id        = src->id;
        dst.parent_id = src->parent;
        dst.flags     = src->flags;
        dst.is_home   = (home && home->id == src->id);
        dst.is_default = (def && def->id == src->id);
    }

    return count;
}

} // namespace mesh
} // namespace sigurdos
