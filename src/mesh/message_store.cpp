// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "message_store.h"

#include "hal/atomic_file.h"
#include "utils/utf8_util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ESP32_PLATFORM)
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include "hal/storage.h"
#endif

namespace sigurdos {
namespace mesh {

namespace {

#if defined(ESP32_PLATFORM)
static constexpr const char* STORE_PATH = "/companion_msgs";
#endif

static constexpr uint32_t MESSAGE_STORE_RECOVERY_MAX_RECORDS =
    MESSAGE_STORE_MAX_RECORDS + 1;
static uint32_t g_companion_backlog_drop_count = 0;
static MessageStoreRecoveryResult g_last_recovery_result =
    MessageStoreRecoveryResult::Clean;
// A record append reaches durable storage before its header is published. If
// publication fails without a reboot, reconcile the durable tail before the
// next append so the stale next_id can never be reused.
static bool g_append_recovery_required = false;

struct IdentityIndexEntry {
    uint32_t hash;
    uint32_t store_id;
};

static IdentityIndexEntry g_identity_index[MESSAGE_STORE_MAX_RECORDS];
static uint32_t g_identity_index_count = 0;
static bool g_identity_index_valid = false;

#if !defined(ESP32_PLATFORM)
static char g_native_path[160] = "/tmp/sigurdos_companion_msgs.bin";
static int g_native_header_write_limit = -1;
static int g_native_record_write_limit = -1;

static void copyZ(char* dest, size_t dest_sz, const char* src)
{
    if (!dest || dest_sz == 0) return;
    if (!src) src = "";
    std::strncpy(dest, src, dest_sz - 1);
    dest[dest_sz - 1] = '\0';
}
#endif

static uint8_t flagsFor(const StoredMessage& msg)
{
    uint8_t flags = 0;
    if (msg.is_self) flags |= 0x01;
    if (msg.is_channel) flags |= 0x02;
    if (msg.acked) flags |= 0x04;
    if (msg.companion_sent) flags |= 0x08;
    if (msg.confirmation_lost) flags |= 0x10;
    if (msg.route_flood) flags |= 0x20;
    if (msg.route_known) flags |= 0x40;
    if (msg.attempt_known) flags |= 0x80;
    return flags;
}

static void applyFlags(StoredMessage& msg, uint8_t flags)
{
    msg.is_self = (flags & 0x01) != 0;
    msg.is_channel = (flags & 0x02) != 0;
    msg.acked = (flags & 0x04) != 0;
    msg.companion_sent = (flags & 0x08) != 0;
    msg.confirmation_lost = (flags & 0x10) != 0;
    msg.route_flood = (flags & 0x20) != 0;
    msg.route_known = (flags & 0x40) != 0;
    msg.attempt_known = (flags & 0x80) != 0;
}

static bool readHeader(uint32_t* out_count, uint32_t* out_next_id = nullptr);
static bool writeHeaderIfNeeded();
static bool atomicReplaceStore(const StoredMessage* msgs, uint32_t count,
                               uint32_t next_id = 0);
static uint32_t nextAfter(uint32_t id);

static uint32_t identityHash(const StoredMessage& msg)
{
    uint32_t hash = 2166136261u;
    auto add = [&hash](const void* data, size_t len) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            hash ^= bytes[i];
            hash *= 16777619u;
        }
    };
    add(msg.conversation, strnlen(msg.conversation, sizeof(msg.conversation)));
    add(msg.text, strnlen(msg.text, sizeof(msg.text)));
    add(&msg.timestamp, sizeof(msg.timestamp));
    add(&msg.txt_type, sizeof(msg.txt_type));
    add(&msg.extra_len, sizeof(msg.extra_len));
    add(msg.extra, msg.extra_len <= sizeof(msg.extra) ? msg.extra_len : sizeof(msg.extra));
    const uint8_t direction = (msg.is_self ? 1u : 0u) |
        (msg.is_channel ? 2u : 0u);
    add(&direction, sizeof(direction));
    return hash == 0 ? 1 : hash;
}

static void identityIndexClear()
{
    g_identity_index_count = 0;
    g_identity_index_valid = true;
}

static void identityIndexAdd(const StoredMessage& msg)
{
    if (!g_identity_index_valid || msg.store_id == 0 ||
        g_identity_index_count >= MESSAGE_STORE_MAX_RECORDS) return;
    g_identity_index[g_identity_index_count++] = {identityHash(msg), msg.store_id};
}

static uint32_t nextUnusedIdAfter(uint32_t id)
{
    uint32_t candidate = nextAfter(id);
    for (uint32_t attempts = 0; attempts <= g_identity_index_count; ++attempts) {
        bool used = false;
        for (uint32_t i = 0; i < g_identity_index_count; ++i) {
            if (g_identity_index[i].store_id == candidate) {
                used = true;
                break;
            }
        }
        if (!used) return candidate;
        candidate = nextAfter(candidate);
    }
    return 0;
}

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

#endif

static const char* storePath()
{
#if defined(ESP32_PLATFORM)
    return STORE_PATH;
#else
    return g_native_path;
#endif
}

static bool pathExists(const char* path)
{
#if defined(ESP32_PLATFORM)
    return path && SPIFFS.exists(path);
#else
    if (!path) return false;
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
#endif
}

static bool removePath(const char* path)
{
#if defined(ESP32_PLATFORM)
    return path && (!SPIFFS.exists(path) || SPIFFS.remove(path));
#else
    return path && (std::remove(path) == 0 || !pathExists(path));
#endif
}

static bool renamePath(const char* from, const char* to)
{
#if defined(ESP32_PLATFORM)
    return from && to && SPIFFS.rename(from, to);
#else
    return from && to && std::rename(from, to) == 0;
#endif
}

static bool quarantinePath(char* out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    const int written = std::snprintf(out, out_size, "%s.corrupt", storePath());
    return written > 0 && (size_t)written < out_size;
}

static MessageStoreRecoveryResult quarantineStoreAndReset()
{
    char quarantine[192];
    if (!quarantinePath(quarantine, sizeof(quarantine)) ||
        !removePath(quarantine) || !renamePath(storePath(), quarantine)) {
        return MessageStoreRecoveryResult::Failed;
    }
    if (!atomicReplaceStore(nullptr, 0, 1)) {
        return MessageStoreRecoveryResult::Failed;
    }
    g_append_recovery_required = false;
    g_identity_index_valid = false;
    return MessageStoreRecoveryResult::Quarantined;
}

static StoredMessage* allocateMessages(uint32_t count)
{
    if (count == 0) return nullptr;
    const size_t bytes = sizeof(StoredMessage) * (size_t)count;
#if defined(ESP32_PLATFORM)
    void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        memory = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return static_cast<StoredMessage*>(memory);
#else
    return static_cast<StoredMessage*>(std::malloc(bytes));
#endif
}

static void freeMessages(StoredMessage* messages)
{
#if defined(ESP32_PLATFORM)
    heap_caps_free(messages);
#else
    std::free(messages);
#endif
}

static bool readRecordRaw(StoredMessage& msg, const uint8_t* rec, size_t len)
{
    if (!rec || len < detail::MESSAGE_STORE_RECORD_SIZE) return false;

    size_t pos = 0;
    std::memset(&msg, 0, sizeof(msg));
    std::memcpy(&msg.store_id, rec + pos, 4); pos += 4;

    std::memcpy(msg.conversation, rec + pos, SIGURDOS_MSG_CONVERSATION_LEN);
    msg.conversation[SIGURDOS_MSG_CONVERSATION_LEN - 1] = '\0';
    pos += SIGURDOS_MSG_CONVERSATION_LEN;

    std::memcpy(msg.sender, rec + pos, SIGURDOS_MSG_SENDER_LEN);
    msg.sender[SIGURDOS_MSG_SENDER_LEN - 1] = '\0';
    pos += SIGURDOS_MSG_SENDER_LEN;

    std::memcpy(msg.text, rec + pos, SIGURDOS_MSG_TEXT_LEN);
    sigurdos::utf8_copy_truncate_n(msg.text, sizeof(msg.text), msg.text,
                                   sizeof(msg.text));
    pos += SIGURDOS_MSG_TEXT_LEN;

    std::memcpy(&msg.timestamp, rec + pos, 4);
    pos += 4;

    std::memcpy(msg.sender_prefix, rec + pos, SIGURDOS_MSG_PREFIX_LEN);
    pos += SIGURDOS_MSG_PREFIX_LEN;

    std::memcpy(&msg.rssi, rec + pos, 2);
    pos += 2;

    msg.snr_quarters = (int8_t)rec[pos++];
    msg.path_len = rec[pos++];
    msg.txt_type = rec[pos++];
    msg.attempt = rec[pos++];
    msg.extra_len = rec[pos++];
    if (msg.extra_len > sizeof(msg.extra)) return false;
    std::memcpy(msg.extra, rec + pos, 8); pos += 8;
    applyFlags(msg, rec[pos++]);
    return true;
}

static bool readVersion5RecordRaw(StoredMessage& msg, const uint8_t* rec, size_t len)
{
    if (!rec || len < detail::MESSAGE_STORE_V5_RECORD_SIZE) return false;

    size_t pos = 0;
    std::memset(&msg, 0, sizeof(msg));
    std::memcpy(&msg.store_id, rec + pos, 4); pos += 4;

    std::memcpy(msg.conversation, rec + pos, SIGURDOS_MSG_CONVERSATION_LEN);
    msg.conversation[SIGURDOS_MSG_CONVERSATION_LEN - 1] = '\0';
    pos += SIGURDOS_MSG_CONVERSATION_LEN;

    std::memcpy(msg.sender, rec + pos, SIGURDOS_MSG_SENDER_LEN);
    msg.sender[SIGURDOS_MSG_SENDER_LEN - 1] = '\0';
    pos += SIGURDOS_MSG_SENDER_LEN;

    std::memcpy(msg.text, rec + pos, SIGURDOS_MSG_TEXT_LEN);
    msg.text[SIGURDOS_MSG_TEXT_LEN - 1] = '\0';
    pos += SIGURDOS_MSG_TEXT_LEN;

    std::memcpy(&msg.timestamp, rec + pos, 4); pos += 4;
    std::memcpy(msg.sender_prefix, rec + pos, SIGURDOS_MSG_PREFIX_LEN);
    pos += SIGURDOS_MSG_PREFIX_LEN;
    std::memcpy(&msg.rssi, rec + pos, 2); pos += 2;

    msg.snr_quarters = (int8_t)rec[pos++];
    msg.path_len = rec[pos++];
    msg.txt_type = rec[pos++];
    msg.attempt = 0;
    msg.extra_len = rec[pos++];
    if (msg.extra_len > sizeof(msg.extra)) return false;
    std::memcpy(msg.extra, rec + pos, 8); pos += 8;
    const uint8_t flags = rec[pos++];
    applyFlags(msg, flags);
    msg.attempt_known = false;

    // Before v6, received flood provenance was represented only by path_len.
    // Preserve any explicit v5 flags, and recover the information that is
    // unambiguous from an encoded flood path.
    if (!msg.route_known && msg.path_len != 0xFF) {
        msg.route_known = true;
        msg.route_flood = true;
    }
    return true;
}

static void writeRecordRaw(const StoredMessage& msg, uint8_t* rec, size_t len)
{
    if (!rec || len < detail::MESSAGE_STORE_RECORD_SIZE) return;

    StoredMessage norm = msg;
    detail::storedMessageNormalize(norm);

    size_t pos = 0;
    std::memset(rec, 0, len);
    std::memcpy(rec + pos, &norm.store_id, 4); pos += 4;
    std::memcpy(rec + pos, norm.conversation,
                strnlen(norm.conversation, SIGURDOS_MSG_CONVERSATION_LEN - 1));
    pos += SIGURDOS_MSG_CONVERSATION_LEN;

    std::memcpy(rec + pos, norm.sender,
                strnlen(norm.sender, SIGURDOS_MSG_SENDER_LEN - 1));
    pos += SIGURDOS_MSG_SENDER_LEN;

    std::memcpy(rec + pos, norm.text,
                strnlen(norm.text, SIGURDOS_MSG_TEXT_LEN - 1));
    pos += SIGURDOS_MSG_TEXT_LEN;

    std::memcpy(rec + pos, &norm.timestamp, 4);
    pos += 4;

    std::memcpy(rec + pos, norm.sender_prefix, SIGURDOS_MSG_PREFIX_LEN);
    pos += SIGURDOS_MSG_PREFIX_LEN;

    std::memcpy(rec + pos, &norm.rssi, 2);
    pos += 2;

    rec[pos++] = (uint8_t)norm.snr_quarters;
    rec[pos++] = norm.path_len;
    rec[pos++] = norm.txt_type;
    rec[pos++] = norm.attempt;
    rec[pos++] = norm.extra_len;
    std::memcpy(rec + pos, norm.extra, 8); pos += 8;
    rec[pos++] = flagsFor(norm);
}

static bool readHeader(uint32_t* out_count, uint32_t* out_next_id)
{
    if (!ensureFs() || !existsStore()) return false;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "r");
    if (!f) return false;
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t count = 0;
    uint32_t next_id = 0;
    bool ok = f.read((uint8_t*)&magic, 4) == 4 &&
              f.read(&version, 1) == 1 &&
              f.read((uint8_t*)&count, 4) == 4 &&
              f.read((uint8_t*)&next_id, 4) == 4;
    f.close();
#else
    FILE* f = std::fopen(g_native_path, "rb");
    if (!f) return false;
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t count = 0;
    uint32_t next_id = 0;
    bool ok = std::fread(&magic, 1, 4, f) == 4 &&
              std::fread(&version, 1, 1, f) == 1 &&
              std::fread(&count, 1, 4, f) == 4 &&
              std::fread(&next_id, 1, 4, f) == 4;
    std::fclose(f);
#endif

