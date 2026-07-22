// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "persistence_store.h"
#include "region_name.h"

#include "hal/atomic_file.h"

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
// Region persistence (SPIFFS-backed, legacy-layout compatible)
// ════════════════════════════════════════════════════

namespace {

static constexpr uint8_t REGION_MAGIC[3] = {'S', 'R', 1};
static constexpr size_t REGION_LEGACY_HEADER_SIZE = 10;
static constexpr size_t REGION_LEGACY_RECORD_SIZE = 164;
static constexpr size_t REGION_CHECKSUM_SIZE = 4;
static constexpr size_t REGION_MAX_RECORDS = 32;

uint16_t regionReadU16(const uint8_t* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t regionReadU32(const uint8_t* data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

void regionWriteU32(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8) & 0xFFU);
    data[2] = (uint8_t)((value >> 16) & 0xFFU);
    data[3] = (uint8_t)((value >> 24) & 0xFFU);
}

uint32_t regionCrcUpdate(uint32_t crc, const uint8_t* data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^
                (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return crc;
}

bool regionIdPresent(uint16_t id, const uint16_t* ids, size_t count)
{
    if (id == 0) return true;
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] == id) return true;
    }
    return false;
}

bool validateRegionStructure(sigurdos::storage::AtomicFileReader& reader,
                             size_t raw_size, bool current)
{
    if (raw_size < REGION_LEGACY_HEADER_SIZE ||
        (raw_size - REGION_LEGACY_HEADER_SIZE) %
            REGION_LEGACY_RECORD_SIZE != 0) return false;
    const size_t count = (raw_size - REGION_LEGACY_HEADER_SIZE) /
        REGION_LEGACY_RECORD_SIZE;
    if (count > REGION_MAX_RECORDS || !reader.seek(0)) return false;

    uint8_t header[REGION_LEGACY_HEADER_SIZE];
    if (reader.read(header, sizeof(header)) != sizeof(header)) return false;
    if (current) {
        if (std::memcmp(header, REGION_MAGIC, sizeof(REGION_MAGIC)) != 0) {
            return false;
        }
    } else if (header[0] != 0 || header[1] != 0 || header[2] != 0) {
        return false;
    }

    const uint16_t default_id = regionReadU16(&header[3]);
    const uint16_t home_id = regionReadU16(&header[5]);
    const uint16_t next_id = regionReadU16(&header[8]);
    uint16_t ids[REGION_MAX_RECORDS]{};
    uint16_t parents[REGION_MAX_RECORDS]{};
    uint16_t max_id = 0;

    for (size_t i = 0; i < count; ++i) {
        uint8_t record[REGION_LEGACY_RECORD_SIZE];
        if (reader.read(record, sizeof(record)) != sizeof(record)) return false;
        const uint16_t id = regionReadU16(record);
        const uint16_t parent = regionReadU16(&record[2]);
        const uint8_t* name = &record[4];
        const void* terminator = std::memchr(name, '\0', 31);
        if (id == 0 || !terminator || name[0] == '\0') return false;
        if (!regionNameValid(reinterpret_cast<const char*>(name))) return false;
        for (size_t j = 0; j < i; ++j) {
            if (ids[j] == id) return false;
        }
        ids[i] = id;
        parents[i] = parent;
        if (id > max_id) max_id = id;
    }

    if (next_id == 0 || next_id <= max_id ||
        !regionIdPresent(default_id, ids, count) ||
        !regionIdPresent(home_id, ids, count)) return false;
    for (size_t i = 0; i < count; ++i) {
        if (parents[i] == ids[i] ||
            !regionIdPresent(parents[i], ids, count)) return false;

        // Every parent chain must reach the wildcard root.  Parent existence
        // and direct self-parent checks alone do not reject A -> B -> A.
        uint16_t cursor = parents[i];
        size_t depth = 0;
        while (cursor != 0) {
            if (depth++ >= count) return false;
            size_t parent_index = 0;
            while (parent_index < count && ids[parent_index] != cursor) {
                ++parent_index;
            }
            if (parent_index == count) return false;
            cursor = parents[parent_index];
        }
    }
    return true;
}

