// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "message_store.h"

#include "hal/atomic_file.h"

#include <cstdlib>
#include <cstring>

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
static constexpr const char* STORE_PATH = "/companion_msgs";
#endif

static constexpr uint32_t MESSAGE_STORE_RECOVERY_MAX_RECORDS =
    MESSAGE_STORE_MAX_RECORDS + 1;

#if !defined(ESP32_PLATFORM)
static char g_native_path[160] = "/tmp/sigurdos_companion_msgs.bin";

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
    return flags;
}

static void applyFlags(StoredMessage& msg, uint8_t flags)
{
    msg.is_self = (flags & 0x01) != 0;
    msg.is_channel = (flags & 0x02) != 0;
    msg.acked = (flags & 0x04) != 0;
    msg.companion_sent = (flags & 0x08) != 0;
    msg.confirmation_lost = (flags & 0x10) != 0;
}

static bool readHeader(uint32_t* out_count, uint32_t* out_next_id = nullptr);
static bool writeHeaderIfNeeded();
static bool atomicReplaceStore(const StoredMessage* msgs, uint32_t count,
                               uint32_t next_id = 0);

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
    msg.text[SIGURDOS_MSG_TEXT_LEN - 1] = '\0';
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
    msg.extra_len = rec[pos++];
    std::memcpy(msg.extra, rec + pos, 8); pos += 8;
    applyFlags(msg, rec[pos++]);
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
        version != detail::MESSAGE_STORE_VERSION || next_id == 0) {
        return false;
    }
    if (out_count) *out_count = count;
    if (out_next_id) *out_next_id = next_id;
    return true;
}

static bool writeHeaderState(uint32_t count, uint32_t next_id)
{
    if (!ensureFs() || next_id == 0) return false;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, existsStore() ? "r+" : "w");
    if (!f) return false;
    uint32_t magic = detail::MESSAGE_STORE_MAGIC;
    uint8_t version = detail::MESSAGE_STORE_VERSION;
    bool ok = f.write((const uint8_t*)&magic, 4) == 4 &&
              f.write(&version, 1) == 1 &&
              f.write((const uint8_t*)&count, 4) == 4 &&
              f.write((const uint8_t*)&next_id, 4) == 4;
    f.close();
    return ok;
#else
    FILE* f = std::fopen(g_native_path, existsStore() ? "r+b" : "w+b");
    if (!f) return false;
    uint32_t magic = detail::MESSAGE_STORE_MAGIC;
    uint8_t version = detail::MESSAGE_STORE_VERSION;
    bool ok = std::fwrite(&magic, 1, 4, f) == 4 &&
              std::fwrite(&version, 1, 1, f) == 1 &&
              std::fwrite(&count, 1, 4, f) == 4 &&
              std::fwrite(&next_id, 1, 4, f) == 4;
    std::fclose(f);
    return ok;
#endif
}

