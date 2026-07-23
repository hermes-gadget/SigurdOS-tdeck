// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace mesh {

static constexpr size_t SIGURDOS_CONTACT_PUBKEY_LEN = 32;
static constexpr size_t SIGURDOS_CONTACT_ID_LEN =
    SIGURDOS_CONTACT_PUBKEY_LEN * 2;
static constexpr size_t SIGURDOS_CONTACT_ID_BUFFER_LEN =
    SIGURDOS_CONTACT_ID_LEN + 1;
static constexpr size_t SIGURDOS_CONTACT_NAME_LEN = 32;
static constexpr size_t SIGURDOS_CONTACT_PATH_LEN = 64;
static constexpr uint8_t SIGURDOS_CONTACT_PATH_UNKNOWN = 0xFF;

// Lightweight names for MeshCore's AdvertDataHelpers.h wire constants. Keep
// this storage/parser header independent of MeshCore's hardware-heavy graph.
static constexpr uint8_t MESHCORE_ADV_TYPE_NONE = 0;
static constexpr uint8_t MESHCORE_ADV_TYPE_CHAT = 1;
static constexpr uint8_t MESHCORE_ADV_TYPE_REPEATER = 2;
static constexpr uint8_t MESHCORE_ADV_TYPE_ROOM = 3;
static constexpr uint8_t MESHCORE_ADV_TYPE_SENSOR = 4;

struct StoredContact {
    uint8_t pub_key[SIGURDOS_CONTACT_PUBKEY_LEN];
    char name[SIGURDOS_CONTACT_NAME_LEN];
    uint8_t type;
    uint8_t flags;
    uint8_t out_path_len;
    uint8_t out_path[SIGURDOS_CONTACT_PATH_LEN];
    uint32_t last_advert_timestamp;
    uint32_t lastmod;
    int32_t gps_lat;
    int32_t gps_lon;
    uint32_t sync_since;
};

using ContactStoreReadFn = bool (*)(int index, StoredContact* out, void* ctx);
using ContactStoreWriteFn = bool (*)(const StoredContact& contact, void* ctx);

namespace detail {
static constexpr size_t CONTACT_STORE_V1_RECORD_SIZE =
    SIGURDOS_CONTACT_PUBKEY_LEN +
    SIGURDOS_CONTACT_NAME_LEN +
    1 +  // type
    1;   // perm
static constexpr size_t CONTACT_STORE_RECORD_SIZE =
    SIGURDOS_CONTACT_PUBKEY_LEN +
    SIGURDOS_CONTACT_NAME_LEN +
    1 +  // type
    1 +  // full flags
    1 +  // output path length
    SIGURDOS_CONTACT_PATH_LEN +
    4 +  // last advert timestamp
    4 +  // last modification timestamp
    4 +  // GPS latitude
    4 +  // GPS longitude
    4;   // sync cursor

// File-format magic. Read as a little-endian int32 these bytes are
// 0xB1434753 — negative — so firmware older than the versioned format
// reads them as the legacy contact count and rejects the file via its
// existing `n <= 0` check: a downgrade after upgrade loses saved
// contacts but cannot ingest garbage.
static constexpr uint8_t CONTACT_STORE_MAGIC[4] = {'S', 'G', 'C', 0xB1};
static constexpr uint8_t CONTACT_STORE_VERSION = 2;

void writeContactRecord(const StoredContact& contact, uint8_t* rec, size_t len);
bool readContactRecord(StoredContact& contact, const uint8_t* rec, size_t len);
bool readContactRecordV1(StoredContact& contact, const uint8_t* rec, size_t len);
} // namespace detail

bool contactStoreBegin();
bool contactStoreClear();
bool contactStoreSave(int count, ContactStoreReadFn read, void* ctx);
int  contactStoreLoad(ContactStoreWriteFn write, void* ctx);
bool contactStoreSaveAll(const StoredContact* contacts, int count);
int  contactStoreLoadAll(StoredContact* out, int max);

// Canonical validation shared by manual and URI contact creation.
bool contactCandidateValid(const char* name, const uint8_t* pub_key,
                           uint8_t type);
bool contactCandidateDuplicates(const char* name, const uint8_t* pub_key,
                                const char* existing_name,
                                const uint8_t* existing_pub_key);

// Stable contact IDs are the lowercase hexadecimal encoding of the complete
// MeshCore public key. Display names are intentionally not part of identity.
bool contactIdFromPubKey(const uint8_t* pub_key, char* out, size_t out_size);
bool contactIdToPubKey(const char* contact_id, uint8_t* out_pub_key);
bool contactDisplayLabel(const char* name, const char* contact_id,
                         bool disambiguate, char* out, size_t out_size);

class ContactReferenceResolver {
public:
    explicit ContactReferenceResolver(const char* reference);
    void consider(int index, const char* name, const uint8_t* pub_key);
    int result() const;

private:
    const char* _reference;
    uint8_t _pub_key[SIGURDOS_CONTACT_PUBKEY_LEN];
    int _match;
    bool _stable_id;
    bool _ambiguous;
};

/// Returns a strictly increasing local contact revision, or false if the
/// persisted high-water mark can no longer be advanced.
inline bool nextContactRevision(uint32_t clock_value, uint32_t high_water,
                                uint32_t& revision_out) {
    if (high_water == UINT32_MAX) return false;
    revision_out = clock_value > high_water ? clock_value : high_water + 1U;
    return true;
}

#if !defined(ESP32_PLATFORM)
void contactStoreSetNativePath(const char* path);
#endif

} // namespace mesh
} // namespace sigurdos