    if (!ok || magic != detail::MESSAGE_STORE_MAGIC ||
        version != detail::MESSAGE_STORE_VERSION || next_id == 0 ||
        count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) {
        return false;
    }
    if (out_count) *out_count = count;
    if (out_next_id) *out_next_id = next_id;
    return true;
}

static bool writeHeaderState(uint32_t count, uint32_t next_id)
{
    if (!ensureFs() || next_id == 0) return false;

    uint8_t header[detail::MESSAGE_STORE_HEADER_SIZE]{};
    const uint32_t magic = detail::MESSAGE_STORE_MAGIC;
    const uint8_t version = detail::MESSAGE_STORE_VERSION;
    std::memcpy(header, &magic, sizeof(magic));
    std::memcpy(header + 4, &version, sizeof(version));
    std::memcpy(header + 5, &count, sizeof(count));
    std::memcpy(header + 9, &next_id, sizeof(next_id));

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, existsStore() ? "r+" : "w");
    if (!f) return false;
    const bool ok = f.write(header, sizeof(header)) == sizeof(header);
    f.close();
    return ok;
#else
    FILE* f = std::fopen(g_native_path, existsStore() ? "r+b" : "w+b");
    if (!f) return false;
    size_t write_size = sizeof(header);
    if (g_native_header_write_limit >= 0 &&
        (size_t)g_native_header_write_limit < write_size) {
        write_size = (size_t)g_native_header_write_limit;
    }
    bool ok = std::fwrite(header, 1, write_size, f) == sizeof(header);
    if (std::fclose(f) != 0) ok = false;
    return ok;
