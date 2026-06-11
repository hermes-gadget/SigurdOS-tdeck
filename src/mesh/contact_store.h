// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace mesh {

static constexpr size_t SIGURDOS_CONTACT_PUBKEY_LEN = 32;
static constexpr size_t SIGURDOS_CONTACT_NAME_LEN = 32;

struct StoredContact {
    uint8_t pub_key[SIGURDOS_CONTACT_PUBKEY_LEN];
    char name[SIGURDOS_CONTACT_NAME_LEN];
    uint8_t type;
    uint8_t perm;
};

using ContactStoreReadFn = bool (*)(int index, StoredContact* out, void* ctx);
using ContactStoreWriteFn = bool (*)(const StoredContact& contact, void* ctx);

namespace detail {
static constexpr size_t CONTACT_STORE_RECORD_SIZE =
    SIGURDOS_CONTACT_PUBKEY_LEN +
    SIGURDOS_CONTACT_NAME_LEN +
    1 +  // type
    1;   // perm

void writeContactRecord(const StoredContact& contact, uint8_t* rec, size_t len);
bool readContactRecord(StoredContact& contact, const uint8_t* rec, size_t len);
} // namespace detail

bool contactStoreBegin();
bool contactStoreClear();
bool contactStoreSave(int count, ContactStoreReadFn read, void* ctx);
int  contactStoreLoad(ContactStoreWriteFn write, void* ctx);
bool contactStoreSaveAll(const StoredContact* contacts, int count);
int  contactStoreLoadAll(StoredContact* out, int max);

#if !defined(ESP32_PLATFORM)
void contactStoreSetNativePath(const char* path);
#endif

} // namespace mesh
} // namespace sigurdos
