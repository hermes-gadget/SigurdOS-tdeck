// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "chat_history_store.h"

#include "hal/atomic_file.h"

#include <cstdio>
#include <cstring>

#if defined(ESP32_PLATFORM)
#include <SPIFFS.h>
#endif

namespace sigurdos::ui {
namespace {

static constexpr uint32_t HISTORY_MAGIC = 0x536d534c; // "SLmS"
static constexpr uint8_t HISTORY_VERSION = 1;
static constexpr size_t HISTORY_HEADER_BYTES = 4 + 1 + 1;
static constexpr size_t HISTORY_CHANNEL_BYTES = CHAT_HISTORY_CHANNEL_NAME_LEN + 1;
static constexpr size_t HISTORY_RECORD_BYTES =
    CHAT_HISTORY_SENDER_LEN + CHAT_HISTORY_TEXT_LEN + 4 + 1;
static constexpr size_t HISTORY_MAX_FILE_SIZE =
    HISTORY_HEADER_BYTES + CHAT_HISTORY_MAX_CHANNELS *
        (HISTORY_CHANNEL_BYTES + CHAT_HISTORY_MAX_MESSAGES * HISTORY_RECORD_BYTES);

#if defined(ESP32_PLATFORM)
static constexpr const char* HISTORY_PATH = "/msgs";
#else
static char g_history_path[160] = "/tmp/sigurdos_chat_history.bin";
#endif

const char* historyPath()
{
#if defined(ESP32_PLATFORM)
    return HISTORY_PATH;
#else
    return g_history_path;
#endif
}

bool ensureFs()
{
#if defined(ESP32_PLATFORM)
    static bool mounted = false;
    if (!mounted) {
        if (!SPIFFS.begin(false)) return false;
        mounted = true;
    }
#endif
    return true;
}

bool pathExists(const char* path)
{
#if defined(ESP32_PLATFORM)
    return SPIFFS.exists(path);
#else
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
#endif
}

bool removePath(const char* path)
{
#if defined(ESP32_PLATFORM)
    return !SPIFFS.exists(path) || SPIFFS.remove(path);
#else
    return std::remove(path) == 0 || !pathExists(path);
#endif
}

struct SaveCtx {
    int channel_count;
    ChatHistoryChannelReadFn read_channel;
    ChatHistoryMessageReadFn read_message;
    void* source_ctx;
};

bool writeHistory(sigurdos::storage::AtomicFileWriter& writer, void* raw)
{
    SaveCtx* ctx = static_cast<SaveCtx*>(raw);
    if (!ctx || ctx->channel_count < 0 ||
        ctx->channel_count > (int)CHAT_HISTORY_MAX_CHANNELS ||
        (ctx->channel_count > 0 && (!ctx->read_channel || !ctx->read_message))) {
        return false;
    }

    const uint8_t channel_count = (uint8_t)ctx->channel_count;
    if (writer.write(&HISTORY_MAGIC, sizeof(HISTORY_MAGIC)) != sizeof(HISTORY_MAGIC) ||
        writer.write(&HISTORY_VERSION, 1) != 1 ||
        writer.write(&channel_count, 1) != 1) {
        return false;
    }

    for (int channel = 0; channel < ctx->channel_count; ++channel) {
        char name[CHAT_HISTORY_CHANNEL_NAME_LEN] = {};
        uint8_t message_count = 0;
        if (!ctx->read_channel(channel, name, sizeof(name),
                               &message_count, ctx->source_ctx) ||
            message_count > CHAT_HISTORY_MAX_MESSAGES) {
            return false;
        }
        name[sizeof(name) - 1] = '\0';
        if (writer.write(name, sizeof(name)) != sizeof(name) ||
            writer.write(&message_count, 1) != 1) {
            return false;
        }

        for (int message = 0; message < message_count; ++message) {
            PersistedChatMessage record{};
            if (!ctx->read_message(channel, message, &record, ctx->source_ctx)) {
                return false;
            }
            record.sender[sizeof(record.sender) - 1] = '\0';
            record.text[sizeof(record.text) - 1] = '\0';
            const uint8_t self = record.is_self ? 1 : 0;
            if (writer.write(record.sender, sizeof(record.sender)) != sizeof(record.sender) ||
                writer.write(record.text, sizeof(record.text)) != sizeof(record.text) ||
                writer.write(&record.timestamp, sizeof(record.timestamp)) !=
                    sizeof(record.timestamp) ||
                writer.write(&self, 1) != 1) {
                return false;
            }
        }
    }
    return true;
}

bool readHeader(sigurdos::storage::AtomicFileReader& reader, uint8_t& channel_count)
{
    uint32_t magic = 0;
    uint8_t version = 0;
    return reader.size() >= HISTORY_HEADER_BYTES &&
        reader.size() <= HISTORY_MAX_FILE_SIZE && reader.seek(0) &&
        reader.read(&magic, sizeof(magic)) == sizeof(magic) &&
        magic == HISTORY_MAGIC && reader.read(&version, 1) == 1 &&
        version == HISTORY_VERSION && reader.read(&channel_count, 1) == 1 &&
        channel_count <= CHAT_HISTORY_MAX_CHANNELS;
}

bool validateHistory(sigurdos::storage::AtomicFileReader& reader, void*)
{
    uint8_t channel_count = 0;
    if (!readHeader(reader, channel_count)) return false;
    size_t expected_size = HISTORY_HEADER_BYTES;
    for (int channel = 0; channel < channel_count; ++channel) {
        char name[CHAT_HISTORY_CHANNEL_NAME_LEN];
        uint8_t message_count = 0;
        if (reader.read(name, sizeof(name)) != sizeof(name) ||
            reader.read(&message_count, 1) != 1 ||
            message_count > CHAT_HISTORY_MAX_MESSAGES) {
            return false;
        }
        expected_size += HISTORY_CHANNEL_BYTES +
            (size_t)message_count * HISTORY_RECORD_BYTES;
        if (expected_size > reader.size() || !reader.seek(expected_size)) return false;
    }
    return expected_size == reader.size();
}

bool loadFromReader(sigurdos::storage::AtomicFileReader& reader,
                    ChatHistoryMessageWriteFn write_message, void* ctx,
                    int& loaded)
{
    uint8_t channel_count = 0;
    if (!readHeader(reader, channel_count)) return false;
    for (int channel = 0; channel < channel_count; ++channel) {
        char name[CHAT_HISTORY_CHANNEL_NAME_LEN + 1] = {};
        uint8_t message_count = 0;
        if (reader.read(name, CHAT_HISTORY_CHANNEL_NAME_LEN) !=
                CHAT_HISTORY_CHANNEL_NAME_LEN ||
            reader.read(&message_count, 1) != 1 ||
            message_count > CHAT_HISTORY_MAX_MESSAGES) {
            return false;
        }
        name[CHAT_HISTORY_CHANNEL_NAME_LEN - 1] = '\0';

        for (int message = 0; message < message_count; ++message) {
            PersistedChatMessage record{};
            uint8_t self = 0;
            if (reader.read(record.sender, sizeof(record.sender)) != sizeof(record.sender) ||
                reader.read(record.text, sizeof(record.text)) != sizeof(record.text) ||
                reader.read(&record.timestamp, sizeof(record.timestamp)) !=
                    sizeof(record.timestamp) ||
                reader.read(&self, 1) != 1) {
                return false;
            }
            record.sender[sizeof(record.sender) - 1] = '\0';
            record.text[sizeof(record.text) - 1] = '\0';
            record.is_self = self != 0;
            if (!write_message(name, record, ctx)) return false;
            ++loaded;
        }
    }
    return true;
}

} // namespace

bool chatHistorySave(int channel_count,
                     ChatHistoryChannelReadFn read_channel,
                     ChatHistoryMessageReadFn read_message,
                     void* ctx)
{
    if (!ensureFs()) return false;
    SaveCtx save_ctx{channel_count, read_channel, read_message, ctx};
    return sigurdos::storage::atomicFileReplace(
        historyPath(), writeHistory, &save_ctx, validateHistory, nullptr);
}

int chatHistoryLoad(ChatHistoryMessageWriteFn write_message, void* ctx)
{
    if (!write_message || !ensureFs()) return 0;
    sigurdos::storage::atomicFileRecover(historyPath(), validateHistory, nullptr);
    if (!pathExists(historyPath())) return 0;

#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(historyPath(), "r");
    if (!file) return 0;
    sigurdos::storage::AtomicFileReader reader(&file);
#else
    FILE* file = std::fopen(historyPath(), "rb");
    if (!file) return 0;
    sigurdos::storage::AtomicFileReader reader(file);
#endif
    int loaded = 0;
    loadFromReader(reader, write_message, ctx, loaded);
#if defined(ESP32_PLATFORM)
    file.close();
#else
    std::fclose(file);
#endif
    return loaded;
}

bool chatHistoryClear()
{
    if (!ensureFs()) return false;
    char temp_path[192];
    if (!sigurdos::storage::atomicFileTempPath(
            historyPath(), temp_path, sizeof(temp_path))) return false;
    const bool temp_ok = removePath(temp_path);
    return removePath(historyPath()) && temp_ok;
}

#if !defined(ESP32_PLATFORM)
void chatHistorySetNativePath(const char* path)
{
    if (!path || !path[0]) return;
    std::strncpy(g_history_path, path, sizeof(g_history_path) - 1);
    g_history_path[sizeof(g_history_path) - 1] = '\0';
}
#endif

} // namespace sigurdos::ui
