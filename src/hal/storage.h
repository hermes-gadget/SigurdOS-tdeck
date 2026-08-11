// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstdbool>
#include <cstdint>

namespace sigurdos {

enum class StorageWarmState : uint8_t {
    Idle,
    Warming,
    Ready,
    Stopping,
};

// Safe storage initialiser — mounts SPIFFS, auto-formatting only
// after every byte in the partition is proven erased (clean flash / factory
// reset). A programmed byte or partition read error preserves the partition.
// Corrupt data is left untouched (user must factory-reset to recover).
//
// Returns true if SPIFFS is usable after this call.
// Safe to call multiple times (idempotent — returns cached result).
bool storage_init();

// Returns true if SPIFFS was mounted successfully during init.
// Used by downstream code to avoid repeated SPIFFS begin() calls
// when storage is known unavailable.
bool storage_available();

// Returns true if SPIFFS is mounted and usable, mounting it on first
// use if needed. The mount happens at most once per boot, so all
// downstream modules share a single mount instead of each calling
// SPIFFS.begin() (which emits a "SPIFFS Already Mounted!" warning per
// redundant call). Requires storage_init() to have succeeded first.
bool storage_ensure_mounted();

// Absorb the first-write SPIFFS GC cost (10-90s on this encrypted
// partition) in a background task shortly after boot, so the first
// runtime commit — channel store, contacts — stays fast. Safe to call
// once after storage_init() succeeds; never blocks the caller.
void storage_warm_after_mount();

// Stop the warm task, wait for it to leave the filesystem, and mark SPIFFS
// unmounted. The wait is bounded; false means the caller must not tear down
// SPIFFS yet because the warm task is still alive.
//
// The WiFi OTA teardown path also needs this interlock. It is owned by wave 2;
// wave 2 must call storage_stop_warm() before its SPIFFS.end() call.
bool storage_stop_warm();
StorageWarmState storage_warm_state();

// Reset internal state (for unit tests only).
// Resets the init-called and availability flags so tests can
// start from a clean state.
void storage_reset();

} // namespace sigurdos
