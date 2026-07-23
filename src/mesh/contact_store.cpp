// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "contact_store.h"
#include "path_codec.h"

#include "hal/atomic_file.h"

#include <cstring>
#include <cstdlib>

#if defined(ESP32_PLATFORM)
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include "hal/storage.h"
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
    if (!sigurdos::storage_available()) return false;
    static bool mounted = false;
    if (!mounted) {
        if (!SPIFFS.begin(false)) return false;
        mounted = true;
    }
    return true;
}

static bool existsStore()
{
    return SPIFFS.exists(STORE_PATH);
}

static bool removeStore()
{
    char temp_path[192];
    if (!sigurdos::storage::atomicFileTempPath(
            STORE_PATH, temp_path, sizeof(temp_path))) return false;
    const bool temp_ok = !SPIFFS.exists(temp_path) || SPIFFS.remove(temp_path);
    return (!SPIFFS.exists(STORE_PATH) || SPIFFS.remove(STORE_PATH)) && temp_ok;
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
    char temp_path[192];
    if (!sigurdos::storage::atomicFileTempPath(
            g_native_path, temp_path, sizeof(temp_path))) return false;
    bool temp_ok = std::remove(temp_path) == 0;
    if (!temp_ok) {
        FILE* temp = std::fopen(temp_path, "rb");
        temp_ok = temp == nullptr;
        if (temp) std::fclose(temp);
    }
    return (std::remove(g_native_path) == 0 || !existsStore()) && temp_ok;
}
#endif

static const char* storePath()
{
#if defined(ESP32_PLATFORM)
    return STORE_PATH;
#else
    return g_native_path;
#endif
}

struct ContactSaveCtx {
    int count;
    ContactStoreReadFn read;
    void* read_ctx;
};

static bool contactValid(const StoredContact& contact)
{
    static_assert(SIGURDOS_CONTACT_PATH_LEN == path::MAX_ENCODED_PATH_BYTES,
                  "contact and mesh path capacities must match");
    return path::storedLengthValid(contact.out_path_len);
}

static StoredContact* allocateContacts(int count)
{
    if (count <= 0 || count > MAX_CONTACTS) return nullptr;
    const size_t bytes = sizeof(StoredContact) * (size_t)count;
#if defined(ESP32_PLATFORM)
    void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) memory = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return static_cast<StoredContact*>(memory);
#else
    return static_cast<StoredContact*>(std::malloc(bytes));
#endif
}

static void freeContacts(StoredContact* contacts)
{
#if defined(ESP32_PLATFORM)
    heap_caps_free(contacts);
#else
    std::free(contacts);
#endif
}

static bool writeContacts(sigurdos::storage::AtomicFileWriter& writer, void* raw)
{
    ContactSaveCtx* ctx = static_cast<ContactSaveCtx*>(raw);
    if (!ctx || !ctx->read || ctx->count <= 0 || ctx->count > MAX_CONTACTS) {
        return false;
    }
    if (writer.write(detail::CONTACT_STORE_MAGIC,
                     sizeof(detail::CONTACT_STORE_MAGIC)) !=
            sizeof(detail::CONTACT_STORE_MAGIC) ||
        writer.write(&detail::CONTACT_STORE_VERSION, 1) != 1 ||
        writer.write(&ctx->count, sizeof(ctx->count)) != sizeof(ctx->count)) {
        return false;
    }

    uint8_t rec[detail::CONTACT_STORE_RECORD_SIZE];
    for (int i = 0; i < ctx->count; ++i) {
        StoredContact contact{};
        if (!ctx->read(i, &contact, ctx->read_ctx) || !contactValid(contact)) return false;
        detail::writeContactRecord(contact, rec, sizeof(rec));
        if (writer.write(rec, sizeof(rec)) != sizeof(rec)) return false;
    }
    return true;
}

