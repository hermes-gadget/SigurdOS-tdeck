// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "persistence_store.h"

#include <cstdio>
#include <cstring>

#if defined(ESP32_PLATFORM)
#include <Preferences.h>
#include <SPIFFS.h>
#include <FS.h>
#endif

namespace sigurdos {
namespace mesh {

// ════════════════════════════════════════════════════
// Channel persistence (NVS-backed)
// ════════════════════════════════════════════════════

namespace {

static constexpr int CHANNEL_STORE_MAX = 16;
static constexpr uint32_t CHANNEL_COMMIT_MAGIC = 0x43484E31; // "CHN1"

uint32_t checksumUpdate(uint32_t checksum, const void* raw, size_t length)
{
    const uint8_t* data = static_cast<const uint8_t*>(raw);
    for (size_t i = 0; i < length; ++i) {
        checksum ^= data[i];
        checksum *= 16777619U;
    }
    return checksum;
}

void bankKey(char* out, size_t size, int bank, int index, char field)
{
    std::snprintf(out, size, "c%d_%02d_%c", bank, index, field);
}

void bankMetaKey(char* out, size_t size, int bank, const char* field)
{
    std::snprintf(out, size, "c%d_%s", bank, field);
}

bool kvComplete(const detail::ChannelStoreKv& kv)
{
    return kv.has && kv.putU8 && kv.putU32 && kv.putString && kv.putBytes &&
        kv.getU8 && kv.getU32 && kv.getString && kv.getBytes;
}

bool validateBank(detail::ChannelStoreKv& kv, int bank, int* count_out)
{
    char count_key[12];
    char crc_key[12];
    char commit_key[12];
    bankMetaKey(count_key, sizeof(count_key), bank, "cnt");
    bankMetaKey(crc_key, sizeof(crc_key), bank, "crc");
    bankMetaKey(commit_key, sizeof(commit_key), bank, "ok");
    if (!kv.has(kv.ctx, count_key) || !kv.has(kv.ctx, crc_key) ||
        !kv.has(kv.ctx, commit_key)) return false;

    const int count = kv.getU8(kv.ctx, count_key, 0xFF);
    if (count > CHANNEL_STORE_MAX) return false;
    const uint8_t count_byte = (uint8_t)count;
    uint32_t checksum = checksumUpdate(
        2166136261U, &count_byte, sizeof(count_byte));
    for (int i = 0; i < count; ++i) {
        char name[32] = {};
        uint8_t secret[32] = {};
        uint8_t hash[32] = {};
        char key[12];
        bankKey(key, sizeof(key), bank, i, 'n');
        if (kv.getString(kv.ctx, key, name, sizeof(name)) == 0 || !name[0]) return false;
        bankKey(key, sizeof(key), bank, i, 's');
        if (kv.getBytes(kv.ctx, key, secret, sizeof(secret)) != sizeof(secret)) return false;
        bankKey(key, sizeof(key), bank, i, 'h');
        if (kv.getBytes(kv.ctx, key, hash, sizeof(hash)) != sizeof(hash)) return false;
        checksum = checksumUpdate(checksum, name, sizeof(name));
        checksum = checksumUpdate(checksum, secret, sizeof(secret));
        checksum = checksumUpdate(checksum, hash, sizeof(hash));
    }
    const uint32_t stored_crc = kv.getU32(kv.ctx, crc_key, 0);
    const uint32_t commit = kv.getU32(kv.ctx, commit_key, 0);
    if (stored_crc != checksum || commit != (CHANNEL_COMMIT_MAGIC ^ checksum)) {
        return false;
    }
    if (count_out) *count_out = count;
    return true;
}

int applyBank(detail::ChannelStoreKv& kv, int bank, int count,
              ChannelLoadFn load, void* ctx)
{
    int loaded = 0;
    for (int i = 0; i < count; ++i) {
        char name[32] = {};
        uint8_t secret[32] = {};
        uint8_t hash[32] = {};
        char key[12];
        bankKey(key, sizeof(key), bank, i, 'n');
        kv.getString(kv.ctx, key, name, sizeof(name));
        bankKey(key, sizeof(key), bank, i, 's');
        kv.getBytes(kv.ctx, key, secret, sizeof(secret));
        bankKey(key, sizeof(key), bank, i, 'h');
        kv.getBytes(kv.ctx, key, hash, sizeof(hash));
        if (load(secret, sizeof(secret), hash, name, ctx)) ++loaded;
    }
    return loaded;
}

int loadLegacy(detail::ChannelStoreKv& kv, ChannelLoadFn load, void* ctx)
{
    if (!kv.has(kv.ctx, "ch_cnt")) return 0;
    const int count = kv.getU8(kv.ctx, "ch_cnt", 0);
    if (count > CHANNEL_STORE_MAX) return 0;
    int loaded = 0;
    for (int i = 0; i < count; ++i) {
        char key[16];
        char name[32] = {};
        uint8_t secret[32] = {};
        uint8_t hash[32] = {};
        std::snprintf(key, sizeof(key), "ch_%d_name", i);
        if (kv.getString(kv.ctx, key, name, sizeof(name)) == 0) continue;
        std::snprintf(key, sizeof(key), "ch_%d_sec", i);
        if (kv.getBytes(kv.ctx, key, secret, sizeof(secret)) != sizeof(secret)) continue;
        std::snprintf(key, sizeof(key), "ch_%d_hash", i);
        if (kv.getBytes(kv.ctx, key, hash, sizeof(hash)) != sizeof(hash)) continue;
        if (name[0] && load(secret, sizeof(secret), hash, name, ctx)) ++loaded;
    }
    return loaded;
}

#if defined(ESP32_PLATFORM)
bool prefHas(void* raw, const char* key)
{
    return static_cast<Preferences*>(raw)->isKey(key);
}
bool prefPutU8(void* raw, const char* key, uint8_t value)
{
    return static_cast<Preferences*>(raw)->putUChar(key, value) == sizeof(value);
}
bool prefPutU32(void* raw, const char* key, uint32_t value)
{
    return static_cast<Preferences*>(raw)->putUInt(key, value) == sizeof(value);
}
bool prefPutString(void* raw, const char* key, const char* value)
{
    return static_cast<Preferences*>(raw)->putString(key, value) > 0;
}
bool prefPutBytes(void* raw, const char* key, const uint8_t* data, size_t len)
{
    return static_cast<Preferences*>(raw)->putBytes(key, data, len) == len;
}
uint8_t prefGetU8(void* raw, const char* key, uint8_t fallback)
{
    return static_cast<Preferences*>(raw)->getUChar(key, fallback);
}
uint32_t prefGetU32(void* raw, const char* key, uint32_t fallback)
{
    return static_cast<Preferences*>(raw)->getUInt(key, fallback);
}
size_t prefGetString(void* raw, const char* key, char* out, size_t len)
{
    return static_cast<Preferences*>(raw)->getString(key, out, len);
}
size_t prefGetBytes(void* raw, const char* key, uint8_t* out, size_t len)
{
    return static_cast<Preferences*>(raw)->getBytes(key, out, len);
}

detail::ChannelStoreKv preferencesKv(Preferences& prefs)
{
    return {&prefs, prefHas, prefPutU8, prefPutU32, prefPutString,
            prefPutBytes, prefGetU8, prefGetU32, prefGetString, prefGetBytes};
}
#endif

} // namespace

namespace detail {

bool channelStoreSaveTransactional(ChannelStoreKv& kv, int count,
                                   ChannelReadFn read, void* ctx)
{
    if (!kvComplete(kv) || !read || count < 0 || count > CHANNEL_STORE_MAX) {
        return false;
    }
    const uint8_t active = kv.has(kv.ctx, "ch_active")
        ? kv.getU8(kv.ctx, "ch_active", 0xFF) : 0xFF;
    const int target = active <= 1 ? (active ^ 1) : 0;
    char key[12];
    bankMetaKey(key, sizeof(key), target, "ok");
    if (!kv.putU32(kv.ctx, key, 0)) return false;
    const uint8_t count_byte = (uint8_t)count;
    uint32_t checksum = checksumUpdate(2166136261U, &count_byte, sizeof(count_byte));

    for (int i = 0; i < count; ++i) {
        char name[32] = {};
        uint8_t secret[32] = {};
        uint8_t hash[32] = {};
        if (!read(i, name, sizeof(name), secret, sizeof(secret),
                  hash, sizeof(hash), ctx) || !name[0]) return false;
        bankKey(key, sizeof(key), target, i, 'n');
        if (!kv.putString(kv.ctx, key, name)) return false;
        bankKey(key, sizeof(key), target, i, 's');
        if (!kv.putBytes(kv.ctx, key, secret, sizeof(secret))) return false;
        bankKey(key, sizeof(key), target, i, 'h');
        if (!kv.putBytes(kv.ctx, key, hash, sizeof(hash))) return false;
        checksum = checksumUpdate(checksum, name, sizeof(name));
        checksum = checksumUpdate(checksum, secret, sizeof(secret));
        checksum = checksumUpdate(checksum, hash, sizeof(hash));
    }

    bankMetaKey(key, sizeof(key), target, "cnt");
    if (!kv.putU8(kv.ctx, key, (uint8_t)count)) return false;
    bankMetaKey(key, sizeof(key), target, "crc");
    if (!kv.putU32(kv.ctx, key, checksum)) return false;
    bankMetaKey(key, sizeof(key), target, "ok");
    if (!kv.putU32(kv.ctx, key, CHANNEL_COMMIT_MAGIC ^ checksum)) return false;

    // The active pointer is the commit point and must be the final write.
    return kv.putU8(kv.ctx, "ch_active", (uint8_t)target);
}

int channelStoreLoadTransactional(ChannelStoreKv& kv,
                                  ChannelLoadFn load, void* ctx)
{
    if (!kvComplete(kv) || !load) return 0;
    const uint8_t active = kv.has(kv.ctx, "ch_active")
        ? kv.getU8(kv.ctx, "ch_active", 0xFF) : 0xFF;
    if (active <= 1) {
        int count = 0;
        if (validateBank(kv, active, &count)) {
            return applyBank(kv, active, count, load, ctx);
        }
        const int fallback = active ^ 1;
        if (validateBank(kv, fallback, &count)) {
            return applyBank(kv, fallback, count, load, ctx);
        }
    } else {
        for (int bank = 0; bank < 2; ++bank) {
            int count = 0;
            if (validateBank(kv, bank, &count)) {
                return applyBank(kv, bank, count, load, ctx);
            }
        }
    }
    return loadLegacy(kv, load, ctx);
}

} // namespace detail

bool channelStoreSave(int count, ChannelReadFn read, void* ctx)
{
    if (!read) return false;

#if defined(ESP32_PLATFORM)
    Preferences nvs;
    if (!nvs.begin("sigurdos", false)) return false;
    detail::ChannelStoreKv kv = preferencesKv(nvs);
    const bool ok = detail::channelStoreSaveTransactional(kv, count, read, ctx);
    nvs.end();
    return ok;
#else
    (void)count;
    return false;
#endif
}

int channelStoreLoad(ChannelLoadFn load, void* ctx)
{
    if (!load) return 0;

#if defined(ESP32_PLATFORM)
    Preferences nvs;
    if (!nvs.begin("sigurdos", true)) return 0;
    detail::ChannelStoreKv kv = preferencesKv(nvs);
    const int loaded = detail::channelStoreLoadTransactional(kv, load, ctx);
    nvs.end();
    return loaded;
#else
    (void)load;
    return 0;
#endif
}

// ════════════════════════════════════════════════════
// Identity persistence (SPIFFS-backed)
// ════════════════════════════════════════════════════

bool identityStoreSave(const uint8_t* data, size_t len)
{
    if (!data || len == 0) return false;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open("/mesh_id", "w");
    if (!f) return false;
    size_t written = f.write(data, len);
    f.close();
    if (written != len) {
        SPIFFS.remove("/mesh_id");
        return false;
    }
    return true;
#else
    (void)data;
    (void)len;
    return false;
#endif
}

bool identityStoreLoad(uint8_t* buf, size_t buf_len, size_t* out_len)
{
    if (!buf || buf_len == 0) return false;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open("/mesh_id", "r");
    if (!f) return false;
    size_t sz = f.size();
    if (sz > buf_len) {
        f.close();
        return false;
    }
    size_t read = f.read(buf, sz);
    f.close();
    if (out_len) *out_len = read;
    return read == sz && sz > 0;
#else
    (void)buf;
    (void)buf_len;
    if (out_len) *out_len = 0;
    return false;
#endif
}

} // namespace mesh
} // namespace sigurdos
