// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "regions.h"
#include "persistence_store.h"
#include "scope_key_hex.h"
#include "region_name.h"
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

static void restoreRegionMap(const RegionMap& snapshot, bool was_dirty) {
    *g_region_map = snapshot;
    g_regions_dirty = was_dirty;
}

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
    return region.id != 0 && region.name[0] == '$' &&
        setPrivateRegionKey(region.name, key16);
}

bool removePrivateRegionKey(const ::RegionEntry& region) {
    if (!g_region_store || region.id == 0 || region.name[0] != '$') {
        return false;
    }
    TransportKey previous[4]{};
    const int previous_count = g_region_store->loadKeysFor(
        region.id, previous, 4);
    const bool previous_dirty = g_regions_dirty;
    if (previous_count <= 0 || !g_region_store->removeKeys(region.id)) {
        return false;
    }
    if (regionsSave()) return true;
    g_region_store->saveKeysFor(region.id, previous, previous_count);
    g_regions_dirty = previous_dirty;
    return false;
}

static int countPrivateRegionKeys() {
    if (!g_region_map || !g_region_store) return 0;
    int count = 0;
    for (int i = 0; i < g_region_map->getCount(); ++i) {
        const ::RegionEntry* region = g_region_map->getByIdx(i);
        if (!region || region->name[0] != '$') continue;
        TransportKey key{};
        if (g_region_store->loadKeysFor(region->id, &key, 1) == 1 &&
            !key.isNull()) {
            ++count;
        }
    }
    return count;
}

static bool readPrivateRegionKey(int index, uint16_t* region_id_out,
                                 uint8_t key_out[16], void*) {
    if (!g_region_map || !g_region_store || index < 0 || !region_id_out ||
        !key_out) {
        return false;
    }
    int output_index = 0;
    for (int i = 0; i < g_region_map->getCount(); ++i) {
        const ::RegionEntry* region = g_region_map->getByIdx(i);
        if (!region || region->name[0] != '$') continue;
        TransportKey key{};
        if (g_region_store->loadKeysFor(region->id, &key, 1) != 1 ||
            key.isNull()) {
            continue;
        }
        if (output_index++ != index) continue;
        *region_id_out = region->id;
        memcpy(key_out, key.key, sizeof(key.key));
        return true;
    }
    return false;
}

static bool loadPrivateRegionKey(uint16_t region_id, const uint8_t key[16],
                                 void*) {
    if (!g_region_map || !g_region_store || region_id == 0 || !key) {
        return false;
    }
    ::RegionEntry* region = g_region_map->findById(region_id);
    if (!region || region->name[0] != '$') return false;
    TransportKey transport_key{};
    memcpy(transport_key.key, key, sizeof(transport_key.key));
    return !transport_key.isNull() &&
        g_region_store->saveKeysFor(region_id, &transport_key, 1);
}