#endif
}

static bool writeHeaderIfNeeded()
{
    uint32_t count = 0;
    if (readHeader(&count)) return true;
    // Never replace a present but unrecognised/corrupt store with an empty
    // current-version file. messageStoreBegin() must migrate or repair it
    // first; otherwise the original remains available for recovery.
    if (existsStore()) return false;
    return atomicReplaceStore(nullptr, 0, 1);
}

static int loadAllInternal(StoredMessage* out, int max)
{
    if (!out || max <= 0) return 0;

    uint32_t count = 0;
    if (!readHeader(&count)) return 0;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "r");
    if (!f) return 0;
    f.seek(detail::MESSAGE_STORE_HEADER_SIZE, SeekSet);
#else
    FILE* f = std::fopen(g_native_path, "rb");
    if (!f) return 0;
    std::fseek(f, (long)detail::MESSAGE_STORE_HEADER_SIZE, SEEK_SET);
#endif

    int n = 0;
    uint8_t rec[detail::MESSAGE_STORE_RECORD_SIZE];
    for (uint32_t i = 0; i < count; i++) {
#if defined(ESP32_PLATFORM)
        if (f.read(rec, sizeof(rec)) != sizeof(rec)) break;
#else
        if (std::fread(rec, 1, sizeof(rec), f) != sizeof(rec)) break;
#endif
        StoredMessage msg;
        if (!readRecordRaw(msg, rec, sizeof(rec))) continue;
        if (n < max) out[n++] = msg;
    }

#if defined(ESP32_PLATFORM)
    f.close();
#else
    std::fclose(f);
#endif
    return n;
}

static int loadRecentInternal(const char* conversation, StoredMessage* out,
                              int max, bool unsent_only)
{
    if (!out || max <= 0) return 0;
    uint32_t count = 0;
    if (!readHeader(&count)) return 0;

#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file) return 0;
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file) return 0;
#endif

    int n = 0;
    uint8_t record[detail::MESSAGE_STORE_RECORD_SIZE];
    for (int i = (int)count - 1; i >= 0 && n < max; --i) {
        const size_t offset = detail::MESSAGE_STORE_HEADER_SIZE +
            (size_t)i * detail::MESSAGE_STORE_RECORD_SIZE;
#if defined(ESP32_PLATFORM)
        const bool read_ok = file.seek(offset, SeekSet) &&
            file.read(record, sizeof(record)) == sizeof(record);
#else
        const bool read_ok = std::fseek(file, (long)offset, SEEK_SET) == 0 &&
            std::fread(record, 1, sizeof(record), file) == sizeof(record);
#endif
        StoredMessage msg{};
        if (!read_ok || !readRecordRaw(msg, record, sizeof(record))) continue;
        if (unsent_only && msg.companion_sent) continue;
        if (conversation && conversation[0] &&
            std::strncmp(msg.conversation, conversation,
                         SIGURDOS_MSG_CONVERSATION_LEN) != 0) {
            continue;
        }
        out[n++] = msg;
    }
#if defined(ESP32_PLATFORM)
    file.close();
#else
    std::fclose(file);
#endif

    for (int i = 0; i < n / 2; ++i) {
        StoredMessage swap = out[i];
        out[i] = out[n - 1 - i];
        out[n - 1 - i] = swap;
    }
    return n;
}

// Load the next companion delivery page in durable store order. Recent chat
// history is intentionally read newest-first above, but offline sync must
// start with the oldest unsent incoming record so repeated pages preserve FIFO
// order. Self-sent records are not part of the companion incoming-message
// stream and must be filtered here; otherwise an old self record can occupy
// every refill forever because it is never marked companion_sent.
static int loadUnsentInternal(StoredMessage* out, int max)
{
    if (!out || max <= 0) return 0;
    uint32_t count = 0;
    if (!readHeader(&count)) return 0;

#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file) return 0;
    file.seek(detail::MESSAGE_STORE_HEADER_SIZE, SeekSet);
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file) return 0;
    std::fseek(file, (long)detail::MESSAGE_STORE_HEADER_SIZE, SEEK_SET);
#endif

    int n = 0;
    uint8_t record[detail::MESSAGE_STORE_RECORD_SIZE];
    for (uint32_t i = 0; i < count && n < max; ++i) {
#if defined(ESP32_PLATFORM)
        const bool read_ok = file.read(record, sizeof(record)) == sizeof(record);
#else
        const bool read_ok =
            std::fread(record, 1, sizeof(record), file) == sizeof(record);
#endif
        if (!read_ok) break;
        StoredMessage msg{};
        if (!readRecordRaw(msg, record, sizeof(record)) ||
            msg.companion_sent || msg.is_self) {
            continue;
        }
        out[n++] = msg;
    }

#if defined(ESP32_PLATFORM)
    file.close();
#else
    std::fclose(file);
#endif
    return n;
}

static bool rebuildIdentityIndex()
{
    uint32_t count = 0;
    if (!readHeader(&count) || count > MESSAGE_STORE_MAX_RECORDS) {
        g_identity_index_valid = false;
        return false;
    }
    if (count == 0) {
        identityIndexClear();
        return true;
    }
    StoredMessage* messages = allocateMessages(count);
    if (!messages) {
        g_identity_index_valid = false;
        return false;
    }
    const int loaded = loadAllInternal(messages, (int)count);
    identityIndexClear();
    if (loaded != (int)count) {
        g_identity_index_valid = false;
        freeMessages(messages);
        return false;
    }
    for (int i = 0; i < loaded; ++i) identityIndexAdd(messages[i]);
    freeMessages(messages);
    return true;
}

