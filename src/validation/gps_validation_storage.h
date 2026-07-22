// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include "hal/storage.h"

namespace sigurdos {
namespace gps_validation {

// Validation firmware must use the same fail-closed mount/format policy as
// production so a diagnostic image cannot erase a populated device.
inline bool init_storage()
{
    return storage_init();
}

} // namespace gps_validation
} // namespace sigurdos
