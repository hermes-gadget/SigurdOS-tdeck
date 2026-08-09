// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>

namespace sigurdos::mesh::detail {

// A bounded FIFO for completed asynchronous responses. Once full, the oldest
// completed entry is reclaimed so a newly matched request is still observable;
// callers can consume one entry without clearing unrelated responses.
template <typename Entry, std::size_t Capacity>
class BoundedResponseQueue {
    static_assert(Capacity > 0, "response queue capacity must be non-zero");

public:
    Entry* appendSlot()
    {
        if (size_ == Capacity) {
            for (std::size_t i = 1; i < Capacity; ++i) {
                entries_[i - 1] = entries_[i];
            }
            --size_;
        }
        entries_[size_] = Entry{};
        return &entries_[size_++];
    }

    bool consume(std::size_t index)
    {
        if (index >= size_) return false;
        for (std::size_t i = index + 1; i < size_; ++i) {
            entries_[i - 1] = entries_[i];
        }
        --size_;
        entries_[size_] = Entry{};
        return true;
    }

    Entry* at(std::size_t index)
    {
        return index < size_ ? &entries_[index] : nullptr;
    }

    const Entry* at(std::size_t index) const
    {
        return index < size_ ? &entries_[index] : nullptr;
    }

    void clear()
    {
        for (std::size_t i = 0; i < Capacity; ++i) entries_[i] = Entry{};
        size_ = 0;
    }

    std::size_t size() const { return size_; }
    constexpr std::size_t capacity() const { return Capacity; }

private:
    Entry entries_[Capacity]{};
    std::size_t size_ = 0;
};

} // namespace sigurdos::mesh::detail