// Normal appends consult only the bounded RAM index. A matching hash is
// collision-checked against the durable record. Sender identity is omitted
// from the hash so legacy zero-prefix records retain name-based comparison.
static bool messageExists(const StoredMessage& msg, uint32_t* store_id_out)
{
    if (!g_identity_index_valid && !rebuildIdentityIndex()) return false;
    const uint32_t hash = identityHash(msg);
    for (uint32_t i = 0; i < g_identity_index_count; ++i) {
        if (g_identity_index[i].hash != hash) continue;
        StoredMessage existing{};
        if (messageStoreGetById(g_identity_index[i].store_id, existing) &&
            detail::storedMessageSameIdentity(existing, msg)) {
            if (store_id_out) *store_id_out = existing.store_id;
            return true;
        }
    }
    return false;
}

struct StoreWriteCtx {
    const StoredMessage* messages;
    uint32_t count;
    uint32_t next_id;
};

static bool writeStore(sigurdos::storage::AtomicFileWriter& writer, void* raw)
{
    StoreWriteCtx* ctx = static_cast<StoreWriteCtx*>(raw);
    if (!ctx || ctx->next_id == 0 ||
        ctx->count > MESSAGE_STORE_RECOVERY_MAX_RECORDS ||
        (ctx->count > 0 && !ctx->messages)) return false;
    const uint32_t magic = detail::MESSAGE_STORE_MAGIC;
    const uint8_t version = detail::MESSAGE_STORE_VERSION;
    if (writer.write(&magic, sizeof(magic)) != sizeof(magic) ||
        writer.write(&version, 1) != 1 ||
        writer.write(&ctx->count, sizeof(ctx->count)) != sizeof(ctx->count) ||
        writer.write(&ctx->next_id, sizeof(ctx->next_id)) != sizeof(ctx->next_id)) {
        return false;
    }
    uint8_t record[detail::MESSAGE_STORE_RECORD_SIZE];
    for (uint32_t i = 0; i < ctx->count; ++i) {
        writeRecordRaw(ctx->messages[i], record, sizeof(record));
        if (writer.write(record, sizeof(record)) != sizeof(record)) return false;
    }
    return true;
}

static bool validateStore(sigurdos::storage::AtomicFileReader& reader, void*)
{
    if (reader.size() < detail::MESSAGE_STORE_HEADER_SIZE || !reader.seek(0)) return false;
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t count = 0;
    uint32_t next_id = 0;
    if (reader.read(&magic, sizeof(magic)) != sizeof(magic) ||
        reader.read(&version, 1) != 1 ||
        reader.read(&count, sizeof(count)) != sizeof(count) ||
        reader.read(&next_id, sizeof(next_id)) != sizeof(next_id) ||
        magic != detail::MESSAGE_STORE_MAGIC ||
        version != detail::MESSAGE_STORE_VERSION ||
        count > MESSAGE_STORE_RECOVERY_MAX_RECORDS || next_id == 0) {
        return false;
    }
    if (reader.size() != detail::MESSAGE_STORE_HEADER_SIZE +
        (size_t)count * detail::MESSAGE_STORE_RECORD_SIZE) return false;
    uint32_t ids[MESSAGE_STORE_RECOVERY_MAX_RECORDS] = {};
    uint8_t record[detail::MESSAGE_STORE_RECORD_SIZE];
    for (uint32_t i = 0; i < count; ++i) {
        if (reader.read(record, sizeof(record)) != sizeof(record)) return false;
        std::memcpy(&ids[i], record, sizeof(ids[i]));
        if (ids[i] == 0 || ids[i] == next_id) return false;
        for (uint32_t j = 0; j < i; ++j) {
            if (ids[j] == ids[i]) return false;
        }
    }
    return true;
}

static bool validateStoreForRecovery(sigurdos::storage::AtomicFileReader& reader, void*)
{
    if (reader.size() < detail::MESSAGE_STORE_V4_HEADER_SIZE || !reader.seek(0)) {
        return false;
    }
    uint32_t magic = 0;
    uint8_t version = 0;
    if (reader.read(&magic, sizeof(magic)) != sizeof(magic) ||
        reader.read(&version, 1) != 1 || magic != detail::MESSAGE_STORE_MAGIC) {
        return false;
    }
    if (version == detail::MESSAGE_STORE_VERSION) {
        return validateStore(reader, nullptr);
    }
    if (version != 4 && version != 5) return false;
    uint32_t count = 0;
    if (reader.read(&count, sizeof(count)) != sizeof(count) ||
        count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) return false;
    const size_t header_size = version == 4
        ? detail::MESSAGE_STORE_V4_HEADER_SIZE
        : detail::MESSAGE_STORE_HEADER_SIZE;
    if (version == 5) {
        uint32_t next_id = 0;
        if (reader.read(&next_id, sizeof(next_id)) != sizeof(next_id) ||
            next_id == 0) return false;
    }
    if (reader.size() != header_size +
        (size_t)count * detail::MESSAGE_STORE_V5_RECORD_SIZE) return false;

    // Version 4 deliberately permits missing/reused IDs; migration assigns a
    // fresh sequence. Version 5 promises stable IDs, so do not recover a temp
    // that would make those identities ambiguous.
    if (version == 4) return true;
    uint32_t ids[MESSAGE_STORE_RECOVERY_MAX_RECORDS] = {};
    uint8_t record[detail::MESSAGE_STORE_V5_RECORD_SIZE];
    for (uint32_t i = 0; i < count; ++i) {
        if (reader.read(record, sizeof(record)) != sizeof(record)) return false;
        std::memcpy(&ids[i], record, sizeof(ids[i]));
        if (ids[i] == 0) return false;
        for (uint32_t j = 0; j < i; ++j) {
            if (ids[j] == ids[i]) return false;
        }
    }
    return true;
}

// Atomically replace the entire message store with the given records. A valid
// temp remains available for boot recovery if only the final rename fails.
static uint32_t nextAfter(uint32_t id)
{
    return id == UINT32_MAX ? 1U : id + 1U;
}

static uint32_t deriveNextId(const StoredMessage* messages, uint32_t count)
{
    uint32_t highest = 0;
    for (uint32_t i = 0; messages && i < count; ++i) {
        if (messages[i].store_id > highest) highest = messages[i].store_id;
    }
    uint32_t candidate = nextAfter(highest);
    // UINT32_MAX wraps to 1. Skip any still-live low IDs rather than
    // publishing a next_id that validateStore() must reject.
    for (uint32_t attempts = 0; attempts <= count; ++attempts) {
        bool used = false;
        for (uint32_t i = 0; messages && i < count; ++i) {
            if (messages[i].store_id == candidate) {
                used = true;
                break;
            }
        }
        if (!used) return candidate;
        candidate = nextAfter(candidate);
    }
    return 0;
}

