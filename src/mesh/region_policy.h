#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdint>

namespace sigurdos::mesh {

bool regionFloodAllowed(bool region_found, uint8_t flags, uint8_t deny_flood_mask);
bool regionScopeIsClear(const char* name);
void copyActiveRegion(char* destination, std::size_t destination_size, const char* name);

} // namespace sigurdos::mesh