bool validateCurrentRegionFile(sigurdos::storage::AtomicFileReader& reader,
                               void*)
{
    const size_t size = reader.size();
    if (size < REGION_LEGACY_HEADER_SIZE + REGION_CHECKSUM_SIZE) return false;
    const size_t raw_size = size - REGION_CHECKSUM_SIZE;
    if (!reader.seek(0)) return false;

    uint32_t crc = 0xFFFFFFFFU;
    size_t remaining = raw_size;
    uint8_t buffer[128];
    while (remaining > 0) {
        const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (reader.read(buffer, chunk) != chunk) return false;
        crc = regionCrcUpdate(crc, buffer, chunk);
        remaining -= chunk;
    }
    uint8_t stored_crc[REGION_CHECKSUM_SIZE];
    if (reader.read(stored_crc, sizeof(stored_crc)) != sizeof(stored_crc) ||
        (crc ^ 0xFFFFFFFFU) != regionReadU32(stored_crc)) return false;
    return validateRegionStructure(reader, raw_size, true);
}

bool validateLegacyRegionFile(sigurdos::storage::AtomicFileReader& reader,
                              void*)
{
    return validateRegionStructure(reader, reader.size(), false);
}

bool validateRegionPath(const char* path,
                        sigurdos::storage::AtomicFileValidateFn validate)
{
    if (!path || !validate) return false;
#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(path, "r");
    if (!file) return false;
    sigurdos::storage::AtomicFileReader reader(&file);
    const bool valid = validate(reader, nullptr);
    file.close();
#else
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    sigurdos::storage::AtomicFileReader reader(file);
    const bool valid = validate(reader, nullptr);
    std::fclose(file);
#endif
    return valid;
}

struct RegionEnvelopeWriteCtx {
    const char* source_path;
    size_t source_size;
};

bool writeRegionEnvelope(sigurdos::storage::AtomicFileWriter& writer,
                         void* raw_ctx)
{
    auto* ctx = static_cast<RegionEnvelopeWriteCtx*>(raw_ctx);
    if (!ctx || !ctx->source_path || ctx->source_size < sizeof(REGION_MAGIC)) {
        return false;
    }
#if defined(ESP32_PLATFORM)
    File source = SPIFFS.open(ctx->source_path, "r");
    if (!source) return false;
#else
    FILE* source = std::fopen(ctx->source_path, "rb");
    if (!source) return false;
#endif

    bool ok = true;
    uint32_t crc = 0xFFFFFFFFU;
    size_t offset = 0;
    uint8_t buffer[128];
    while (ok && offset < ctx->source_size) {
        const size_t chunk = ctx->source_size - offset < sizeof(buffer)
            ? ctx->source_size - offset : sizeof(buffer);
#if defined(ESP32_PLATFORM)
        const size_t read = source.read(buffer, chunk);
#else
        const size_t read = std::fread(buffer, 1, chunk, source);
#endif
        if (read != chunk) {
            ok = false;
            break;
        }
        if (offset == 0) std::memcpy(buffer, REGION_MAGIC, sizeof(REGION_MAGIC));
        crc = regionCrcUpdate(crc, buffer, chunk);
        ok = writer.write(buffer, chunk) == chunk;
        offset += chunk;
    }
#if defined(ESP32_PLATFORM)
    source.close();
#else
    std::fclose(source);
#endif
    if (!ok) return false;
    uint8_t encoded_crc[REGION_CHECKSUM_SIZE];
    regionWriteU32(encoded_crc, crc ^ 0xFFFFFFFFU);
    return writer.write(encoded_crc, sizeof(encoded_crc)) == sizeof(encoded_crc);
}

} // namespace

