// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace mesh {

// Callbacks for channel store to read/write individual channels without
// depending on the MeshCore type. The store owns the NVS format; callers
// provide data access.
using ChannelCountFn   = int  (*)(void* ctx);
using ChannelReadFn    = bool (*)(int index, char* name_out, size_t name_len,
                                  uint8_t* secret_out, size_t secret_len,
                                  uint8_t* hash_out, size_t hash_len, void* ctx);
using ChannelLoadFn    = bool (*)(const uint8_t* secret, size_t secret_len,
                                  const uint8_t* hash, const char* name, void* ctx);

namespace detail {

struct ChannelStoreKv {
    void* ctx;
    bool (*has)(void* ctx, const char* key);
    bool (*putU8)(void* ctx, const char* key, uint8_t value);
    bool (*putU32)(void* ctx, const char* key, uint32_t value);
    bool (*putString)(void* ctx, const char* key, const char* value);
    bool (*putBytes)(void* ctx, const char* key, const uint8_t* data, size_t len);
    uint8_t (*getU8)(void* ctx, const char* key, uint8_t fallback);
    uint32_t (*getU32)(void* ctx, const char* key, uint32_t fallback);
    size_t (*getString)(void* ctx, const char* key, char* out, size_t len);
    size_t (*getBytes)(void* ctx, const char* key, uint8_t* out, size_t len);
};

bool channelStoreSaveTransactional(ChannelStoreKv& kv, int count,
                                   ChannelReadFn read, void* ctx);
int channelStoreLoadTransactional(ChannelStoreKv& kv,
                                  ChannelLoadFn load, void* ctx);

enum class RegionStoreFormat {
    Invalid,
    Legacy,
    Current,
};

// Atomically wrap a validated MeshCore RegionMap file in SigurdOS's
// versioned, checksummed envelope. The envelope deliberately retains the
// upstream field layout so RegionMap can still consume it after validation.
bool regionStoreSaveLegacyFile(const char* path, const char* legacy_path);

// Recover an interrupted replacement and classify the live file. Legacy
// files are accepted only when every fixed-size record is complete.
RegionStoreFormat regionStorePrepareLoad(const char* path);

} // namespace detail

// Save channels list to NVS.
// Returns true on success (NVS write committed).
bool channelStoreSave(int count, ChannelReadFn read, void* ctx);

// Load channels list from NVS.
// Returns the number of channels loaded (0 if NVS unavailable or empty).
int  channelStoreLoad(ChannelLoadFn load, void* ctx);

// Identity persistence. New saves use a checksummed envelope; loads remain
// compatible with the legacy raw LocalIdentity bytes.
bool identityStoreSave(const uint8_t* data, size_t len);
bool identityStoreLoad(uint8_t* buf, size_t buf_len, size_t* out_len);
bool identityStoreClear();

#if !defined(ESP32_PLATFORM)
void identityStoreSetNativePath(const char* path);
#endif

} // namespace mesh
} // namespace sigurdos
