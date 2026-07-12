// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos::mesh {

template <size_t Capacity>
class CmdResponseQueue {
public:
    static_assert(Capacity > 0, "command response queue must have capacity");

    struct Entry {
        char name[32]{};
        char text[160]{};
        uint32_t timestamp = 0;
    };

    bool push(const char* name, const char* text, uint32_t timestamp)
    {
        if (!name || !text || count_ >= Capacity) return false;
        Entry& entry = entries_[(head_ + count_) % Capacity];
        copy(entry.name, sizeof(entry.name), name);
        copy(entry.text, sizeof(entry.text), text);
        entry.timestamp = timestamp;
        ++count_;
        return true;
    }

    bool poll(char* name_out, size_t name_size, char* text_out, size_t text_size,
              uint32_t* timestamp_out)
    {
        if (count_ == 0) return false;
        const Entry& entry = entries_[head_];
        copy(name_out, name_size, entry.name);
        copy(text_out, text_size, entry.text);
        if (timestamp_out) *timestamp_out = entry.timestamp;
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }

    void clear()
    {
        head_ = 0;
        count_ = 0;
    }

    size_t size() const { return count_; }

private:
    static void copy(char* destination, size_t size, const char* source)
    {
        if (!destination || size == 0) return;
        std::strncpy(destination, source ? source : "", size - 1);
        destination[size - 1] = '\0';
    }

    Entry entries_[Capacity]{};
    size_t head_ = 0;
    size_t count_ = 0;
};

} // namespace sigurdos::mesh