static bool atomicReplaceStore(const StoredMessage* msgs, uint32_t count,
                               uint32_t next_id)
{
    if (!ensureFs()) return false;
    if (next_id == 0) {
        uint32_t ignored_count = 0;
        if (!readHeader(&ignored_count, &next_id)) next_id = deriveNextId(msgs, count);
    }
    StoreWriteCtx ctx{msgs, count, next_id};
    return sigurdos::storage::atomicFileReplace(
        storePath(), writeStore, &ctx, validateStore, nullptr);
}

static bool readCompleteRecords(StoredMessage* out, uint32_t count, size_t header_size)
{
    if (count > 0 && !out) return false;
#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file || !file.seek(header_size, SeekSet)) {
        if (file) file.close();
        return false;
    }
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file || std::fseek(file, (long)header_size, SEEK_SET) != 0) {
        if (file) std::fclose(file);
        return false;
    }
#endif
    uint8_t record[detail::MESSAGE_STORE_RECORD_SIZE];
    bool ok = true;
    for (uint32_t i = 0; ok && i < count; ++i) {
#if defined(ESP32_PLATFORM)
        ok = file.read(record, sizeof(record)) == sizeof(record);
#else
        ok = std::fread(record, 1, sizeof(record), file) == sizeof(record);
#endif
        if (ok) ok = readRecordRaw(out[i], record, sizeof(record));
    }
#if defined(ESP32_PLATFORM)
    file.close();
#else
    std::fclose(file);
#endif
    return ok;
}

// Read the longest unambiguous prefix of complete records. A damaged record
// or reused/zero ID terminates the prefix: records after that point cannot be
// identified safely, but earlier durable records can still be salvaged.
static bool readRecoverablePrefix(StoredMessage* out, uint32_t count,
                                  size_t header_size, uint32_t* out_count)
{
    if (!out_count || (count > 0 && !out)) return false;
    *out_count = 0;
#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file || !file.seek(header_size, SeekSet)) {
        if (file) file.close();
        return false;
    }
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file || std::fseek(file, (long)header_size, SEEK_SET) != 0) {
        if (file) std::fclose(file);
        return false;
    }
#endif
    uint8_t record[detail::MESSAGE_STORE_RECORD_SIZE];
    for (uint32_t i = 0; i < count; ++i) {
#if defined(ESP32_PLATFORM)
        const bool read_ok = file.read(record, sizeof(record)) == sizeof(record);
#else
        const bool read_ok =
            std::fread(record, 1, sizeof(record), file) == sizeof(record);
#endif
        if (!read_ok || !readRecordRaw(out[i], record, sizeof(record)) ||
            out[i].store_id == 0) {
            break;
        }
        bool duplicate = false;
        for (uint32_t j = 0; j < i; ++j) {
            if (out[j].store_id == out[i].store_id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) break;
        *out_count = i + 1;
    }
#if defined(ESP32_PLATFORM)
    file.close();
#else
    std::fclose(file);
#endif
    return true;
}

static bool readVersion5Records(StoredMessage* out, uint32_t count,
                                size_t header_size)
{
    if (count > 0 && !out) return false;
#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file || !file.seek(header_size, SeekSet)) {
        if (file) file.close();
        return false;
    }
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file || std::fseek(file, (long)header_size, SEEK_SET) != 0) {
        if (file) std::fclose(file);
        return false;
    }
#endif
    uint8_t record[detail::MESSAGE_STORE_V5_RECORD_SIZE];
    bool ok = true;
    for (uint32_t i = 0; ok && i < count; ++i) {
#if defined(ESP32_PLATFORM)
        ok = file.read(record, sizeof(record)) == sizeof(record);
#else
        ok = std::fread(record, 1, sizeof(record), file) == sizeof(record);
#endif
        if (ok) ok = readVersion5RecordRaw(out[i], record, sizeof(record));
    }
#if defined(ESP32_PLATFORM)
    file.close();
#else
    std::fclose(file);
#endif
    return ok;
}

static bool idsAreUniqueAndNonzero(const StoredMessage* messages, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (messages[i].store_id == 0) return false;
        for (uint32_t j = 0; j < i; ++j) {
            if (messages[i].store_id == messages[j].store_id) return false;
        }
    }
    return true;
}

static MessageStoreRecoveryResult migrateOlderStore()
{
    if (!existsStore()) return MessageStoreRecoveryResult::Clean;
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t declared_count = 0;
    uint32_t declared_next_id = 0;
    size_t file_size = 0;
#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file) return MessageStoreRecoveryResult::Failed;
    file_size = file.size();
    const bool header_ok = file.read((uint8_t*)&magic, 4) == 4 &&
        file.read(&version, 1) == 1 &&
        file.read((uint8_t*)&declared_count, 4) == 4 &&
        (version != 5 || file.read((uint8_t*)&declared_next_id, 4) == 4);
    file.close();
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file) return MessageStoreRecoveryResult::Failed;
    std::fseek(file, 0, SEEK_END);
    const long end = std::ftell(file);
    file_size = end > 0 ? (size_t)end : 0;
    std::fseek(file, 0, SEEK_SET);
    const bool header_ok = std::fread(&magic, 1, 4, file) == 4 &&
        std::fread(&version, 1, 1, file) == 1 &&
        std::fread(&declared_count, 1, 4, file) == 4 &&
        (version != 5 || std::fread(&declared_next_id, 1, 4, file) == 4);
    std::fclose(file);
#endif
    if (file_size < 5) return MessageStoreRecoveryResult::Clean;
    if (magic != detail::MESSAGE_STORE_MAGIC) {
        return MessageStoreRecoveryResult::Clean; // Current repair quarantines it.
    }
    if (version == detail::MESSAGE_STORE_VERSION) {
        return MessageStoreRecoveryResult::Clean;
    }
    if (version != 4 && version != 5) {
        return MessageStoreRecoveryResult::UnsupportedVersion;
    }
    if (!header_ok) return quarantineStoreAndReset();
    const size_t header_size = version == 4
        ? detail::MESSAGE_STORE_V4_HEADER_SIZE
        : detail::MESSAGE_STORE_HEADER_SIZE;
    if (file_size < header_size) return quarantineStoreAndReset();
    const size_t payload_size = file_size - header_size;
    const uint32_t complete_count =
        (uint32_t)(payload_size / detail::MESSAGE_STORE_V5_RECORD_SIZE);
    if (complete_count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) {
        return quarantineStoreAndReset();
    }

    StoredMessage* messages = allocateMessages(complete_count);
    if (complete_count > 0 && !messages) return MessageStoreRecoveryResult::Failed;
    bool ok = readVersion5Records(messages, complete_count, header_size);
    if (version == 4) {
        for (uint32_t i = 0; ok && i < complete_count; ++i) {
            messages[i].store_id = i + 1U;
        }
    } else if (ok) {
        ok = idsAreUniqueAndNonzero(messages, complete_count);
    }
    const uint32_t next_id = ok ? deriveNextId(messages, complete_count) : 0;
    if (!ok || next_id == 0) {
        freeMessages(messages);
        return quarantineStoreAndReset();
    }
    ok = atomicReplaceStore(messages, complete_count, next_id);
    freeMessages(messages);
    if (ok) return MessageStoreRecoveryResult::Migrated;
    return MessageStoreRecoveryResult::Failed;
}

