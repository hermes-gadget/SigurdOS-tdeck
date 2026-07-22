#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>

namespace sigurdos::utils {

template <typename T, std::size_t Capacity>
class FixedQueue {
    static_assert(Capacity > 0, "FixedQueue capacity must be non-zero");

public:
    bool push(const T& value)
    {
        if (full()) return false;
        values_[head_] = value;
        head_ = (head_ + 1) % Capacity;
        ++size_;
        return true;
    }

    bool pop(T* out)
    {
        if (!out || empty()) return false;
        *out = values_[tail_];
        tail_ = (tail_ + 1) % Capacity;
        --size_;
        return true;
    }

    void reset()
    {
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

    constexpr std::size_t capacity() const { return Capacity; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == Capacity; }

private:
    T values_[Capacity]{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
};

} // namespace sigurdos::utils
