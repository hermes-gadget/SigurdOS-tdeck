// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos {
namespace mesh {

inline bool scopeActivationInputsValid(const char* name,
                                       const uint8_t* private_key)
{
    if (!name || !name[0]) return true;  // clear/unscoped
    const size_t length = strlen(name);
    if (length > 30) return false;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t c = static_cast<uint8_t>(name[i]);
        if (!(c == '-' || c == '$' || c == '#' ||
              (c >= '0' && c <= '9') || c >= 'A')) return false;
    }
    if (name[0] != '$') return true;
    if (!private_key) return false;
    uint8_t any = 0;
    for (size_t i = 0; i < 16; ++i) any |= private_key[i];
    return any != 0;
}

} // namespace mesh
} // namespace sigurdos
