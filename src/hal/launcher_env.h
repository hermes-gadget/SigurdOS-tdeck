// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Launcher environment detection — runtime check for bmorcelli/Launcher.
//
// Detection mechanism:
//   Primary: Launcher's resident app partition uses the `test` subtype
//   (ESP_PARTITION_SUBTYPE_APP_TEST = 0x20), which never appears in the
//   standard Arduino default_16MB.csv layout.
//
//   Confirmatory: Under Launcher, otadata lives at 0xD000 (vs 0xE000 in
//   the standalone layout). Both signals must be present.
//
//   Standalone devices always return false — no false positives possible
//   with the standard partition table (no `test` app partition exists).

#pragma once
#include <cstdint>

// Returns true when the current firmware is installed under bmorcelli/Launcher.
// Detection requires Launcher's `test`-subtype resident app partition and
// the Launcher otadata location at 0xD000.
bool sigurdos_is_under_launcher();

// Returns a human-readable description of the detected environment.
// Returns "standalone" or "bmorcelli/Launcher".
const char* sigurdos_launcher_env_name();
