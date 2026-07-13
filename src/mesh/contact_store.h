// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace mesh {

static constexpr size_t SIGURDOS_CONTACT_PUBKEY_LEN = 32;
static constexpr size_t SIGURDOS_CONTACT_NAME_LEN = 32;
static constexpr size_t SIGURDOS_CONTACT_PATH_LEN = 64;
static constexpr uint8_t SIGURDOS_CONTACT_PATH_UNKNOWN = 0xFF;

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

#if !defined(ESP32_PLATFORM)
void contactStoreSetNativePath(const char* path);
#endif

} // namespace mesh
} // namespace sigurdos