static bool readValidatedContacts(sigurdos::storage::AtomicFileReader& reader,
                                  StoredContact** contacts_out, int* count_out)
{
    if (!contacts_out || !count_out || reader.size() < sizeof(int) ||
        !reader.seek(0)) return false;
    *contacts_out = nullptr;
    *count_out = 0;
    uint8_t head[sizeof(detail::CONTACT_STORE_MAGIC)] = {};
    if (reader.read(head, sizeof(head)) != sizeof(head)) return false;

    int count = 0;
    size_t header_size = sizeof(int);
    uint8_t version = 0;
    if (std::memcmp(head, detail::CONTACT_STORE_MAGIC, sizeof(head)) == 0) {
        if (reader.read(&version, 1) != 1 ||
            version < 1 || version > detail::CONTACT_STORE_VERSION ||
            reader.read(&count, sizeof(count)) != sizeof(count)) {
            return false;
        }
        header_size += 1 + sizeof(count);
    } else {
        std::memcpy(&count, head, sizeof(count));
    }
    if (count <= 0 || count > MAX_CONTACTS) return false;
    const size_t record_size = version >= 2
        ? detail::CONTACT_STORE_RECORD_SIZE
        : detail::CONTACT_STORE_V1_RECORD_SIZE;
    if (reader.size() != header_size + (size_t)count * record_size) return false;

    StoredContact* contacts = allocateContacts(count);
    if (!contacts) return false;
    uint8_t rec[detail::CONTACT_STORE_RECORD_SIZE];
    for (int i = 0; i < count; ++i) {
        if (reader.read(rec, record_size) != record_size) {
            freeContacts(contacts);
            return false;
        }
        const bool ok = version >= 2
            ? detail::readContactRecord(contacts[i], rec, record_size)
            : detail::readContactRecordV1(contacts[i], rec, record_size);
        if (!ok || !contactValid(contacts[i]) ||
            !contactCandidateValid(contacts[i].name, contacts[i].pub_key,
                                   contacts[i].type)) {
            freeContacts(contacts);
            return false;
        }
        for (int previous = 0; previous < i; ++previous) {
            if (contactCandidateDuplicates(
                    contacts[i].name, contacts[i].pub_key,
                    contacts[previous].name, contacts[previous].pub_key)) {
                freeContacts(contacts);
                return false;
            }
        }
    }
    *contacts_out = contacts;
    *count_out = count;
    return true;
}

static bool validateContacts(sigurdos::storage::AtomicFileReader& reader, void*)
{
    StoredContact* contacts = nullptr;
    int count = 0;
    const bool valid = readValidatedContacts(reader, &contacts, &count);
    freeContacts(contacts);
    return valid;
}

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
    rec[pos++] = contact.flags;
    rec[pos++] = contact.out_path_len;
    std::memcpy(rec + pos, contact.out_path, SIGURDOS_CONTACT_PATH_LEN);
    pos += SIGURDOS_CONTACT_PATH_LEN;
    std::memcpy(rec + pos, &contact.last_advert_timestamp, 4); pos += 4;
    std::memcpy(rec + pos, &contact.lastmod, 4); pos += 4;
    std::memcpy(rec + pos, &contact.gps_lat, 4); pos += 4;
    std::memcpy(rec + pos, &contact.gps_lon, 4); pos += 4;
    std::memcpy(rec + pos, &contact.sync_since, 4);
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
    contact.flags = rec[pos++];
    contact.out_path_len = rec[pos++];
    if (!contactValid(contact)) return false;
    std::memcpy(contact.out_path, rec + pos, SIGURDOS_CONTACT_PATH_LEN);
    pos += SIGURDOS_CONTACT_PATH_LEN;
    std::memcpy(&contact.last_advert_timestamp, rec + pos, 4); pos += 4;
    std::memcpy(&contact.lastmod, rec + pos, 4); pos += 4;
    std::memcpy(&contact.gps_lat, rec + pos, 4); pos += 4;
    std::memcpy(&contact.gps_lon, rec + pos, 4); pos += 4;
    std::memcpy(&contact.sync_since, rec + pos, 4);
    return true;
}

