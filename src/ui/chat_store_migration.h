// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include "chat_history_store.h"
#include "mesh/message_store.h"
#include "utils/utf8_util.h"

#include <algorithm>
#include <cstring>

namespace sigurdos::ui {

inline sigurdos::mesh::StoredMessage legacyChatMessageToStored(
    const char* channel, const LegacyChatMessage& legacy)
{
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, channel ? channel : "",
                 sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, legacy.sender, sizeof(msg.sender) - 1);
    msg.timestamp = legacy.timestamp;
    msg.is_self = legacy.is_self;
    msg.is_channel = channel && std::strncmp(channel, "DM: ", 4) != 0;
    msg.path_len = 0xFF;
    // Self-authored records are never mirrored to the companion app.
    msg.companion_sent = legacy.is_self;
    if (msg.is_channel && !msg.is_self && legacy.sender[0]) {
        size_t pos = 0;
        const size_t sender_len = std::min(
            std::strlen(legacy.sender), sizeof(msg.text) - 1);
        std::memcpy(msg.text + pos, legacy.sender, sender_len);
        pos += sender_len;
        if (pos + 2 < sizeof(msg.text)) {
            msg.text[pos++] = ':';
            msg.text[pos++] = ' ';
        }
        const size_t keep = sigurdos::utf8_truncate_bytes(
            legacy.text, sizeof(msg.text) - 1 - pos);
        std::memcpy(msg.text + pos, legacy.text, keep);
        msg.text[pos + keep] = '\0';
    } else {
        sigurdos::utf8_copy_truncate(msg.text, sizeof(msg.text), legacy.text);
    }
    return msg;
}

} // namespace sigurdos::ui