bool regionsLoad() {
    if (!g_region_map || !g_region_store) return false;
    if (!SPIFFS.begin(false)) {
        g_regions_dirty = true;
        return false;
    }

    const detail::RegionStoreFormat format =
        detail::regionStorePrepareLoad(REGION_PATH);
    const bool transactional =
        format == detail::RegionStoreFormat::Transactional;
    bool ok = format != detail::RegionStoreFormat::Invalid;
    if (ok && transactional) {
        SPIFFS.remove(REGION_RAW_PATH);
        ok = detail::regionStoreExtractTransactionalMap(
            REGION_PATH, REGION_RAW_PATH);
    }
    const char* map_path = transactional ? REGION_RAW_PATH : REGION_PATH;
    if (ok) ok = g_region_map->load(&SPIFFS, map_path);
    if (transactional) SPIFFS.remove(REGION_RAW_PATH);

    // TransportKeyStore is RAM-only upstream. Rebuild it exclusively from the
    // same validated v2 snapshot as the RegionMap so names/IDs and private key
    // material can never come from different commits.
    g_region_store->clear();
    if (ok && transactional) {
        ok = detail::regionStoreLoadTransactionalKeys(
            REGION_PATH, loadPrivateRegionKey, nullptr) >= 0;
    }
    g_regions_dirty = !ok;
    if (!ok) {
        g_active_name[0] = '\0';
        return false;
    }

    NodePrefs np = prefs_get();
    bool prefs_quarantined = false;
    if (np.active_region[0] && !regionNameValid(np.active_region)) {
        np.active_region[0] = '\0';
        np.default_scope_key_hex[0] = '\0';
        prefs_quarantined = true;
    }
    if (np.default_scope_key_hex[0]) {
        uint8_t checked_key[16];
        if (!scopeKeyHexDecode(np.default_scope_key_hex, checked_key)) {
            np.default_scope_key_hex[0] = '\0';
            prefs_quarantined = true;
        }
    }
    // RegionMap's default is the canonical persisted scope. Migrate the older
    // active_region-only model once, then keep the compatibility preference in
    // lockstep with the canonical name.
    ::RegionEntry* canonical = g_region_map->getDefaultRegion();
    if (!canonical && np.active_region[0]) {
        canonical = g_region_map->findByName(np.active_region);
        if (canonical) {
            g_region_map->setDefaultRegion(canonical);
            g_regions_dirty = true;
        }
    }

    if (canonical) {
        strncpy(g_active_name, canonical->name, sizeof(g_active_name) - 1);
        g_active_name[sizeof(g_active_name) - 1] = '\0';
    } else {
        g_active_name[0] = '\0';
    }

    // V1 stored only the active private key in NodePrefs. Install that overlay
    // in RAM before the one-time v2 commit. A valid v2 file always wins over a
    // stale preference left behind by an interrupted NVS cleanup.
    if (ok && !transactional && canonical && canonical->name[0] == '$' &&
        strcmp(np.active_region, canonical->name) == 0 &&
        strlen(np.default_scope_key_hex) == SCOPE_KEY_HEX_LEN) {
        uint8_t key[16];
        if (scopeKeyHexDecode(np.default_scope_key_hex, key)) {
            TransportKey transport_key{};
            memcpy(transport_key.key, key, sizeof(transport_key.key));
            if (!transport_key.isNull()) {
                g_region_store->saveKeysFor(
                    canonical->id, &transport_key, 1);
            }
        }
    }

    // Every valid legacy/v1 map is upgraded after the preference overlay has
    // been applied, producing the first transactional metadata+keys snapshot.
    const bool upgraded = ok && !transactional && regionsSave();
    if (ok && !transactional && !upgraded) g_regions_dirty = true;

    if (ok && (transactional || upgraded) &&
        np.default_scope_key_hex[0] != '\0') {
        np.default_scope_key_hex[0] = '\0';
        prefs_quarantined = true;
    }

    if (strcmp(np.active_region, g_active_name) != 0) {
        strncpy(np.active_region, g_active_name, sizeof(np.active_region) - 1);
        np.active_region[sizeof(np.active_region) - 1] = '\0';
        prefs_quarantined = true;
    }

    if (prefs_quarantined) prefs_set(np);

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
    const int key_count = countPrivateRegionKeys();
    const bool ok = serialized &&
        detail::regionStoreSaveTransactionalFile(
            REGION_PATH, REGION_RAW_PATH, key_count,
            readPrivateRegionKey, nullptr);
    SPIFFS.remove(REGION_RAW_PATH);
    g_regions_dirty = !ok;
    return ok;
}

bool regionsPersistenceDirty() {
    return g_regions_dirty;
}

// ── CRUD ────────────────────────────────────────────────

::RegionEntry* addRegion(const char* name, const char* parent_name) {
    if (!g_region_map || !regionNameValid(name) ||
        (parent_name && parent_name[0] && !regionNameValid(parent_name))) {
        return nullptr;
    }

    const RegionMap previous = *g_region_map;
    const bool previous_dirty = g_regions_dirty;

    uint16_t parent_id = 0;  // root/wildcard
    if (parent_name && parent_name[0]) {
        ::RegionEntry* parent = g_region_map->findByName(parent_name);
        if (parent) {
            parent_id = parent->id;
        } else {
            // Parent doesn't exist — create it first
            parent = g_region_map->putRegion(parent_name, 0);
            if (!parent) {
                restoreRegionMap(previous, previous_dirty);
                return nullptr;
            }
            parent_id = parent->id;
        }
    }

    ::RegionEntry* region = g_region_map->putRegion(name, parent_id);
    if (!region) {
        restoreRegionMap(previous, previous_dirty);
        return nullptr;
    }

    // New regions default to ALLOW flood (flags = 0), matching upstream
    // behaviour since commit d131e8ae ("companion: RegionMap now used in Datastore")
    region->flags = 0;

    // Persist
    if (!regionsSave()) {
        restoreRegionMap(previous, previous_dirty);
        return nullptr;
    }
    return region;
}

