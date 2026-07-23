#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstring>

namespace sigurdos::ui {

constexpr std::size_t CHAT_MESH_CONVERSATION_CAPACITY = 16;
constexpr std::size_t CHAT_DM_CONVERSATION_CAPACITY = 16;
constexpr std::size_t CHAT_CONVERSATION_CAPACITY =
    CHAT_MESH_CONVERSATION_CAPACITY + CHAT_DM_CONVERSATION_CAPACITY;

// Non-owning presentation index over the canonical conversation registry.
// Rebuilding a view never moves names, metadata, unread counts, or message
// buffers; consumers translate a visible row back to its canonical index.
template <std::size_t Capacity>
class ChatConversationView {
public:
    template <std::size_t NameCapacity>
    void rebuild(const char (&names)[Capacity][NameCapacity], int canonical_count,
                 int filter_mode)
    {
        count_ = 0;
        if (canonical_count < 0) canonical_count = 0;
        if (canonical_count > static_cast<int>(Capacity)) {
            canonical_count = static_cast<int>(Capacity);
        }

        for (int canonical = 0; canonical < canonical_count; ++canonical) {
            const bool is_dm = std::strncmp(names[canonical], "DM:", 3) == 0;
            if ((filter_mode == 1 && is_dm) ||
                (filter_mode == 2 && !is_dm)) {
                continue;
            }
            indices_[count_++] = canonical;
        }
    }

    int count() const { return count_; }

    int canonicalIndex(int visible_index) const
    {
        if (visible_index < 0 || visible_index >= count_) return -1;
        return indices_[visible_index];
    }

    int visibleIndex(int canonical_index) const
    {
        for (int visible = 0; visible < count_; ++visible) {
            if (indices_[visible] == canonical_index) return visible;
        }
        return -1;
    }

private:
    int indices_[Capacity] = {};
    int count_ = 0;
};

} // namespace sigurdos::ui
