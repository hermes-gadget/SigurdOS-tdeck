// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "persistence_store.h"

#if defined(ESP32_PLATFORM)
#include <Preferences.h>
#include <SPIFFS.h>
#include <FS.h>
#else
#include <cstdio>
#include <cstring>
#endif

namespace sigurdos {
namespace mesh {

// ════════════════════════════════════════════════════
// Channel persistence (NVS-backed)
// ════════════════════════════════════════════════════

bool channelStoreSave(int count, ChannelReadFn read, void* ctx)
{
    if (!read) return false;

#if defined(ESP32_PLATFORM)
    Preferences nvs;
    if (!nvs.begin("sigurdos", false)) return false;
    nvs.putUChar("ch_cnt", (uint8_t)count);
    for (int i = 0; i < count; i++) {
        char name[32] = {0};
        uint8_t secret[32] = {0};
        uint8_t hash[32] = {0};
        if (!read(i, name, sizeof(name), secret, sizeof(secret),
                  hash, sizeof(hash), ctx)) continue;
        char key[16];
        snprintf(key, sizeof(key), "ch_%d_name", i);
        nvs.putString(key, name);
        snprintf(key, sizeof(key), "ch_%d_sec", i);
        nvs.putBytes(key, secret, sizeof(secret));
        snprintf(key, sizeof(key), "ch_%d_hash", i);
        nvs.putBytes(key, hash, sizeof(hash));
    }
    nvs.end();
    return true;
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
    int n = nvs.getUChar("ch_cnt", 0);
    int loaded = 0;
    for (int i = 0; i < n; i++) {
        char key[16];
        char name[32] = {0};
        uint8_t secret[32] = {0};
        uint8_t hash[32] = {0};
        snprintf(key, sizeof(key), "ch_%d_name", i);
        if (nvs.getString(key, name, sizeof(name)) <= 0) continue;
        snprintf(key, sizeof(key), "ch_%d_sec", i);
        if (nvs.getBytes(key, secret, sizeof(secret)) <= 0) continue;
        snprintf(key, sizeof(key), "ch_%d_hash", i);
        if (nvs.getBytes(key, hash, sizeof(hash)) <= 0) continue;
        if (name[0] && load(secret, sizeof(secret), hash, name, ctx)) {
            loaded++;
        }
    }
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
