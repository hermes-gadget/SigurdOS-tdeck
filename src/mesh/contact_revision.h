// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstdint>

namespace sigurdos::mesh {

inline bool nextContactRevision(uint32_t clock_value, uint32_t high_water,
                                uint32_t* out)
{
    if (!out || high_water == UINT32_MAX) return false;
    uint32_t revision = clock_value;
    if (revision <= high_water) revision = high_water + 1;
    if (revision == 0) revision = 1;
    *out = revision;
    return true;
}

} // namespace sigurdos::mesh
