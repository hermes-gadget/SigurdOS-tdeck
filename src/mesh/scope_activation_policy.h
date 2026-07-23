// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstdint>

#include "region_name.h"
#include "scope_key_hex.h"

namespace sigurdos {
namespace mesh {

inline bool scopeActivationInputsValid(const char* name,
                                       const uint8_t* private_key)
{
    if (!name || !name[0]) return true;  // clear/unscoped
    if (!regionNameValid(name)) return false;
    if (name[0] != '$') return true;
    return scopeKeyNonZero(private_key);
}

} // namespace mesh
} // namespace sigurdos
