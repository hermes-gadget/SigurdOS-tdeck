// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <climits>
#include <cstdint>
#include <cstring>

namespace sigurdos::ui {

class ChatUnreadStore {
public:
    static constexpr int CAPACITY = 16;
    static constexpr int NAME_CAPACITY = 37;

    bool increment(const char* conversation, bool mention)
    {
        Entry* entry = find(conversation, true);
        if (!entry) return false;
        if (entry->count < INT_MAX) ++entry->count;
        entry->mention = entry->mention || mention;
        return true;
    }

    void clear(const char* conversation)
    {
        Entry* entry = find(conversation, false);
        if (!entry) return;
        entry->count = 0;
        entry->mention = false;
    }

    int count(const char* conversation) const
    {
        const Entry* entry = find_const(conversation);
        return entry ? entry->count : 0;
    }

    int total() const
    {
        int result = 0;
        for (const Entry& entry : _entries) {
            if (entry.count > INT_MAX - result) return INT_MAX;
            result += entry.count;
        }
        return result;
    }

    bool has_mentions() const
    {
        for (const Entry& entry : _entries) {
            if (entry.count > 0 && entry.mention) return true;
        }
        return false;
    }

private:
    struct Entry {
        char name[NAME_CAPACITY]{};
        int count = 0;
        bool mention = false;
    };

    Entry* find(const char* conversation, bool create)
    {
        if (!conversation || !conversation[0]) return nullptr;
        Entry* empty = nullptr;
        for (Entry& entry : _entries) {
            if (entry.name[0] && std::strcmp(entry.name, conversation) == 0) return &entry;
            if (!empty && !entry.name[0]) empty = &entry;
        }
        if (!create || !empty || std::strlen(conversation) >= sizeof(empty->name)) return nullptr;
        std::strncpy(empty->name, conversation, sizeof(empty->name) - 1);
        return empty;
    }

    const Entry* find_const(const char* conversation) const
    {
        if (!conversation || !conversation[0]) return nullptr;
        for (const Entry& entry : _entries) {
            if (entry.name[0] && std::strcmp(entry.name, conversation) == 0) return &entry;
        }
        return nullptr;
    }

    Entry _entries[CAPACITY]{};
};

} // namespace sigurdos::ui
