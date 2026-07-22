// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "region_policy.h"

#include <cstring>

namespace sigurdos::mesh {

bool regionFloodAllowed(bool region_found, uint8_t flags, uint8_t deny_flood_mask)
{
    return !region_found || (flags & deny_flood_mask) == 0;
}

bool regionScopeIsClear(const char* name)
{
    return !name || !name[0] || std::strcmp(name, "<null>") == 0;
}

void copyActiveRegion(char* destination, std::size_t destination_size, const char* name)
{
    if (!destination || destination_size == 0) return;
    if (!name || !name[0]) {
        destination[0] = '\0';
        return;
    }
    std::strncpy(destination, name, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

} // namespace sigurdos::mesh