::RegionEntry* addPrivateRegion(const char* name, const uint8_t key[16],
                                const char* parent_name) {
    if (!name || name[0] != '$' || !key || !g_region_map || !g_region_store ||
        !regionNameValid(name) ||
        (parent_name && parent_name[0] && !regionNameValid(parent_name)) ||
        g_region_map->findByName(name)) {
        return nullptr;
    }
    TransportKey transport_key{};
    memcpy(transport_key.key, key, sizeof(transport_key.key));
    if (transport_key.isNull()) return nullptr;

    const RegionMap previous = *g_region_map;
    const bool previous_dirty = g_regions_dirty;
    uint16_t parent_id = 0;
    if (parent_name && parent_name[0]) {
        ::RegionEntry* parent = g_region_map->findByName(parent_name);
        if (!parent) parent = g_region_map->putRegion(parent_name, 0);
        if (!parent) {
            restoreRegionMap(previous, previous_dirty);
            return nullptr;
        }
        parent_id = parent->id;
    }
    ::RegionEntry* region = g_region_map->putRegion(name, parent_id);
    if (!region) {
        restoreRegionMap(previous, previous_dirty);
        return nullptr;
    }
    region->flags = 0;
    const uint16_t region_id = region->id;
    if (!g_region_store->saveKeysFor(region_id, &transport_key, 1) ||
        !regionsSave()) {
        g_region_store->removeKeys(region_id);
        restoreRegionMap(previous, previous_dirty);
        return nullptr;
    }
    return region;
}

bool setPrivateRegionKey(const char* name, const uint8_t key[16]) {
    if (!name || name[0] != '$' || !key || !g_region_store) return false;
    ::RegionEntry* region = findRegion(name);
    if (!region || region->name[0] != '$') return false;

    TransportKey previous[4]{};
    const int previous_count = g_region_store->loadKeysFor(
        region->id, previous, 4);
    const bool previous_dirty = g_regions_dirty;
    TransportKey transport_key{};
    memcpy(transport_key.key, key, sizeof(transport_key.key));
    if (transport_key.isNull()) return false;
    if (!g_region_store->saveKeysFor(region->id, &transport_key, 1)) return false;
    if (regionsSave()) return true;
    if (previous_count > 0) {
        g_region_store->saveKeysFor(region->id, previous, previous_count);
    } else {
        g_region_store->removeKeys(region->id);
    }
    g_regions_dirty = previous_dirty;
    return false;
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
    if (!g_region_map || !regionNameValid(name)) return false;

    ::RegionEntry* region = g_region_map->findByName(name);
    if (!region) return false;

    const bool was_home = g_region_map->getHomeRegion() == region;
    const bool was_default = g_region_map->getDefaultRegion() == region;
    const uint16_t region_id = region->id;
    const bool was_active = g_active_name[0] &&
        strcmp(g_active_name, region->name) == 0;
    const RegionMap previous = *g_region_map;
    const bool previous_dirty = g_regions_dirty;
    const NodePrefs previous_prefs = prefs_get();
    char previous_active[sizeof(g_active_name)];
    memcpy(previous_active, g_active_name, sizeof(previous_active));

    bool ok = g_region_map->removeRegion(*region);
    if (!ok) return false;
    if (was_home) g_region_map->setHomeRegion(nullptr);
    if (was_default) g_region_map->setDefaultRegion(nullptr);
    if (was_active) g_active_name[0] = '\0';

    if (!regionsSave()) {
        restoreRegionMap(previous, previous_dirty);
        memcpy(g_active_name, previous_active, sizeof(g_active_name));
        return false;
    }
    if (was_active || was_default) {
        NodePrefs prefs = previous_prefs;
        if (was_active) prefs.active_region[0] = '\0';
        if (was_default) {
            prefs.default_scope_key_hex[0] = '\0';
        }
        if (!prefs_set(prefs)) {
            restoreRegionMap(previous, previous_dirty);
            memcpy(g_active_name, previous_active, sizeof(g_active_name));
            regionsSave();
            return false;
        }
    }
    if (name[0] == '$' && g_region_store &&
        !g_region_store->removeKeys(region_id)) {
        restoreRegionMap(previous, previous_dirty);
        memcpy(g_active_name, previous_active, sizeof(g_active_name));
        prefs_set(previous_prefs);
        regionsSave();
        return false;
    }
    return true;
}

