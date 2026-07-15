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
static constexpr size_t HISTORY_CHANNEL_BYTES = LEGACY_CHAT_HISTORY_CHANNEL_NAME_LEN + 1;
static constexpr size_t HISTORY_RECORD_BYTES =
    LEGACY_CHAT_HISTORY_SENDER_LEN + LEGACY_CHAT_HISTORY_TEXT_LEN + 4 + 1;
static constexpr size_t HISTORY_MAX_FILE_SIZE =
    HISTORY_HEADER_BYTES + LEGACY_CHAT_HISTORY_MAX_CHANNELS *
        (HISTORY_CHANNEL_BYTES +
         LEGACY_CHAT_HISTORY_MAX_MESSAGES * HISTORY_RECORD_BYTES);

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

bool readHeader(sigurdos::storage::AtomicFileReader& reader, uint8_t& channel_count)
{
    uint32_t magic = 0;
    uint8_t version = 0;
    return reader.size() >= HISTORY_HEADER_BYTES &&
        reader.size() <= HISTORY_MAX_FILE_SIZE && reader.seek(0) &&
        reader.read(&magic, sizeof(magic)) == sizeof(magic) &&
        magic == HISTORY_MAGIC && reader.read(&version, 1) == 1 &&
        version == HISTORY_VERSION && reader.read(&channel_count, 1) == 1 &&
        channel_count <= LEGACY_CHAT_HISTORY_MAX_CHANNELS;
}

bool validateHistory(sigurdos::storage::AtomicFileReader& reader, void*)
{
    uint8_t channel_count = 0;
    if (!readHeader(reader, channel_count)) return false;
    size_t expected_size = HISTORY_HEADER_BYTES;
    for (int channel = 0; channel < channel_count; ++channel) {
        char name[LEGACY_CHAT_HISTORY_CHANNEL_NAME_LEN];
        uint8_t message_count = 0;
        if (reader.read(name, sizeof(name)) != sizeof(name) ||
            reader.read(&message_count, 1) != 1 ||
            message_count > LEGACY_CHAT_HISTORY_MAX_MESSAGES) {
            return false;
        }
        expected_size += HISTORY_CHANNEL_BYTES +
            (size_t)message_count * HISTORY_RECORD_BYTES;
        if (expected_size > reader.size() || !reader.seek(expected_size)) return false;
    }
    return expected_size == reader.size();
}

bool loadFromReader(sigurdos::storage::AtomicFileReader& reader,
                    LegacyChatHistoryMessageFn write_message, void* ctx,
                    int& loaded)
{
    uint8_t channel_count = 0;
    if (!readHeader(reader, channel_count)) return false;
    for (int channel = 0; channel < channel_count; ++channel) {
        char name[LEGACY_CHAT_HISTORY_CHANNEL_NAME_LEN + 1] = {};
        uint8_t message_count = 0;
        if (reader.read(name, LEGACY_CHAT_HISTORY_CHANNEL_NAME_LEN) !=
                LEGACY_CHAT_HISTORY_CHANNEL_NAME_LEN ||
            reader.read(&message_count, 1) != 1 ||
            message_count > LEGACY_CHAT_HISTORY_MAX_MESSAGES) {
            return false;
        }
        name[LEGACY_CHAT_HISTORY_CHANNEL_NAME_LEN - 1] = '\0';

        for (int message = 0; message < message_count; ++message) {
            LegacyChatMessage record{};
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

LegacyChatHistoryResult legacyChatHistoryMigrate(
    LegacyChatHistoryMessageFn write_message, void* ctx, int* migrated_count)
{
    if (migrated_count) *migrated_count = 0;
    if (!write_message || !ensureFs()) return LegacyChatHistoryResult::Failed;
    sigurdos::storage::atomicFileRecover(historyPath(), validateHistory, nullptr);
    if (!pathExists(historyPath())) return LegacyChatHistoryResult::NotFound;

#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(historyPath(), "r");
    if (!file) return LegacyChatHistoryResult::Failed;
    sigurdos::storage::AtomicFileReader reader(&file);
#else
    FILE* file = std::fopen(historyPath(), "rb");
    if (!file) return LegacyChatHistoryResult::Failed;
    sigurdos::storage::AtomicFileReader reader(file);
#endif
    int loaded = 0;
    const bool loaded_ok = loadFromReader(reader, write_message, ctx, loaded);
#if defined(ESP32_PLATFORM)
    file.close();
#else
    std::fclose(file);
#endif
    if (!loaded_ok) return LegacyChatHistoryResult::Failed;

    char temp_path[192];
    if (!sigurdos::storage::atomicFileTempPath(
            historyPath(), temp_path, sizeof(temp_path)) ||
        !removePath(temp_path) || !removePath(historyPath())) {
        return LegacyChatHistoryResult::Failed;
    }
    if (migrated_count) *migrated_count = loaded;
    return LegacyChatHistoryResult::Migrated;
}

#if !defined(ESP32_PLATFORM)
void legacyChatHistorySetNativePath(const char* path)
{
    if (!path || !path[0]) return;
    std::strncpy(g_history_path, path, sizeof(g_history_path) - 1);
    g_history_path[sizeof(g_history_path) - 1] = '\0';
}
#endif

} // namespace sigurdos::ui