static MessageStoreRecoveryResult repairInterruptedAppend()
{
    if (!existsStore()) return MessageStoreRecoveryResult::Clean;
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t declared_count = 0;
    uint32_t declared_next_id = 0;
    size_t file_size = 0;
#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file) return MessageStoreRecoveryResult::Failed;
    file_size = file.size();
    const bool header_ok = file.read((uint8_t*)&magic, 4) == 4 &&
        file.read(&version, 1) == 1 &&
        file.read((uint8_t*)&declared_count, 4) == 4 &&
        file.read((uint8_t*)&declared_next_id, 4) == 4;
    file.close();
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file) return MessageStoreRecoveryResult::Failed;
    std::fseek(file, 0, SEEK_END);
    const long end = std::ftell(file);
    file_size = end > 0 ? (size_t)end : 0;
    std::fseek(file, 0, SEEK_SET);
    const bool header_ok = std::fread(&magic, 1, 4, file) == 4 &&
        std::fread(&version, 1, 1, file) == 1 &&
        std::fread(&declared_count, 1, 4, file) == 4 &&
        std::fread(&declared_next_id, 1, 4, file) == 4;
    std::fclose(file);
#endif
    if (file_size < 5) return quarantineStoreAndReset();
    if (magic != detail::MESSAGE_STORE_MAGIC) {
        return quarantineStoreAndReset();
    }
    if (version != detail::MESSAGE_STORE_VERSION) {
        return MessageStoreRecoveryResult::UnsupportedVersion;
    }
    if (file_size < detail::MESSAGE_STORE_HEADER_SIZE) {
        return quarantineStoreAndReset();
    }

    const size_t payload_size = file_size - detail::MESSAGE_STORE_HEADER_SIZE;
    const uint32_t complete_count =
        (uint32_t)(payload_size / detail::MESSAGE_STORE_RECORD_SIZE);
    const bool has_torn_tail =
        payload_size % detail::MESSAGE_STORE_RECORD_SIZE != 0;
    if (complete_count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) {
        return quarantineStoreAndReset();
    }

    StoredMessage* messages = allocateMessages(complete_count);
    if (complete_count > 0 && !messages) {
        return MessageStoreRecoveryResult::Failed;
    }
    uint32_t recovered_count = 0;
    const bool read_ok = readRecoverablePrefix(
        messages, complete_count, detail::MESSAGE_STORE_HEADER_SIZE,
        &recovered_count);
    if (!read_ok) {
        freeMessages(messages);
        return MessageStoreRecoveryResult::Failed;
    }

    // If bytes that look like records exist but no record has an unambiguous
    // identity, preserve the original for inspection instead of erasing it.
    if (complete_count > 0 && recovered_count == 0) {
        freeMessages(messages);
        return quarantineStoreAndReset();
    }

    const uint32_t recovered_next_id = deriveNextId(messages, recovered_count);
    const bool valid_header = header_ok &&
        declared_count <= MESSAGE_STORE_RECOVERY_MAX_RECORDS &&
        declared_next_id != 0;
    const bool exact = valid_header && !has_torn_tail &&
        declared_count == complete_count && recovered_count == complete_count &&
        declared_next_id == recovered_next_id;
    if (exact) {
        freeMessages(messages);
        return MessageStoreRecoveryResult::Clean;
    }

    if (recovered_count == 0 && !valid_header) {
        freeMessages(messages);
        return quarantineStoreAndReset();
    }

    const bool replace_ok = recovered_next_id != 0 &&
        atomicReplaceStore(messages, recovered_count, recovered_next_id);
    const bool salvaged = !valid_header || recovered_count < complete_count;
    freeMessages(messages);
    if (!replace_ok) return MessageStoreRecoveryResult::Failed;
    return salvaged ? MessageStoreRecoveryResult::Salvaged
                    : MessageStoreRecoveryResult::Repaired;
}

static bool compactStoreToRecent(uint32_t max_records)
{
    if (max_records == 0) return messageStoreClear();
    uint32_t count = 0;
    uint32_t next_id = 0;
    if (!readHeader(&count, &next_id)) return false;
    if (count <= max_records) return true;

    StoredMessage* messages = allocateMessages(count);
    if (!messages || !readCompleteRecords(
            messages, count, detail::MESSAGE_STORE_HEADER_SIZE)) {
        freeMessages(messages);
        return false;
    }

    uint32_t unsent = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (!messages[i].is_self && !messages[i].companion_sent) ++unsent;
    }
    const uint32_t unsent_to_drop = unsent > max_records ? unsent - max_records : 0;
    const uint32_t unsent_to_keep = unsent - unsent_to_drop;
    const uint32_t other_slots = max_records - unsent_to_keep;
    const uint32_t others = count - unsent;
    const uint32_t others_to_drop = others > other_slots ? others - other_slots : 0;

    uint32_t skipped_unsent = 0;
    uint32_t skipped_others = 0;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const bool protect = !messages[i].is_self && !messages[i].companion_sent;
        if (protect && skipped_unsent < unsent_to_drop) {
            ++skipped_unsent;
            continue;
        }
        if (!protect && skipped_others < others_to_drop) {
            ++skipped_others;
            continue;
        }
        messages[kept++] = messages[i];
    }

    const bool ok = atomicReplaceStore(messages, kept, next_id);
    freeMessages(messages);
    if (ok) g_companion_backlog_drop_count += unsent_to_drop;
    return ok;
}

} // namespace

namespace detail {

bool storedMessageSameIdentity(const StoredMessage& a, const StoredMessage& b)
{
    bool a_has_prefix = false;
    bool b_has_prefix = false;
    for (size_t i = 0; i < SIGURDOS_MSG_PREFIX_LEN; ++i) {
        a_has_prefix = a_has_prefix || a.sender_prefix[i] != 0;
        b_has_prefix = b_has_prefix || b.sender_prefix[i] != 0;
    }
    const bool same_sender = a_has_prefix && b_has_prefix
        ? std::memcmp(a.sender_prefix, b.sender_prefix, SIGURDOS_MSG_PREFIX_LEN) == 0
        : std::strncmp(a.sender, b.sender, SIGURDOS_MSG_SENDER_LEN) == 0;
    return std::strncmp(a.conversation, b.conversation, SIGURDOS_MSG_CONVERSATION_LEN) == 0 &&
           same_sender &&
           std::strncmp(a.text, b.text, SIGURDOS_MSG_TEXT_LEN) == 0 &&
           a.timestamp == b.timestamp &&
           a.txt_type == b.txt_type &&
           a.extra_len == b.extra_len &&
           std::memcmp(a.extra, b.extra, a.extra_len) == 0 &&
           a.is_self == b.is_self &&
           a.is_channel == b.is_channel;
}

void storedMessageNormalize(StoredMessage& msg)
{
    msg.conversation[SIGURDOS_MSG_CONVERSATION_LEN - 1] = '\0';
    msg.sender[SIGURDOS_MSG_SENDER_LEN - 1] = '\0';
    sigurdos::utf8_copy_truncate_n(msg.text, sizeof(msg.text), msg.text,
                                   sizeof(msg.text));
    if (msg.extra_len > sizeof(msg.extra)) msg.extra_len = sizeof(msg.extra);
    if (msg.timestamp == 0) msg.timestamp = 1;
}

} // namespace detail