static bool writeHeaderIfNeeded()
{
    uint32_t count = 0;
    if (readHeader(&count)) return true;
    return atomicReplaceStore(nullptr, 0);
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

// Check whether a message with the same identity already exists in the store.
// Opens the store file ONCE and streams all records sequentially — O(n) reads,
// but O(1) file opens.  Previously called readRecordAt() per record, which
// opened the file twice for each (readHeader + seek/read), giving O(n²) SPIFFS
// opens repeatedly for every record on each incoming message. PERF-001.
static bool messageExists(const StoredMessage& msg, uint32_t* store_id_out)
{
    if (!ensureFs() || !existsStore()) return false;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "r");
    if (!f) return false;

    // Read header inline (avoids a separate readHeader() open)
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t count = 0;
    uint32_t next_id = 0;
    bool header_ok = f.read((uint8_t*)&magic, 4) == 4 &&
                     f.read(&version, 1) == 1 &&
                     f.read((uint8_t*)&count, 4) == 4 &&
                     f.read((uint8_t*)&next_id, 4) == 4 &&
                     magic == detail::MESSAGE_STORE_MAGIC &&
                     version == detail::MESSAGE_STORE_VERSION && next_id != 0;

    if (!header_ok) { f.close(); return false; }

    uint8_t rec[detail::MESSAGE_STORE_RECORD_SIZE];
    StoredMessage existing;
    bool found = false;
    for (uint32_t i = 0; i < count && !found; i++) {
        if (f.read(rec, sizeof(rec)) == sizeof(rec) &&
            readRecordRaw(existing, rec, sizeof(rec)) &&
            detail::storedMessageSameIdentity(existing, msg)) {
            if (store_id_out) *store_id_out = existing.store_id;
            found = true;
        }
    }
    f.close();
    return found;
#else
    FILE* f = std::fopen(g_native_path, "rb");
    if (!f) return false;

    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t count = 0;
    uint32_t next_id = 0;
    bool header_ok = std::fread(&magic, 1, 4, f) == 4 &&
                     std::fread(&version, 1, 1, f) == 1 &&
                     std::fread(&count, 1, 4, f) == 4 &&
                     std::fread(&next_id, 1, 4, f) == 4 &&
                     magic == detail::MESSAGE_STORE_MAGIC &&
                     version == detail::MESSAGE_STORE_VERSION && next_id != 0;

    if (!header_ok) { std::fclose(f); return false; }

    uint8_t rec[detail::MESSAGE_STORE_RECORD_SIZE];
    StoredMessage existing;
    bool found = false;
    for (uint32_t i = 0; i < count && !found; i++) {
        if (std::fread(rec, 1, sizeof(rec), f) == sizeof(rec) &&
            readRecordRaw(existing, rec, sizeof(rec)) &&
            detail::storedMessageSameIdentity(existing, msg)) {
            if (store_id_out) *store_id_out = existing.store_id;
            found = true;
        }
    }
    std::fclose(f);
    return found;
#endif
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

struct CompactWriteCtx {
    uint32_t first_index;
    uint32_t count;
    uint32_t next_id;
};

static bool writeCompactedStore(sigurdos::storage::AtomicFileWriter& writer, void* raw)
{
    CompactWriteCtx* ctx = static_cast<CompactWriteCtx*>(raw);
    if (!ctx || ctx->next_id == 0 || ctx->count > MESSAGE_STORE_MAX_RECORDS) {
        return false;
    }
    const uint32_t magic = detail::MESSAGE_STORE_MAGIC;
    const uint8_t version = detail::MESSAGE_STORE_VERSION;
    if (writer.write(&magic, sizeof(magic)) != sizeof(magic) ||
        writer.write(&version, 1) != 1 ||
        writer.write(&ctx->count, sizeof(ctx->count)) != sizeof(ctx->count) ||
        writer.write(&ctx->next_id, sizeof(ctx->next_id)) != sizeof(ctx->next_id)) {
        return false;
    }

    const size_t offset = detail::MESSAGE_STORE_HEADER_SIZE +
        (size_t)ctx->first_index * detail::MESSAGE_STORE_RECORD_SIZE;
#if defined(ESP32_PLATFORM)
    File source = SPIFFS.open(STORE_PATH, "r");
    if (!source || !source.seek(offset, SeekSet)) {
        if (source) source.close();
        return false;
    }
#else
    FILE* source = std::fopen(g_native_path, "rb");
    if (!source || std::fseek(source, (long)offset, SEEK_SET) != 0) {
        if (source) std::fclose(source);
        return false;
    }
#endif

    uint8_t record[detail::MESSAGE_STORE_RECORD_SIZE];
    bool ok = true;
    for (uint32_t i = 0; ok && i < ctx->count; ++i) {
#if defined(ESP32_PLATFORM)
        ok = source.read(record, sizeof(record)) == sizeof(record);
#else
        ok = std::fread(record, 1, sizeof(record), source) == sizeof(record);
#endif
        if (ok) ok = writer.write(record, sizeof(record)) == sizeof(record);
    }
#if defined(ESP32_PLATFORM)
    source.close();
#else
    std::fclose(source);
#endif
    return ok;
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
    if (version != 4) return false;
    uint32_t count = 0;
    if (reader.read(&count, sizeof(count)) != sizeof(count) ||
        count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) return false;
    return reader.size() == detail::MESSAGE_STORE_V4_HEADER_SIZE +
        (size_t)count * detail::MESSAGE_STORE_RECORD_SIZE;
}

// Atomically replace the entire message store with the given records. A valid
// temp remains available for boot recovery if only the final rename fails.
static uint32_t nextAfter(uint32_t id)
{
    return id == UINT32_MAX ? 1U : id + 1U;
}

static uint32_t deriveNextId(const StoredMessage* messages, uint32_t count)
{
    uint32_t next_id = 1;
    for (uint32_t i = 0; messages && i < count; ++i) {
        if (messages[i].store_id >= next_id) next_id = nextAfter(messages[i].store_id);
    }
    return next_id == 0 ? 1 : next_id;
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

static bool migrateVersion4Store()
{
    if (!existsStore()) return true;
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t declared_count = 0;
    size_t file_size = 0;
#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file) return false;
    file_size = file.size();
    const bool header_ok = file.read((uint8_t*)&magic, 4) == 4 &&
        file.read(&version, 1) == 1 &&
        file.read((uint8_t*)&declared_count, 4) == 4;
    file.close();
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long end = std::ftell(file);
    file_size = end > 0 ? (size_t)end : 0;
    std::fseek(file, 0, SEEK_SET);
    const bool header_ok = std::fread(&magic, 1, 4, file) == 4 &&
        std::fread(&version, 1, 1, file) == 1 &&
        std::fread(&declared_count, 1, 4, file) == 4;
    std::fclose(file);
#endif
    (void)declared_count;
    if (!header_ok || magic != detail::MESSAGE_STORE_MAGIC || version != 4) return true;
    if (file_size < detail::MESSAGE_STORE_V4_HEADER_SIZE) return false;
    const size_t payload_size = file_size - detail::MESSAGE_STORE_V4_HEADER_SIZE;
    const uint32_t complete_count =
        (uint32_t)(payload_size / detail::MESSAGE_STORE_RECORD_SIZE);
    if (complete_count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) return false;

    StoredMessage* messages = allocateMessages(complete_count);
    if (complete_count > 0 && !messages) return false;
    bool ok = readCompleteRecords(
        messages, complete_count, detail::MESSAGE_STORE_V4_HEADER_SIZE);
    for (uint32_t i = 0; ok && i < complete_count; ++i) {
        messages[i].store_id = i + 1U;
    }
    const uint32_t next_id = complete_count == UINT32_MAX ? 1U : complete_count + 1U;
    ok = ok && atomicReplaceStore(messages, complete_count, next_id);
    freeMessages(messages);
    return ok;
}

static bool repairInterruptedAppend()
{
    if (!existsStore()) return true;
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t declared_count = 0;
    uint32_t declared_next_id = 0;
    size_t file_size = 0;
#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(STORE_PATH, "r");
    if (!file) return false;
    file_size = file.size();
    const bool header_ok = file.read((uint8_t*)&magic, 4) == 4 &&
        file.read(&version, 1) == 1 &&
        file.read((uint8_t*)&declared_count, 4) == 4 &&
        file.read((uint8_t*)&declared_next_id, 4) == 4;
    file.close();
#else
    FILE* file = std::fopen(g_native_path, "rb");
    if (!file) return false;
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
    if (!header_ok || magic != detail::MESSAGE_STORE_MAGIC ||
        version != detail::MESSAGE_STORE_VERSION || declared_next_id == 0 ||
        file_size < detail::MESSAGE_STORE_HEADER_SIZE) {
        return true; // writeHeaderIfNeeded() will replace an invalid header.
    }

    const size_t payload_size = file_size - detail::MESSAGE_STORE_HEADER_SIZE;
    const uint32_t complete_count =
        (uint32_t)(payload_size / detail::MESSAGE_STORE_RECORD_SIZE);
    const bool has_torn_tail =
        payload_size % detail::MESSAGE_STORE_RECORD_SIZE != 0;
    if (complete_count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) return false;
    if (!has_torn_tail && declared_count == complete_count) return true;

    StoredMessage* messages = allocateMessages(complete_count);
    if (complete_count > 0 && !messages) return false;
    const bool read_ok = readCompleteRecords(
        messages, complete_count, detail::MESSAGE_STORE_HEADER_SIZE);
    const uint32_t recovered_next_id = read_ok
        ? deriveNextId(messages, complete_count) : 0;
    const bool replace_ok = read_ok &&
        atomicReplaceStore(messages, complete_count, recovered_next_id);
    freeMessages(messages);
    return replace_ok;
}

static bool compactStoreToRecent(uint32_t max_records)
{
    if (max_records == 0) return messageStoreClear();
    uint32_t count = 0;
    uint32_t next_id = 0;
    if (!readHeader(&count, &next_id)) return false;
    if (count <= max_records) return true;
    CompactWriteCtx ctx{count - max_records, max_records, next_id};
    return sigurdos::storage::atomicFileReplace(
        storePath(), writeCompactedStore, &ctx, validateStore, nullptr);
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
           a.timestamp == b.timestamp &&
           a.txt_type == b.txt_type &&
           a.is_self == b.is_self &&
           a.is_channel == b.is_channel;
}

void storedMessageNormalize(StoredMessage& msg)
{
    msg.conversation[SIGURDOS_MSG_CONVERSATION_LEN - 1] = '\0';
    msg.sender[SIGURDOS_MSG_SENDER_LEN - 1] = '\0';
    msg.text[SIGURDOS_MSG_TEXT_LEN - 1] = '\0';
    if (msg.timestamp == 0) msg.timestamp = 1;
}

} // namespace detail

bool messageStoreBegin()
{
    if (!ensureFs()) return false;
    // A valid whole-store replacement wins. Invalid temps are removed without
    // touching the live file, which may still be repairable after an append.
    sigurdos::storage::atomicFileRecover(
        storePath(), validateStoreForRecovery, nullptr);
    if (!migrateVersion4Store()) return false;
    if (!repairInterruptedAppend()) return false;
    if (!writeHeaderIfNeeded()) return false;
    uint32_t count = 0;
    if (readHeader(&count) && count > MESSAGE_STORE_MAX_RECORDS) {
        return compactStoreToRecent(MESSAGE_STORE_MAX_RECORDS);
    }
    return true;
}

bool messageStoreClear()
{
    return atomicReplaceStore(nullptr, 0, 1);
}

bool messageStoreAppend(const StoredMessage& msg, uint32_t* store_id_out)
{
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

    uint8_t rec[detail::MESSAGE_STORE_RECORD_SIZE];
    writeRecordRaw(norm, rec, sizeof(rec));

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "a");
    if (!f) return false;
    bool ok = f.write(rec, sizeof(rec)) == sizeof(rec);
    f.close();
#else
    FILE* f = std::fopen(g_native_path, "ab");
    if (!f) return false;
    bool ok = std::fwrite(rec, 1, sizeof(rec), f) == sizeof(rec);
    std::fclose(f);
#endif
    if (!ok) return false;

    uint32_t new_count = count + 1;
    if (!writeHeaderState(new_count, nextAfter(next_id))) return false;
    if (new_count > MESSAGE_STORE_MAX_RECORDS) {
        if (!compactStoreToRecent(MESSAGE_STORE_COMPACT_TO_RECORDS)) return false;
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
    if (!readHeader(&count) || count == 0 || count > 256) return false;
    StoredMessage* msgs = (StoredMessage*)std::malloc(sizeof(StoredMessage) * count);
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
    std::free(msgs);
    return ok;
}

int messageStoreMarkOrphanedPendingLost()
{
    uint32_t count = 0;
    if (!readHeader(&count) || count == 0 || count > 256) return 0;
    StoredMessage* msgs = (StoredMessage*)std::malloc(sizeof(StoredMessage) * count);
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
    std::free(msgs);
    return ok ? changed : 0;
}

bool messageStoreMarkCompanionSent(uint32_t store_id)
{
    uint32_t count = 0;
    if (!readHeader(&count)) return false;
    if (count == 0) return false;
    if (count > MESSAGE_STORE_RECOVERY_MAX_RECORDS) return false;
    StoredMessage* msgs = allocateMessages(count);
    if (!msgs) return false;
    int n = messageStoreLoadAll(msgs, (int)count);
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (msgs[i].store_id == store_id) {
            msgs[i].companion_sent = true;
            found = true;
            break;
        }
    }
    if (!found) {
        freeMessages(msgs);
        return false;
    }
    bool ok = atomicReplaceStore(msgs, (uint32_t)n);
    freeMessages(msgs);
    return ok;
}

int messageStoreLoadUnsent(StoredMessage* out, int max)
{
    return loadRecentInternal(nullptr, out, max, true);
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
}
#endif

} // namespace mesh
} // namespace sigurdos
