// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "message_store.h"

#include <cstdlib>
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
static constexpr const char* STORE_PATH = "/companion_msgs";
#endif

static constexpr uint32_t MESSAGE_STORE_MAX_RECORDS = 64;

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
    return flags;
}

static void applyFlags(StoredMessage& msg, uint8_t flags)
{
    msg.is_self = (flags & 0x01) != 0;
    msg.is_channel = (flags & 0x02) != 0;
    msg.acked = (flags & 0x04) != 0;
    msg.companion_sent = (flags & 0x08) != 0;
}

static bool readHeader(uint32_t* out_count);
static bool writeHeaderIfNeeded();

#if defined(ESP32_PLATFORM)
static bool ensureFs()
{
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

static bool readRecordAt(uint32_t index, StoredMessage& msg)
{
    uint32_t count = 0;
    if (!readHeader(&count) || index >= count) return false;
    const long offset = 9L + (long)index * (long)detail::MESSAGE_STORE_RECORD_SIZE;
    uint8_t rec[detail::MESSAGE_STORE_RECORD_SIZE];

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "r");
    if (!f) return false;
    if (!f.seek(offset, SeekSet)) { f.close(); return false; }
    bool ok = f.read(rec, sizeof(rec)) == sizeof(rec);
    f.close();
#else
    FILE* f = std::fopen(g_native_path, "rb");
    if (!f) return false;
    if (std::fseek(f, offset, SEEK_SET) != 0) { std::fclose(f); return false; }
    bool ok = std::fread(rec, 1, sizeof(rec), f) == sizeof(rec);
    std::fclose(f);
#endif
    return ok && readRecordRaw(msg, rec, sizeof(rec));
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

static bool readHeader(uint32_t* out_count)
{
    if (!ensureFs() || !existsStore()) return false;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "r");
    if (!f) return false;
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t count = 0;
    bool ok = f.read((uint8_t*)&magic, 4) == 4 &&
              f.read(&version, 1) == 1 &&
              f.read((uint8_t*)&count, 4) == 4;
    f.close();
#else
    FILE* f = std::fopen(g_native_path, "rb");
    if (!f) return false;
    uint32_t magic = 0;
    uint8_t version = 0;
    uint32_t count = 0;
    bool ok = std::fread(&magic, 1, 4, f) == 4 &&
              std::fread(&version, 1, 1, f) == 1 &&
              std::fread(&count, 1, 4, f) == 4;
    std::fclose(f);
#endif

    if (!ok || magic != detail::MESSAGE_STORE_MAGIC ||
        version != detail::MESSAGE_STORE_VERSION) {
        return false;
    }
    if (out_count) *out_count = count;
    return true;
}

static bool writeHeaderCount(uint32_t count)
{
    if (!ensureFs()) return false;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, existsStore() ? "r+" : "w");
    if (!f) return false;
    uint32_t magic = detail::MESSAGE_STORE_MAGIC;
    uint8_t version = detail::MESSAGE_STORE_VERSION;
    bool ok = f.write((const uint8_t*)&magic, 4) == 4 &&
              f.write(&version, 1) == 1 &&
              f.write((const uint8_t*)&count, 4) == 4;
    f.close();
    return ok;
#else
    FILE* f = std::fopen(g_native_path, existsStore() ? "r+b" : "w+b");
    if (!f) return false;
    uint32_t magic = detail::MESSAGE_STORE_MAGIC;
    uint8_t version = detail::MESSAGE_STORE_VERSION;
    bool ok = std::fwrite(&magic, 1, 4, f) == 4 &&
              std::fwrite(&version, 1, 1, f) == 1 &&
              std::fwrite(&count, 1, 4, f) == 4;
    std::fclose(f);
    return ok;
#endif
}

static bool writeHeaderIfNeeded()
{
    uint32_t count = 0;
    if (readHeader(&count)) return true;
    removeStore();
    return writeHeaderCount(0);
}