bool messageStoreBegin()
{
    g_last_recovery_result = MessageStoreRecoveryResult::Clean;
    if (!ensureFs()) {
        g_last_recovery_result = MessageStoreRecoveryResult::Failed;
        return false;
    }
    // A valid whole-store replacement wins. Invalid temps are removed without
    // touching the live file, which may still be repairable after an append.
    const bool replacement_recovered = sigurdos::storage::atomicFileRecover(
        storePath(), validateStoreForRecovery, nullptr);
    if (!replacement_recovered && !existsStore()) {
        char temp_path[192];
        const bool pending_valid_replacement =
            sigurdos::storage::atomicFileTempPath(
                storePath(), temp_path, sizeof(temp_path)) &&
            pathExists(temp_path);
        if (pending_valid_replacement) {
            g_last_recovery_result = MessageStoreRecoveryResult::Failed;
            return false;
        }
        // atomicFileRecover removed an invalid temp and there is no live file.
        // Continue so writeHeaderIfNeeded() can recreate an empty store (for
        // example after a power cut between quarantine and reset).
    }

    const MessageStoreRecoveryResult migration = migrateOlderStore();
    if (migration == MessageStoreRecoveryResult::UnsupportedVersion ||
        migration == MessageStoreRecoveryResult::Failed) {
        g_last_recovery_result = migration;
        return false;
    }
    if (migration != MessageStoreRecoveryResult::Clean) {
        g_last_recovery_result = migration;
    }

    const MessageStoreRecoveryResult repair = repairInterruptedAppend();
    if (repair == MessageStoreRecoveryResult::UnsupportedVersion ||
        repair == MessageStoreRecoveryResult::Failed) {
        g_last_recovery_result = repair;
        return false;
    }
    if (repair != MessageStoreRecoveryResult::Clean) {
        g_last_recovery_result = repair;
    }
    if (!writeHeaderIfNeeded()) {
        g_last_recovery_result = MessageStoreRecoveryResult::Failed;
        return false;
    }
    uint32_t count = 0;
    if (readHeader(&count) && count > MESSAGE_STORE_MAX_RECORDS) {
        if (!compactStoreToRecent(MESSAGE_STORE_MAX_RECORDS)) {
            g_last_recovery_result = MessageStoreRecoveryResult::Failed;
            return false;
        }
        g_last_recovery_result = MessageStoreRecoveryResult::Repaired;
    }
    if (!rebuildIdentityIndex()) {
        g_last_recovery_result = MessageStoreRecoveryResult::Failed;
        return false;
    }
    g_append_recovery_required = false;
    return true;
}

MessageStoreRecoveryResult messageStoreLastRecoveryResult()
{
    return g_last_recovery_result;
}

bool messageStoreClear()
{
    if (!atomicReplaceStore(nullptr, 0, 1)) return false;
    identityIndexClear();
    g_append_recovery_required = false;
    return true;
}

bool messageStoreAppend(const StoredMessage& msg, uint32_t* store_id_out)
{
    if (g_append_recovery_required) {
        const MessageStoreRecoveryResult recovery = repairInterruptedAppend();
        if (recovery == MessageStoreRecoveryResult::UnsupportedVersion ||
            recovery == MessageStoreRecoveryResult::Failed) {
            return false;
        }
        g_append_recovery_required = false;
        if (!rebuildIdentityIndex()) return false;
    }
    if (!writeHeaderIfNeeded()) return false;

    StoredMessage norm = msg;
    detail::storedMessageNormalize(norm);

    uint32_t existing_id = 0;
    if (messageExists(norm, &existing_id)) {
        if (store_id_out) *store_id_out = existing_id;
        return true;
    }

    uint32_t count = 0;
    uint32_t next_id = 0;
    if (!readHeader(&count, &next_id) || next_id == 0) return false;
    norm.store_id = next_id;
    const uint32_t following_id = nextUnusedIdAfter(next_id);
    if (following_id == 0) return false;

    uint8_t rec[detail::MESSAGE_STORE_RECORD_SIZE];
    writeRecordRaw(norm, rec, sizeof(rec));

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "a");
    if (!f) return false;
    g_append_recovery_required = true;
    bool ok = f.write(rec, sizeof(rec)) == sizeof(rec);
    f.close();
#else
    FILE* f = std::fopen(g_native_path, "ab");
    if (!f) return false;
    g_append_recovery_required = true;
    size_t write_size = sizeof(rec);
    if (g_native_record_write_limit >= 0 &&
        (size_t)g_native_record_write_limit < write_size) {
        write_size = (size_t)g_native_record_write_limit;
    }
    bool ok = std::fwrite(rec, 1, write_size, f) == sizeof(rec);
    if (std::fclose(f) != 0) ok = false;
#endif
    if (!ok) return false;

    uint32_t new_count = count + 1;
    if (!writeHeaderState(new_count, following_id)) return false;
    g_append_recovery_required = false;
    if (new_count > MESSAGE_STORE_MAX_RECORDS) {
        if (!compactStoreToRecent(MESSAGE_STORE_COMPACT_TO_RECORDS)) return false;
        if (!rebuildIdentityIndex()) return false;
    } else {
        identityIndexAdd(norm);
    }
    if (store_id_out) *store_id_out = norm.store_id;
    return true;
}

int messageStoreLoadRecent(const char* conversation, StoredMessage* out, int max)
{
    return loadRecentInternal(conversation, out, max, false);
}

int messageStoreLoadAll(StoredMessage* out, int max)
{
    return loadAllInternal(out, max);
}

bool messageStoreMarkAcked(const char* conversation, uint32_t timestamp)
{
    if (!conversation || !conversation[0] || timestamp == 0) return false;
    uint32_t count = 0;
    if (!readHeader(&count)) return false;
    if (count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) return false;
    StoredMessage* msgs = allocateMessages(count);
    if (!msgs) return false;
    int n = messageStoreLoadAll(msgs, (int)count);
    bool changed = false;
    for (int i = 0; i < n; i++) {
        if (msgs[i].timestamp == timestamp &&
            std::strncmp(msgs[i].conversation, conversation, SIGURDOS_MSG_CONVERSATION_LEN) == 0) {
            msgs[i].acked = true;
            msgs[i].confirmation_lost = false;
            changed = true;
        }
    }
    if (!changed) {
        freeMessages(msgs);
        return false;
    }

    bool ok = atomicReplaceStore(msgs, (uint32_t)n);
    freeMessages(msgs);
    return ok;
}

