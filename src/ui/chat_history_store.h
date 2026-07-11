// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos::ui {

static constexpr size_t CHAT_HISTORY_CHANNEL_NAME_LEN = 32;
static constexpr size_t CHAT_HISTORY_SENDER_LEN = 32;
static constexpr size_t CHAT_HISTORY_TEXT_LEN = 160;
static constexpr size_t CHAT_HISTORY_MAX_CHANNELS = 16;
static constexpr size_t CHAT_HISTORY_MAX_MESSAGES = 200;

struct PersistedChatMessage {
    char sender[CHAT_HISTORY_SENDER_LEN];
    char text[CHAT_HISTORY_TEXT_LEN];
    uint32_t timestamp;
    bool is_self;
};

using ChatHistoryChannelReadFn = bool (*)(
    int stored_index, char* name_out, size_t name_len,
    uint8_t* message_count_out, void* ctx);
using ChatHistoryMessageReadFn = bool (*)(
    int stored_index, int message_index,
    PersistedChatMessage* out, void* ctx);
using ChatHistoryMessageWriteFn = bool (*)(
    const char* channel, const PersistedChatMessage& message, void* ctx);

bool chatHistorySave(int channel_count,
                     ChatHistoryChannelReadFn read_channel,
                     ChatHistoryMessageReadFn read_message,
                     void* ctx);

// Returns the number of complete messages delivered to write_message.
int chatHistoryLoad(ChatHistoryMessageWriteFn write_message, void* ctx);

bool chatHistoryClear();

#if !defined(ESP32_PLATFORM)
void chatHistorySetNativePath(const char* path);
#endif

} // namespace sigurdos::ui
