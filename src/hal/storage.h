// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstdbool>

namespace sigurdos {

// Safe storage initialiser — mounts SPIFFS, auto-formatting only
// when the partition appears fully erased (clean flash / factory reset).
// Corrupt data is left untouched (user must factory-reset to recover).
//
// Returns true if SPIFFS is usable after this call.
// Safe to call multiple times (idempotent — returns cached result).
bool storage_init();

// Returns true if SPIFFS was mounted successfully during init.
// Used by downstream code to avoid repeated SPIFFS begin() calls
// when storage is known unavailable.
bool storage_available();

// Reset internal state (for unit tests only).
// Resets the init-called and availability flags so tests can
// start from a clean state.
void storage_reset();

} // namespace sigurdos
