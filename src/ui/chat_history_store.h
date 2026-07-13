// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos::ui {

// Read-only codec for the retired /msgs snapshot. New messages are written
// only to mesh/message_store; this exists solely for one-time boot migration.
static constexpr size_t LEGACY_CHAT_HISTORY_CHANNEL_NAME_LEN = 32;
static constexpr size_t LEGACY_CHAT_HISTORY_SENDER_LEN = 32;
static constexpr size_t LEGACY_CHAT_HISTORY_TEXT_LEN = 160;
static constexpr size_t LEGACY_CHAT_HISTORY_MAX_CHANNELS = 16;
static constexpr size_t LEGACY_CHAT_HISTORY_MAX_MESSAGES = 200;

struct LegacyChatMessage {
    char sender[LEGACY_CHAT_HISTORY_SENDER_LEN];
    char text[LEGACY_CHAT_HISTORY_TEXT_LEN];
    uint32_t timestamp;
    bool is_self;
};

using LegacyChatHistoryMessageFn = bool (*)(
    const char* channel, const LegacyChatMessage& message, void* ctx);

enum class LegacyChatHistoryResult : uint8_t {
    NotFound,
    Migrated,
    Failed,
};

// Streams every complete legacy record to write_message. The legacy live and
// temp files are removed only after every callback succeeds. A failed partial
// migration is safe to retry because the unified store deduplicates records.
LegacyChatHistoryResult legacyChatHistoryMigrate(
    LegacyChatHistoryMessageFn write_message, void* ctx = nullptr,
    int* migrated_count = nullptr);

#if !defined(ESP32_PLATFORM)
void legacyChatHistorySetNativePath(const char* path);
#endif

} // namespace sigurdos::ui
