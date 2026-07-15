// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "regions.h"
#include "persistence_store.h"
#include "../hal/prefs.h"
#include <SPIFFS.h>
#include <cstring>

namespace sigurdos {
namespace mesh {

// ── Global state ────────────────────────────────────────

static RegionMap*       g_region_map = nullptr;
static TransportKeyStore* g_region_store = nullptr;
static bool g_regions_dirty = false;

static constexpr const char* REGION_PATH = "/regions2";
static constexpr const char* REGION_RAW_PATH = "/regions2.raw";

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

bool installPrivateRegionKey(const ::RegionEntry& region, const uint8_t* key16) {
    if (!g_region_store || region.id == 0 || region.name[0] != '$' || !key16) {
        return false;
    }
    TransportKey key{};
    memcpy(key.key, key16, sizeof(key.key));
    if (key.isNull() || !g_region_store->saveKeysFor(region.id, &key, 1)) {
        return false;
    }
    TransportKey check{};
    return g_region_store->loadKeysFor(region.id, &check, 1) == 1 &&
        memcmp(check.key, key.key, sizeof(key.key)) == 0;
}

bool removePrivateRegionKey(const ::RegionEntry& region) {
    if (!g_region_store || region.id == 0 || region.name[0] != '$' ||
        !g_region_store->removeKeys(region.id)) return false;
    TransportKey check{};
    return g_region_store->loadKeysFor(region.id, &check, 1) == 0;
}

bool regionsLoad() {
    if (!g_region_map) return false;
    if (!SPIFFS.begin(false)) {
        g_regions_dirty = true;
        return false;
    }

    const detail::RegionStoreFormat format =
        detail::regionStorePrepareLoad(REGION_PATH);
    bool ok = format != detail::RegionStoreFormat::Invalid &&
        g_region_map->load(&SPIFFS, REGION_PATH);
    g_regions_dirty = !ok;

    // A validated upstream-format file is safe to load, then immediately
    // upgraded. If the upgrade cannot commit, retain the loaded map and its
    // dirty flag so a later regionsSave() can retry it.
    if (ok && format == detail::RegionStoreFormat::Legacy) {
        regionsSave();
    }

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
    if (!SPIFFS.begin(false)) {
        g_regions_dirty = true;
        return false;
    }

    SPIFFS.remove(REGION_RAW_PATH);
    const bool serialized = g_region_map->save(&SPIFFS, REGION_RAW_PATH);
    const bool ok = serialized && detail::regionStoreSaveLegacyFile(
        REGION_PATH, REGION_RAW_PATH);
    SPIFFS.remove(REGION_RAW_PATH);
    g_regions_dirty = !ok;
    return ok;
}

bool regionsPersistenceDirty() {
    return g_regions_dirty;
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
    if (!regionsSave()) return nullptr;
    return region;
}

::RegionEntry* addPrivateRegion(const char* name, const uint8_t key[16],
                                const char* parent_name) {
    if (!name || name[0] != '$' || !key || !g_region_store) return nullptr;
    ::RegionEntry* region = addRegion(name, parent_name);
    if (!region) return nullptr;
    const uint16_t region_id = region->id;

    TransportKey transport_key{};
    memcpy(transport_key.key, key, sizeof(transport_key.key));
    if (!g_region_store->saveKeysFor(region_id, &transport_key, 1) ||
        !regionsSave()) {
        g_region_map->removeRegion(*region);
        g_region_store->removeKeys(region_id);
        regionsSave();
        return nullptr;
    }
    return region;
}

bool setPrivateRegionKey(const char* name, const uint8_t key[16]) {
    if (!name || name[0] != '$' || !key || !g_region_store) return false;
    ::RegionEntry* region = findRegion(name);
    if (!region || region->name[0] != '$') return false;

    TransportKey transport_key{};
    memcpy(transport_key.key, key, sizeof(transport_key.key));
    if (!g_region_store->saveKeysFor(region->id, &transport_key, 1)) return false;
    return regionsSave();
}

bool getPrivateRegionKey(const char* name, uint8_t key_out[16]) {
    if (!name || name[0] != '$' || !key_out || !g_region_store) return false;
    ::RegionEntry* region = findRegion(name);
    if (!region || region->name[0] != '$') return false;

    TransportKey transport_key{};
    if (g_region_store->loadKeysFor(region->id, &transport_key, 1) != 1) {
        return false;
    }
    memcpy(key_out, transport_key.key, sizeof(transport_key.key));
    return true;
}

bool removeRegion(const char* name) {
    if (!g_region_map || !name || !name[0]) return false;

    ::RegionEntry* region = g_region_map->findByName(name);
    if (!region) return false;

    const bool was_home = g_region_map->getHomeRegion() == region;
    const bool was_default = g_region_map->getDefaultRegion() == region;
    const uint16_t region_id = region->id;

    // If removing the active region, clear the scope
    if (g_active_name[0] && strcmp(g_active_name, region->name) == 0) {
        setActiveRegionName("");
    }

    bool ok = g_region_map->removeRegion(*region);
    if (!ok) return false;
    if (g_region_store) g_region_store->removeKeys(region_id);
    if (was_home) g_region_map->setHomeRegion(nullptr);
    if (was_default) g_region_map->setDefaultRegion(nullptr);

    // Persist
    if (!regionsSave()) return false;
    if (was_default) {
        NodePrefs prefs = prefs_get();
        prefs.default_scope_key_hex[0] = '\0';
        if (!prefs_set(prefs)) return false;
    }
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
    return regionsSave();
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
    return regionsSave();
}

// ── Default scope ───────────────────────────────────────

const char* getDefaultScopeName() {
    if (!g_region_map) return nullptr;
    ::RegionEntry* def = g_region_map->getDefaultRegion();
    return def ? def->name : nullptr;
}

bool setDefaultScope(const char* name) {
    if (!g_region_map) return false;

    ::RegionEntry* previous = g_region_map->getDefaultRegion();
    const bool changed = (!previous && name && name[0]) ||
        (previous && (!name || !name[0] || strcmp(name, "<null>") == 0 ||
                      strcmp(previous->name, name) != 0));

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

    // Persist and discard an app-supplied key if it belonged to a different
    // default name. Companion SET writes the replacement key immediately.
    if (!regionsSave()) return false;
    if (changed) {
        NodePrefs prefs = prefs_get();
        prefs.default_scope_key_hex[0] = '\0';
        if (!prefs_set(prefs)) return false;
    }
    return true;
}

// ── Active send scope ───────────────────────────────────

const char* getActiveRegion() {
    return g_active_name;
}

static bool commitActiveRegionPrefs(const char* name, const char* key_hex,
                                    bool replace_key) {
    if (name && strlen(name) >= sizeof(g_active_name)) return false;
    NodePrefs np = prefs_get();
    if (name && name[0]) {
        strncpy(np.active_region, name, sizeof(np.active_region) - 1);
        np.active_region[sizeof(np.active_region) - 1] = '\0';
    } else {
        np.active_region[0] = '\0';
    }
    if (replace_key) {
        if (key_hex) {
            strncpy(np.default_scope_key_hex, key_hex,
                    sizeof(np.default_scope_key_hex) - 1);
            np.default_scope_key_hex[sizeof(np.default_scope_key_hex) - 1] = '\0';
        } else {
            np.default_scope_key_hex[0] = '\0';
        }
    }
    if (!prefs_set(np)) return false;

    if (name && name[0]) {
        strncpy(g_active_name, name, sizeof(g_active_name) - 1);
        g_active_name[sizeof(g_active_name) - 1] = '\0';
    } else {
        g_active_name[0] = '\0';
    }
    return true;
}

bool setActiveRegionName(const char* name) {
    return commitActiveRegionPrefs(name, nullptr, false);
}

bool setActiveRegionNameWithKey(const char* name, const char* key_hex) {
    return commitActiveRegionPrefs(name, key_hex, true);
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
