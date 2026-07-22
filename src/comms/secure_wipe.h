#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace comms {

// Volatile stores prevent the compiler from removing erasure of data whose
// logical lifetime has ended.
inline void secureWipe(void* data, size_t len)
{
    volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
    while (p && len-- > 0) *p++ = 0;
}

class ScopedWipe {
public:
    ScopedWipe(void* data, size_t len) : _data(data), _len(len) {}
    ~ScopedWipe() { secureWipe(_data, _len); }
    ScopedWipe(const ScopedWipe&) = delete;
    ScopedWipe& operator=(const ScopedWipe&) = delete;

private:
    void* _data;
    size_t _len;
};

} // namespace comms
} // namespace sigurdos