static int loadAllInternal(StoredMessage* out, int max)
{
    if (!out || max <= 0) return 0;

    uint32_t count = 0;
    if (!readHeader(&count)) return 0;

#if defined(ESP32_PLATFORM)
    File f = SPIFFS.open(STORE_PATH, "r");
    if (!f) return 0;
    f.seek(9, SeekSet);
#else
    FILE* f = std::fopen(g_native_path, "rb");
    if (!f) return 0;
    std::fseek(f, 9, SEEK_SET);
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

static bool messageExists(const StoredMessage& msg)
{
    uint32_t count = 0;
    if (!readHeader(&count)) return false;
    StoredMessage existing;
    for (uint32_t i = 0; i < count; i++) {
        if (readRecordAt(i, existing) && detail::storedMessageSameIdentity(existing, msg)) {
            return true;
        }
    }
    return false;
}

// Atomically replace the entire message store with the given records.
// Writes to a temp file first, then uses rename() to swap it in —
// power loss after the initial write leaves the original file intact.
static bool atomicReplaceStore(const StoredMessage* msgs, uint32_t count)
{
    if (!ensureFs()) return false;
    if (!msgs || count == 0) return messageStoreClear();

#if defined(ESP32_PLATFORM)
    static constexpr const char* TMP_PATH = "/companion_msgs.tmp";
    SPIFFS.remove(TMP_PATH);
    File f = SPIFFS.open(TMP_PATH, "w");
    if (!f) return false;
    uint32_t magic = detail::MESSAGE_STORE_MAGIC;
    uint8_t version = detail::MESSAGE_STORE_VERSION;
    bool ok = f.write((const uint8_t*)&magic, 4) == 4 &&
              f.write(&version, 1) == 1 &&
              f.write((const uint8_t*)&count, 4) == 4;
    if (ok) {
        uint8_t rec[detail::MESSAGE_STORE_RECORD_SIZE];
        for (uint32_t i = 0; ok && i < count; i++) {
            writeRecordRaw(msgs[i], rec, sizeof(rec));
            ok = f.write(rec, sizeof(rec)) == sizeof(rec);
        }
    }
    f.close();
    if (!ok) {
        SPIFFS.remove(TMP_PATH);
        return false;
    }
    SPIFFS.remove(STORE_PATH);
    if (!SPIFFS.rename(TMP_PATH, STORE_PATH)) {
        SPIFFS.remove(TMP_PATH);
        return false;
    }
    return true;
#else
    char tmp_path[180];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_native_path);
    std::remove(tmp_path);
    FILE* f = std::fopen(tmp_path, "wb");
    if (!f) return false;
    uint32_t magic = detail::MESSAGE_STORE_MAGIC;
    uint8_t version = detail::MESSAGE_STORE_VERSION;
    bool ok = std::fwrite(&magic, 1, 4, f) == 4 &&
              std::fwrite(&version, 1, 1, f) == 1 &&
              std::fwrite(&count, 1, 4, f) == 4;
    if (ok) {
        uint8_t rec[detail::MESSAGE_STORE_RECORD_SIZE];
        for (uint32_t i = 0; ok && i < count; i++) {
            writeRecordRaw(msgs[i], rec, sizeof(rec));
            ok = std::fwrite(rec, 1, sizeof(rec), f) == sizeof(rec);
        }
    }
    std::fclose(f);
    if (!ok) {
        std::remove(tmp_path);
        return false;
    }
    std::remove(g_native_path);
    if (std::rename(tmp_path, g_native_path) != 0) {
        std::remove(tmp_path);
        return false;
    }
    return true;
#endif
}

static bool trimStoreToRecent(uint32_t max_records)
{
    if (max_records == 0) return messageStoreClear();
    StoredMessage* recent = (StoredMessage*)std::malloc(sizeof(StoredMessage) * max_records);
    if (!recent) return false;
    int n = messageStoreLoadRecent(nullptr, recent, (int)max_records);
    if (!atomicReplaceStore(recent, (uint32_t)n)) {
        std::free(recent);
        return false;
    }
    std::free(recent);
    return true;
}

} // namespace