bool readContactRecordV1(StoredContact& contact, const uint8_t* rec, size_t len)
{
    if (!rec || len < CONTACT_STORE_V1_RECORD_SIZE) return false;
    size_t pos = 0;
    std::memset(&contact, 0, sizeof(contact));
    std::memcpy(contact.pub_key, rec + pos, SIGURDOS_CONTACT_PUBKEY_LEN);
    pos += SIGURDOS_CONTACT_PUBKEY_LEN;
    std::memcpy(contact.name, rec + pos, SIGURDOS_CONTACT_NAME_LEN);
    contact.name[SIGURDOS_CONTACT_NAME_LEN - 1] = '\0';
    pos += SIGURDOS_CONTACT_NAME_LEN;
    contact.type = rec[pos++];
    const uint8_t legacy_perm = rec[pos];
    contact.flags = (uint8_t)((legacy_perm & 0x03) << 1);
    contact.out_path_len = SIGURDOS_CONTACT_PATH_UNKNOWN;
    return true;
}

} // namespace detail

bool contactStoreBegin()
{
    if (!ensureFs()) return false;
    // Invalid temps are discarded without touching the live store. A false
    // return can also mean a salvageable legacy live file is incomplete, so
    // do not block loading it after cleanup.
    sigurdos::storage::atomicFileRecover(storePath(), validateContacts, nullptr);
    return true;
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

    ContactSaveCtx save_ctx{count, read, ctx};
    return sigurdos::storage::atomicFileReplace(
        storePath(), writeContacts, &save_ctx, validateContacts, nullptr);
}

int contactStoreLoad(ContactStoreWriteFn write, void* ctx)
{
    if (!write || !ensureFs()) return 0;
    sigurdos::storage::atomicFileRecover(storePath(), validateContacts, nullptr);
    if (!existsStore()) return 0;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "r");
    if (!f) return 0;
    sigurdos::storage::AtomicFileReader reader(&f);
#else
    FILE* f = std::fopen(g_native_path, "rb");
    if (!f) return 0;
    sigurdos::storage::AtomicFileReader reader(f);
#endif
    StoredContact* contacts = nullptr;
    int count = 0;
    const bool valid = readValidatedContacts(reader, &contacts, &count);
#if defined(ESP32_PLATFORM)
    f.close();
#else
    std::fclose(f);
#endif
    if (!valid) return 0;
    int loaded = 0;
    for (int i = 0; i < count && write(contacts[i], ctx); ++i) ++loaded;
    freeContacts(contacts);
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

bool contactCandidateValid(const char* name, const uint8_t* pub_key,
                           uint8_t type)
{
    if (!name || !pub_key) return false;
    const size_t name_len = strnlen(name, SIGURDOS_CONTACT_NAME_LEN);
    if (name_len == 0 || name_len == SIGURDOS_CONTACT_NAME_LEN) return false;
    for (size_t i = 0; i < name_len; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    if (type < MESHCORE_ADV_TYPE_CHAT || type > MESHCORE_ADV_TYPE_SENSOR) {
        return false;
    }
    bool nonzero = false;
    for (size_t i = 0; i < SIGURDOS_CONTACT_PUBKEY_LEN; ++i) {
        nonzero = nonzero || pub_key[i] != 0;
    }
    return nonzero;
}

bool contactCandidateDuplicates(const char* name, const uint8_t* pub_key,
                                const char* existing_name,
                                const uint8_t* existing_pub_key)
{
    if (!name || !pub_key || !existing_name || !existing_pub_key) return false;
    return std::strncmp(name, existing_name, SIGURDOS_CONTACT_NAME_LEN) == 0 ||
           std::memcmp(pub_key, existing_pub_key,
                       SIGURDOS_CONTACT_PUBKEY_LEN) == 0;
}

#if !defined(ESP32_PLATFORM)
void contactStoreSetNativePath(const char* path)
{
    copyZ(g_native_path, sizeof(g_native_path), path);
}
#endif

} // namespace mesh
} // namespace sigurdos