bool messageStoreMarkConfirmationLost(const char* conversation, uint32_t timestamp)
{
    if (!conversation || !conversation[0] || timestamp == 0) return false;
    uint32_t count = 0;
    if (!readHeader(&count) || count == 0 ||
        count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) return false;
    StoredMessage* msgs = allocateMessages(count);
    if (!msgs) return false;
    int n = messageStoreLoadAll(msgs, (int)count);
    bool changed = false;
    for (int i = 0; i < n; i++) {
        if (!msgs[i].acked && msgs[i].timestamp == timestamp &&
            std::strncmp(msgs[i].conversation, conversation,
                         SIGURDOS_MSG_CONVERSATION_LEN) == 0) {
            msgs[i].confirmation_lost = true;
            changed = true;
        }
    }
    bool ok = changed && atomicReplaceStore(msgs, (uint32_t)n);
    freeMessages(msgs);
    return ok;
}

int messageStoreMarkOrphanedPendingLost()
{
    uint32_t count = 0;
    if (!readHeader(&count) || count == 0 ||
        count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) return 0;
    StoredMessage* msgs = allocateMessages(count);
    if (!msgs) return 0;
    int n = messageStoreLoadAll(msgs, (int)count);
    int changed = 0;
    for (int i = 0; i < n; i++) {
        if (msgs[i].is_self && !msgs[i].is_channel && !msgs[i].acked &&
            !msgs[i].confirmation_lost) {
            msgs[i].confirmation_lost = true;
            changed++;
        }
    }
    bool ok = changed == 0 || atomicReplaceStore(msgs, (uint32_t)n);
    freeMessages(msgs);
    return ok ? changed : 0;
}

bool messageStoreMarkCompanionSent(uint32_t store_id)
{
    if (store_id == 0) return false;
    uint32_t count = 0;
    if (!readHeader(&count)) return false;
    if (count == 0) return false;
    if (count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) return false;

    StoredMessage* messages = allocateMessages(count);
    if (!messages) return false;
    const int loaded = messageStoreLoadAll(messages, (int)count);
    if (loaded != (int)count) {
        freeMessages(messages);
        return false;
    }
    bool found = false;
    for (int i = 0; i < loaded; ++i) {
        if (messages[i].store_id != store_id) continue;
        if (messages[i].companion_sent) {
            freeMessages(messages);
            return true;
        }
        messages[i].companion_sent = true;
        found = true;
        break;
    }
    const bool ok = found && atomicReplaceStore(messages, count);
    freeMessages(messages);
    return ok;
}

uint32_t messageStoreCompanionBacklogDropCount()
{
    return g_companion_backlog_drop_count;
}

bool messageStoreGetById(uint32_t store_id, StoredMessage& out)
{
    if (store_id == 0) return false;
    uint32_t count = 0;
    if (!readHeader(&count) || count == 0) return false;

#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file) return false;
    if (!file.seek(detail::MESSAGE_STORE_HEADER_SIZE, SeekSet)) {
        file.close();
        return false;
    }
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file) return false;
    if (std::fseek(file, (long)detail::MESSAGE_STORE_HEADER_SIZE, SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }
#endif

    bool found = false;
    uint8_t record[detail::MESSAGE_STORE_RECORD_SIZE];
    for (uint32_t i = 0; i < count && !found; ++i) {
#if defined(ESP32_PLATFORM)
        const bool read_ok = file.read(record, sizeof(record)) == sizeof(record);
#else
        const bool read_ok = std::fread(record, 1, sizeof(record), file) == sizeof(record);
#endif
        StoredMessage candidate{};
        if (!read_ok) break;
        if (readRecordRaw(candidate, record, sizeof(record)) &&
            candidate.store_id == store_id) {
            out = candidate;
            found = true;
        }
    }
#if defined(ESP32_PLATFORM)
    file.close();
#else
    std::fclose(file);
#endif
    return found;
}

bool messageStoreFindRecent(const char* conversation, const char* sender,
                            const char* text, uint32_t timestamp, bool is_self,
                            StoredMessage& out)
{
    if (!conversation || !conversation[0] || !sender || !text || timestamp == 0) {
        return false;
    }
    uint32_t count = 0;
    if (!readHeader(&count) || count == 0) return false;

#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file) return false;
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file) return false;
#endif

    // UI timestamps can be sampled one tick after the durable append. Search
    // newest-first within a narrow window so the fallback cannot select an
    // older identical message from the conversation.
    static constexpr uint32_t RECENT_WINDOW_SECONDS = 2;
    bool found = false;
    uint8_t record[detail::MESSAGE_STORE_RECORD_SIZE];
    for (int i = (int)count - 1; i >= 0 && !found; --i) {
        const size_t offset = detail::MESSAGE_STORE_HEADER_SIZE +
            (size_t)i * detail::MESSAGE_STORE_RECORD_SIZE;
#if defined(ESP32_PLATFORM)
        const bool read_ok = file.seek(offset, SeekSet) &&
            file.read(record, sizeof(record)) == sizeof(record);
#else
        const bool read_ok = std::fseek(file, (long)offset, SEEK_SET) == 0 &&
            std::fread(record, 1, sizeof(record), file) == sizeof(record);
#endif
        StoredMessage candidate{};
        if (!read_ok || !readRecordRaw(candidate, record, sizeof(record))) continue;
        const uint32_t delta = candidate.timestamp > timestamp
            ? candidate.timestamp - timestamp : timestamp - candidate.timestamp;
        if (delta <= RECENT_WINDOW_SECONDS && candidate.is_self == is_self &&
            std::strncmp(candidate.conversation, conversation,
                         SIGURDOS_MSG_CONVERSATION_LEN) == 0 &&
            std::strncmp(candidate.sender, sender, SIGURDOS_MSG_SENDER_LEN) == 0 &&
            std::strncmp(candidate.text, text, SIGURDOS_MSG_TEXT_LEN) == 0) {
            out = candidate;
            found = true;
        }
    }
#if defined(ESP32_PLATFORM)
    file.close();
#else
    std::fclose(file);
#endif
    return found;
}

int messageStoreLoadUnsent(StoredMessage* out, int max)
{
    return loadUnsentInternal(out, max);
}

int messageStoreCount()
{
    uint32_t count = 0;
    return readHeader(&count) ? (int)count : 0;
}

#if !defined(ESP32_PLATFORM)
void messageStoreSetNativePath(const char* path)
{
    if (!path || !path[0]) return;
    copyZ(g_native_path, sizeof(g_native_path), path);
    g_identity_index_valid = false;
    g_append_recovery_required = false;
}

void messageStoreSetNativeHeaderWriteLimit(int bytes)
{
    g_native_header_write_limit = bytes < -1 ? -1 : bytes;
}

void messageStoreSetNativeRecordWriteLimit(int bytes)
{
    g_native_record_write_limit = bytes < -1 ? -1 : bytes;
}
#endif

} // namespace mesh
} // namespace sigurdos