namespace detail {

bool regionStoreSaveLegacyFile(const char* path, const char* legacy_path)
{
    if (!path || !legacy_path ||
        !validateRegionPath(legacy_path, validateLegacyRegionFile)) return false;
    // Finish any prior validated replacement before creating a new temp. This
    // prevents a retry from discarding the only good copy after a rename
    // interruption removed the old live file.
    const bool recovered = sigurdos::storage::atomicFileRecover(
        path, validateCurrentRegionFile, nullptr);
    if (!recovered) {
        char temp_path[192];
        if (!sigurdos::storage::atomicFileTempPath(
                path, temp_path, sizeof(temp_path)) ||
            validateRegionPath(temp_path, validateCurrentRegionFile) ||
            (!validateRegionPath(path, validateCurrentRegionFile) &&
             !validateRegionPath(path, validateLegacyRegionFile))) {
            return false;
        }
    }
#if defined(ESP32_PLATFORM)
    File source = SPIFFS.open(legacy_path, "r");
    if (!source) return false;
    const size_t source_size = source.size();
    source.close();
#else
    FILE* source = std::fopen(legacy_path, "rb");
    if (!source) return false;
    if (std::fseek(source, 0, SEEK_END) != 0) {
        std::fclose(source);
        return false;
    }
    const long end = std::ftell(source);
    std::fclose(source);
    if (end < 0) return false;
    const size_t source_size = (size_t)end;
#endif
    RegionEnvelopeWriteCtx ctx{legacy_path, source_size};
    return sigurdos::storage::atomicFileReplace(
        path, writeRegionEnvelope, &ctx,
        validateCurrentRegionFile, nullptr);
}

RegionStoreFormat regionStorePrepareLoad(const char* path)
{
    if (!path) return RegionStoreFormat::Invalid;
    // Recovery may report false when an invalid temp accompanies a valid
    // legacy live file. It still removes the bad temp, so classify the live
    // file independently below.
    sigurdos::storage::atomicFileRecover(
        path, validateCurrentRegionFile, nullptr);
    if (validateRegionPath(path, validateCurrentRegionFile)) {
        return RegionStoreFormat::Current;
    }
    if (validateRegionPath(path, validateLegacyRegionFile)) {
        return RegionStoreFormat::Legacy;
    }
    return RegionStoreFormat::Invalid;
}

} // namespace detail

// ════════════════════════════════════════════════════
// Identity persistence (SPIFFS-backed)
// ════════════════════════════════════════════════════

namespace {

#if defined(ESP32_PLATFORM)
static constexpr const char* IDENTITY_PATH = "/mesh_id";
#else
static char g_identity_path[160] = "/tmp/sigurdos_mesh_id.bin";
#endif

static constexpr uint8_t IDENTITY_MAGIC[4] = {'S', 'G', 'I', 0xB1};
static constexpr uint8_t IDENTITY_VERSION = 1;
static constexpr size_t IDENTITY_HEADER_SIZE = 4 + 1 + 2 + 4;
static constexpr size_t IDENTITY_MAX_PAYLOAD = 128;

const char* identityPath()
{
#if defined(ESP32_PLATFORM)
    return IDENTITY_PATH;
#else
    return g_identity_path;
#endif
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return crc;
}

uint32_t payloadCrc(const uint8_t* data, size_t length)
{
    return crc32Update(0xFFFFFFFFU, data, length) ^ 0xFFFFFFFFU;
}

void encodeIdentityHeader(uint8_t* header, size_t length, uint32_t crc)
{
    std::memcpy(header, IDENTITY_MAGIC, sizeof(IDENTITY_MAGIC));
    header[4] = IDENTITY_VERSION;
    header[5] = (uint8_t)(length & 0xFFU);
    header[6] = (uint8_t)((length >> 8) & 0xFFU);
    header[7] = (uint8_t)(crc & 0xFFU);
    header[8] = (uint8_t)((crc >> 8) & 0xFFU);
    header[9] = (uint8_t)((crc >> 16) & 0xFFU);
    header[10] = (uint8_t)((crc >> 24) & 0xFFU);
}

bool decodeIdentityHeader(const uint8_t* header, size_t& length, uint32_t& crc)
{
    if (std::memcmp(header, IDENTITY_MAGIC, sizeof(IDENTITY_MAGIC)) != 0 ||
        header[4] != IDENTITY_VERSION) {
        return false;
    }
    length = (size_t)header[5] | ((size_t)header[6] << 8);
    crc = (uint32_t)header[7] |
          ((uint32_t)header[8] << 8) |
          ((uint32_t)header[9] << 16) |
          ((uint32_t)header[10] << 24);
    return length > 0 && length <= IDENTITY_MAX_PAYLOAD;
}

struct IdentityWriteCtx {
    const uint8_t* data;
    size_t length;
};

bool writeIdentity(sigurdos::storage::AtomicFileWriter& writer, void* raw)
{
    IdentityWriteCtx* ctx = static_cast<IdentityWriteCtx*>(raw);
    if (!ctx || !ctx->data || ctx->length == 0 ||
        ctx->length > IDENTITY_MAX_PAYLOAD) {
        return false;
    }
    uint8_t header[IDENTITY_HEADER_SIZE];
    encodeIdentityHeader(header, ctx->length, payloadCrc(ctx->data, ctx->length));
    return writer.write(header, sizeof(header)) == sizeof(header) &&
           writer.write(ctx->data, ctx->length) == ctx->length;
}

bool validateIdentity(sigurdos::storage::AtomicFileReader& reader, void*)
{
    if (reader.size() < IDENTITY_HEADER_SIZE || !reader.seek(0)) return false;
    uint8_t header[IDENTITY_HEADER_SIZE];
    if (reader.read(header, sizeof(header)) != sizeof(header)) return false;
    size_t length = 0;
    uint32_t expected_crc = 0;
    if (!decodeIdentityHeader(header, length, expected_crc) ||
        reader.size() != IDENTITY_HEADER_SIZE + length) {
        return false;
    }

    uint8_t chunk[32];
    uint32_t crc = 0xFFFFFFFFU;
    size_t remaining = length;
    while (remaining > 0) {
        const size_t wanted = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        if (reader.read(chunk, wanted) != wanted) return false;
        crc = crc32Update(crc, chunk, wanted);
        remaining -= wanted;
    }
    return (crc ^ 0xFFFFFFFFU) == expected_crc;
}

bool removeIdentityFile(const char* path)
{
#if defined(ESP32_PLATFORM)
    return !SPIFFS.exists(path) || SPIFFS.remove(path);
#else
    if (std::remove(path) == 0) return true;
    FILE* file = std::fopen(path, "rb");
    if (!file) return true;
    std::fclose(file);
    return false;
#endif
}

} // namespace

