// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "chat_message_buffer.h"

#include <cstring>

#include <esp_heap_caps.h>

namespace sigurdos {
namespace ui {

bool ChatMessageBuffer::ensure(uint16_t full_cap, uint16_t fallback_cap)
{
    if (_msgs) return true;
    if (full_cap == 0) return false;

    const size_t bytes = (size_t)full_cap * sizeof(ChannelMessage);
    _msgs = (ChannelMessage*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (_msgs) {
        _capacity = full_cap;
        return true;
    }

    // PSRAM exhausted — fall back to DRAM at reduced capacity (#542)
    const size_t dram_bytes = (size_t)fallback_cap * sizeof(ChannelMessage);
    if (fallback_cap > 0) {
        _msgs = (ChannelMessage*)heap_caps_malloc(dram_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    _capacity = _msgs ? fallback_cap : 0;
    return _msgs != nullptr;
}

void ChatMessageBuffer::trim(uint16_t cap)
{
    if (cap == 0 || !_msgs) return;

    const uint16_t effective_cap = (cap < _capacity) ? cap : _capacity;
    if (_count <= effective_cap) return;

    const uint16_t drop = _count - effective_cap;
    memmove(&_msgs[0], &_msgs[drop], (size_t)effective_cap * sizeof(ChannelMessage));
    _count = effective_cap;
}

ChannelMessage* ChatMessageBuffer::append(const char* sender, const char* text,
                                          uint32_t timestamp, bool is_self,
                                          uint16_t user_cap, uint16_t full_cap,
                                          uint16_t fallback_cap)
{
    if (!ensure(full_cap, fallback_cap)) return nullptr;

    // Use the allocated capacity if it's smaller than the user-configured cap.
    // Prevents OOB write when the DRAM fallback provided fewer entries (#542).
    const uint16_t cap = (_capacity > 0 && _capacity < user_cap) ? _capacity : user_cap;
    if (cap == 0) return nullptr;
    trim(cap);

    uint16_t pos = _count;
    if (pos >= cap) {
        for (uint16_t i = 1; i < cap; i++) _msgs[i - 1] = _msgs[i];
        pos = cap - 1;
    } else {
        _count++;
    }

    ChannelMessage& msg = _msgs[pos];
    strncpy(msg.sender, sender ? sender : "", sizeof(msg.sender) - 1);
    msg.sender[sizeof(msg.sender) - 1] = '\0';
    strncpy(msg.text, text ? text : "", sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';
    msg.timestamp = timestamp;
    msg.is_self = is_self;
    msg.acked = false;
    msg.confirmation_lost = false;
    return &msg;
}

void ChatMessageBuffer::markLastAcked()
{
    if (_msgs && _count > 0) {
        _msgs[_count - 1].acked = true;
    }
}

void ChatMessageBuffer::takeFrom(ChatMessageBuffer& other)
{
    if (this == &other) return;
    release();
    _msgs = other._msgs;
    _capacity = other._capacity;
    _count = other._count;
    other._msgs = nullptr;
    other._capacity = 0;
    other._count = 0;
}

void ChatMessageBuffer::release()
{
    if (_msgs) {
        heap_caps_free(_msgs);
        _msgs = nullptr;
    }
    _capacity = 0;
    _count = 0;
}

} // namespace ui
} // namespace sigurdos
