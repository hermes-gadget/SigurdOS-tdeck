// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "contact_store.h"

#include <cstring>

#if defined(ESP32_PLATFORM)
#include <SPIFFS.h>
#else
#include <cstdio>
#endif

namespace sigurdos {
namespace mesh {

namespace {

#if defined(ESP32_PLATFORM)
static constexpr const char* STORE_PATH = "/contacts";
#endif

#if !defined(ESP32_PLATFORM)
static char g_native_path[160] = "/tmp/sigurdos_contacts.bin";

static void copyZ(char* dest, size_t dest_sz, const char* src)
{
    if (!dest || dest_sz == 0) return;
    if (!src) src = "";
    std::strncpy(dest, src, dest_sz - 1);
    dest[dest_sz - 1] = '\0';
}
#endif

#if defined(ESP32_PLATFORM)
static bool ensureFs()
{
    return SPIFFS.begin(false);
}

static bool existsStore()
{
    return SPIFFS.exists(STORE_PATH);
}

static bool removeStore()
{
    return SPIFFS.remove(STORE_PATH);
}
#else
static bool ensureFs()
{
    return true;
}

static bool existsStore()
{
    FILE* f = std::fopen(g_native_path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

static bool removeStore()
{
    return std::remove(g_native_path) == 0 || !existsStore();
}
#endif

struct SaveAllCtx {
    const StoredContact* contacts;
};

static bool readFromArray(int index, StoredContact* out, void* ctx)
{
    SaveAllCtx* save_ctx = static_cast<SaveAllCtx*>(ctx);
    if (!save_ctx || !save_ctx->contacts || !out || index < 0) return false;
    *out = save_ctx->contacts[index];
    return true;
}

struct LoadAllCtx {
    StoredContact* contacts;
    int max;
    int count;
};

static bool writeToArray(const StoredContact& contact, void* ctx)
{
    LoadAllCtx* load_ctx = static_cast<LoadAllCtx*>(ctx);
    if (!load_ctx || !load_ctx->contacts || load_ctx->count >= load_ctx->max) return false;
    load_ctx->contacts[load_ctx->count++] = contact;
    return true;
}

} // namespace

namespace detail {

void writeContactRecord(const StoredContact& contact, uint8_t* rec, size_t len)
{
    if (!rec || len < CONTACT_STORE_RECORD_SIZE) return;

    size_t pos = 0;
    std::memcpy(rec + pos, contact.pub_key, SIGURDOS_CONTACT_PUBKEY_LEN);
    pos += SIGURDOS_CONTACT_PUBKEY_LEN;

    std::memcpy(rec + pos, contact.name, SIGURDOS_CONTACT_NAME_LEN);
    pos += SIGURDOS_CONTACT_NAME_LEN;

    rec[pos++] = contact.type;
    rec[pos++] = contact.perm;
}

bool readContactRecord(StoredContact& contact, const uint8_t* rec, size_t len)
{
    if (!rec || len < CONTACT_STORE_RECORD_SIZE) return false;

    size_t pos = 0;
    std::memset(&contact, 0, sizeof(contact));
    std::memcpy(contact.pub_key, rec + pos, SIGURDOS_CONTACT_PUBKEY_LEN);
    pos += SIGURDOS_CONTACT_PUBKEY_LEN;

    std::memcpy(contact.name, rec + pos, SIGURDOS_CONTACT_NAME_LEN);
    contact.name[SIGURDOS_CONTACT_NAME_LEN - 1] = '\0';
    pos += SIGURDOS_CONTACT_NAME_LEN;

    contact.type = rec[pos++];
    contact.perm = rec[pos++];
    return true;
}

} // namespace detail

bool contactStoreBegin()
{
    return ensureFs();
}

bool contactStoreClear()
{
    if (!ensureFs()) return false;
    return removeStore();
}

bool contactStoreSave(int count, ContactStoreReadFn read, void* ctx)
{
    if (!ensureFs()) return false;
    if (count <= 0) return removeStore();
    if (!read) return false;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "w");
    if (!f) return false;
    bool ok = f.write((const uint8_t*)&count, sizeof(count)) == sizeof(count);
#else
    FILE* f = std::fopen(g_native_path, "wb");
    if (!f) return false;
    bool ok = std::fwrite(&count, 1, sizeof(count), f) == sizeof(count);
#endif

    uint8_t rec[detail::CONTACT_STORE_RECORD_SIZE];
    for (int i = 0; ok && i < count; i++) {
        StoredContact contact{};
        if (!read(i, &contact, ctx)) continue;
        detail::writeContactRecord(contact, rec, sizeof(rec));
#if defined(ESP32_PLATFORM)
        ok = f.write(rec, sizeof(rec)) == sizeof(rec);
#else
        ok = std::fwrite(rec, 1, sizeof(rec), f) == sizeof(rec);
#endif
    }

#if defined(ESP32_PLATFORM)
    f.close();
#else
    std::fclose(f);
#endif
    return ok;
}

int contactStoreLoad(ContactStoreWriteFn write, void* ctx)
{
    if (!write || !ensureFs() || !existsStore()) return 0;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "r");
    if (!f) return 0;
    int count = 0;
    bool ok = f.read((uint8_t*)&count, sizeof(count)) == sizeof(count);
#else
    FILE* f = std::fopen(g_native_path, "rb");
    if (!f) return 0;
    int count = 0;
    bool ok = std::fread(&count, 1, sizeof(count), f) == sizeof(count);
#endif

    if (!ok || count <= 0) {
#if defined(ESP32_PLATFORM)
        f.close();
#else
        std::fclose(f);
#endif
        return 0;
    }

    int loaded = 0;
    uint8_t rec[detail::CONTACT_STORE_RECORD_SIZE];
    for (int i = 0; i < count; i++) {
#if defined(ESP32_PLATFORM)
        if (f.read(rec, sizeof(rec)) != sizeof(rec)) break;
#else
        if (std::fread(rec, 1, sizeof(rec), f) != sizeof(rec)) break;
#endif
        StoredContact contact{};
        if (!detail::readContactRecord(contact, rec, sizeof(rec))) break;
        if (!write(contact, ctx)) break;
        loaded++;
    }

#if defined(ESP32_PLATFORM)
    f.close();
#else
    std::fclose(f);
#endif
    return loaded;
}

bool contactStoreSaveAll(const StoredContact* contacts, int count)
{
    if (count <= 0) return contactStoreSave(count, nullptr, nullptr);
    SaveAllCtx ctx{contacts};
    return contactStoreSave(count, readFromArray, &ctx);
}

int contactStoreLoadAll(StoredContact* out, int max)
{
    if (!out || max <= 0) return 0;
    LoadAllCtx ctx{out, max, 0};
    contactStoreLoad(writeToArray, &ctx);
    return ctx.count;
}

#if !defined(ESP32_PLATFORM)
void contactStoreSetNativePath(const char* path)
{
    copyZ(g_native_path, sizeof(g_native_path), path);
}
#endif

} // namespace mesh
} // namespace sigurdos
