// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mesh_wrapper.h"

namespace sigurdos::mesh::detail {

// Parse one complete CayenneLPP container. The destination is changed only
// after every item has passed shape and bounds validation.
bool parseTelemetryLpp(const uint8_t* data, size_t len, TelemetryResult* out);

} // namespace sigurdos::mesh::detail