namespace detail {

bool storedMessageSameIdentity(const StoredMessage& a, const StoredMessage& b)
{
    return std::strncmp(a.conversation, b.conversation, SIGURDOS_MSG_CONVERSATION_LEN) == 0 &&
           std::strncmp(a.sender, b.sender, SIGURDOS_MSG_SENDER_LEN) == 0 &&
           a.timestamp == b.timestamp &&
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
    if (!writeHeaderIfNeeded()) return false;
    uint32_t count = 0;
    if (readHeader(&count) && count > MESSAGE_STORE_MAX_RECORDS) {
        return trimStoreToRecent(MESSAGE_STORE_MAX_RECORDS);
    }
    return true;
}

bool messageStoreClear()
{
    removeStore();
    return writeHeaderCount(0);
}

bool messageStoreAppend(const StoredMessage& msg)
{
    if (!writeHeaderIfNeeded()) return false;

    StoredMessage norm = msg;
    detail::storedMessageNormalize(norm);

    if (messageExists(norm)) return true;

    // Assign a monotonic store_id from the current record count.
    {
        uint32_t count = 0;
        readHeader(&count);
        norm.store_id = count;
    }

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

    uint32_t count = 0;
    readHeader(&count);
    uint32_t new_count = count + 1;
    if (!writeHeaderCount(new_count)) return false;
    if (new_count > MESSAGE_STORE_MAX_RECORDS) {
        return trimStoreToRecent(MESSAGE_STORE_MAX_RECORDS);
    }
    return true;
}

int messageStoreLoadRecent(const char* conversation, StoredMessage* out, int max)
{
    if (!out || max <= 0) return 0;
    uint32_t count = 0;
    if (!readHeader(&count)) return 0;
    int n = 0;
    for (int i = (int)count - 1; i >= 0 && n < max; i--) {
        StoredMessage msg;
        if (!readRecordAt((uint32_t)i, msg)) continue;
        if (!conversation || !conversation[0] ||
            std::strncmp(msg.conversation, conversation, SIGURDOS_MSG_CONVERSATION_LEN) == 0) {
            out[n++] = msg;
        }
    }
    for (int i = 0; i < n / 2; i++) {
        StoredMessage swap = out[i];
        out[i] = out[n - 1 - i];
        out[n - 1 - i] = swap;
    }
    return n;
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
    if (count > 256) return false;
    StoredMessage* msgs = (StoredMessage*)std::malloc(sizeof(StoredMessage) * count);
    if (!msgs) return false;
    int n = messageStoreLoadAll(msgs, (int)count);
    bool changed = false;
    for (int i = 0; i < n; i++) {
        if (msgs[i].timestamp == timestamp &&
            std::strncmp(msgs[i].conversation, conversation, SIGURDOS_MSG_CONVERSATION_LEN) == 0) {
            msgs[i].acked = true;
            changed = true;
        }
    }
    if (!changed) {
        std::free(msgs);
        return false;
    }

    bool ok = atomicReplaceStore(msgs, (uint32_t)n);
    std::free(msgs);
    return ok;
}

bool messageStoreMarkCompanionSent(uint32_t store_id)
{
    uint32_t count = 0;
    if (!readHeader(&count)) return false;
    if (count == 0) return false;
    if (count > 256) return false;
    StoredMessage* msgs = (StoredMessage*)std::malloc(sizeof(StoredMessage) * count);
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
        std::free(msgs);
        return false;
    }
    bool ok = atomicReplaceStore(msgs, (uint32_t)n);
    std::free(msgs);
    return ok;
}

int messageStoreLoadUnsent(StoredMessage* out, int max)
{
    if (!out || max <= 0) return 0;
    uint32_t count = 0;
    if (!readHeader(&count)) return 0;
    int out_idx = 0;
    for (int i = (int)count - 1; i >= 0 && out_idx < max; i--) {
        StoredMessage msg;
        if (!readRecordAt((uint32_t)i, msg)) continue;
        if (!msg.companion_sent) {
            out[out_idx++] = msg;
        }
    }
    // Reverse to chronological order (we loaded newest-first)
    for (int i = 0; i < out_idx / 2; i++) {
        StoredMessage swap = out[i];
        out[i] = out[out_idx - 1 - i];
        out[out_idx - 1 - i] = swap;
    }
    return out_idx;
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