bool identityStoreSave(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > IDENTITY_MAX_PAYLOAD) return false;
    IdentityWriteCtx ctx{data, len};
    return sigurdos::storage::atomicFileReplace(
        identityPath(), writeIdentity, &ctx, validateIdentity, nullptr);
}

bool identityStoreLoad(uint8_t* buf, size_t buf_len, size_t* out_len)
{
    if (!buf || buf_len == 0) return false;

    // A failed recovery can mean an invalid temp was discarded while a legacy
    // raw live file remains. Continue and let the legacy reader handle it.
    sigurdos::storage::atomicFileRecover(identityPath(), validateIdentity, nullptr);

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(identityPath(), "r");
    if (!f) return false;
    size_t sz = f.size();
    uint8_t header[IDENTITY_HEADER_SIZE] = {};
    const bool has_header = sz >= sizeof(header) &&
        f.read(header, sizeof(header)) == sizeof(header) &&
        std::memcmp(header, IDENTITY_MAGIC, sizeof(IDENTITY_MAGIC)) == 0;
    if (!has_header) f.seek(0, SeekSet);
#else
    FILE* f = std::fopen(identityPath(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long end = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    size_t sz = end > 0 ? (size_t)end : 0;
    uint8_t header[IDENTITY_HEADER_SIZE] = {};
    const bool has_header = sz >= sizeof(header) &&
        std::fread(header, 1, sizeof(header), f) == sizeof(header) &&
        std::memcmp(header, IDENTITY_MAGIC, sizeof(IDENTITY_MAGIC)) == 0;
    if (!has_header) std::fseek(f, 0, SEEK_SET);
#endif

    size_t payload_len = sz;
    uint32_t expected_crc = 0;
    if (has_header) {
        if (!decodeIdentityHeader(header, payload_len, expected_crc) ||
            sz != IDENTITY_HEADER_SIZE + payload_len) {
#if defined(ESP32_PLATFORM)
            f.close();
#else
            std::fclose(f);
#endif
            return false;
        }
    }
    if (payload_len == 0 || payload_len > buf_len) {
#if defined(ESP32_PLATFORM)
        f.close();
#else
        std::fclose(f);
#endif
        return false;
    }

#if defined(ESP32_PLATFORM)
    size_t read = f.read(buf, payload_len);
    f.close();
#else
    size_t read = std::fread(buf, 1, payload_len, f);
    std::fclose(f);
#endif
    if (out_len) *out_len = read;
    return read == payload_len &&
        (!has_header || payloadCrc(buf, payload_len) == expected_crc);
}

bool identityStoreClear()
{
    char temp_path[192];
    if (!sigurdos::storage::atomicFileTempPath(
            identityPath(), temp_path, sizeof(temp_path))) {
        return false;
    }
    const bool temp_ok = removeIdentityFile(temp_path);
    return removeIdentityFile(identityPath()) && temp_ok;
}

#if !defined(ESP32_PLATFORM)
void identityStoreSetNativePath(const char* path)
{
    if (!path || !path[0]) return;
    std::strncpy(g_identity_path, path, sizeof(g_identity_path) - 1);
    g_identity_path[sizeof(g_identity_path) - 1] = '\0';
}
#endif

} // namespace mesh
} // namespace sigurdos