::RegionEntry* findRegion(const char* name) {
    if (!g_region_map || !regionNameValid(name)) return nullptr;
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

    const RegionMap previous = *g_region_map;
    const bool previous_dirty = g_regions_dirty;

    if (allowed) {
        r->flags &= ~REGION_DENY_FLOOD;
    } else {
        r->flags |= REGION_DENY_FLOOD;
    }

    // Persist
    if (regionsSave()) return true;
    restoreRegionMap(previous, previous_dirty);
    return false;
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

    const RegionMap previous = *g_region_map;
    const bool previous_dirty = g_regions_dirty;

    if (!name || !name[0]) {
        g_region_map->setHomeRegion(nullptr);
    } else {
        ::RegionEntry* r = g_region_map->findByName(name);
        if (!r) return false;
        g_region_map->setHomeRegion(r);
    }

    // Persist
    if (regionsSave()) return true;
    restoreRegionMap(previous, previous_dirty);
    return false;
}

// ── Default scope ───────────────────────────────────────

const char* getDefaultScopeName() {
    if (!g_region_map) return nullptr;
    ::RegionEntry* def = g_region_map->getDefaultRegion();
    return def ? def->name : nullptr;
}

static bool setDefaultScopeInMemory(const char* name) {
    if (!g_region_map) return false;

    if (!name || !name[0] || strcmp(name, "<null>") == 0) {
        g_region_map->setDefaultRegion(nullptr);
    } else {
        if (!regionNameValid(name)) return false;
        // Auto-create the region if it doesn't exist (matches upstream CLI)
        ::RegionEntry* def = g_region_map->findByName(name);
        if (!def) {
            def = g_region_map->putRegion(name, 0);
        }
        if (!def) return false;
        def->flags &= ~REGION_DENY_FLOOD;  // default scope must allow flood
        g_region_map->setDefaultRegion(def);
    }
    return true;
}

bool setDefaultScope(const char* name) {
    if (!g_region_map) return false;
    const RegionMap previous = *g_region_map;
    const bool previous_dirty = g_regions_dirty;
    if (!setDefaultScopeInMemory(name)) return false;
    if (regionsSave()) return true;
    restoreRegionMap(previous, previous_dirty);
    return false;
}

// ── Active send scope ───────────────────────────────────

const char* getActiveRegion() {
    return g_active_name;
}

static bool commitActiveRegionPrefs(const char* name, const char* key_hex,
                                    bool replace_key) {
    if (name && name[0] && !regionNameValid(name)) return false;
    uint8_t checked_key[16];
    if (replace_key && key_hex &&
        (!name || name[0] != '$' || !scopeKeyHexDecode(key_hex, checked_key))) {
        return false;
    }
    const NodePrefs previous_prefs = prefs_get();
    NodePrefs np = previous_prefs;
    const RegionMap previous_map = *g_region_map;
    const bool previous_regions_dirty = g_regions_dirty;

    if (name && name[0]) {
        strncpy(np.active_region, name, sizeof(np.active_region) - 1);
        np.active_region[sizeof(np.active_region) - 1] = '\0';
    } else {
        np.active_region[0] = '\0';
    }
    if (replace_key) {
        // Private key material is part of the transactional /regions2 v2
        // snapshot. This legacy NVS field is accepted only as a migration
        // input and must not become a second source of truth again.
        np.default_scope_key_hex[0] = '\0';
    }
    if (!prefs_set(np)) return false;

    // RegionMap is the canonical reboot-time source. Keep its persisted
    // default in lockstep with the compatibility preference used at runtime.
    if (!setDefaultScope(name)) {
        // Atomic region persistence leaves the previous live file intact on
        // failure. Restore the in-memory selection and preference snapshot so
        // callers do not observe a half-applied scope change.
        restoreRegionMap(previous_map, previous_regions_dirty);
        prefs_set(previous_prefs);
        return false;
    }

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
