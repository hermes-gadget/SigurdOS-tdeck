#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>
#include <cstddef>

namespace sigurdos {
namespace ui {

// One chat message as cached per channel (ARCH-001, issue #820 — extracted
// from chat_screen.cpp so the buffer mechanics are testable off-target).
struct ChannelMessage {
    char     sender[32];
    char     text[160];
    uint32_t timestamp;
    bool     is_self;
    bool     acked;
};

// Owns one channel's message array: allocation with PSRAM-first/DRAM-fallback
// (#542), FIFO eviction at the effective cap, trimming after cap changes, and
// buffer handoff for the channel remap in refresh_channels(). Non-copyable;
// buffers move between channel slots with takeFrom().
class ChatMessageBuffer {
public:
    ChatMessageBuffer() = default;
    ~ChatMessageBuffer() { release(); }
    ChatMessageBuffer(const ChatMessageBuffer&) = delete;
    ChatMessageBuffer& operator=(const ChatMessageBuffer&) = delete;

    // Allocate lazily: full_cap entries in PSRAM, falling back to
    // fallback_cap entries in DRAM when PSRAM is exhausted (#542).
    // No-op once allocated. Returns true when a buffer is available.
    bool ensure(uint16_t full_cap, uint16_t fallback_cap);

    bool allocated() const { return _msgs != nullptr; }
    uint16_t count() const { return _count; }
    uint16_t capacity() const { return _capacity; }

    ChannelMessage& at(uint16_t i) { return _msgs[i]; }
    const ChannelMessage& at(uint16_t i) const { return _msgs[i]; }

    // Drop the oldest entries so at most cap remain. cap is clamped to the
    // allocated capacity; cap == 0 is a no-op (matches legacy behavior).
    void trim(uint16_t cap);

    // Append a message, evicting the oldest entry once the effective cap
    // (min of user_cap and allocated capacity, #542) is reached. sender and
    // text are truncated to fit and may be null. Returns the stored message,
    // or nullptr when no buffer could be allocated.
    ChannelMessage* append(const char* sender, const char* text,
                           uint32_t timestamp, bool is_self,
                           uint16_t user_cap, uint16_t full_cap,
                           uint16_t fallback_cap);

    // Mark the newest message acknowledged (no-op when empty).
    void markLastAcked();

    // Release our buffer and steal other's buffer, count, and capacity,
    // leaving other empty. Used by the channel remap.
    void takeFrom(ChatMessageBuffer& other);

    // Free the buffer and reset to the unallocated state.
    void release();

private:
    ChannelMessage* _msgs = nullptr;
    uint16_t _capacity = 0;
    uint16_t _count = 0;
};

} // namespace ui
} // namespace sigurdos
