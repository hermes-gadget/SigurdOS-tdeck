#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// debug_cfg.h — Per-feature debug flags for SigurdOS.
//
// Each feature can be enabled independently by setting its -D flag:
//   -DSIGURDOS_DEBUG_DISPLAY=1  — flush/invalidate/auto-off display debug
//   -DSIGURDOS_DEBUG_MESH=1     — mesh message rx/tx, radio init, packet logging
//   -DSIGURDOS_DEBUG_UI=1       — UI boot steps, screen transitions, layout
//   -DSIGURDOS_DEBUG_DIAG=1     — periodic stats & system dumps (debug.cpp)
//   -DSIGURDOS_DEBUG_MAP=1      — map tile loading, rendering diagnostics
//   -DSIGURDOS_TRACKBALL_DEBUG=1— trackball event/state debug
//   -DSIGURDOS_TELEMETRY=1      — structured telemetry for AI agent (replaces debug above)
//
// When SIGURDOS_DEBUG=1 is set (no individual specifier), ALL per-feature
// flags default to enabled for backward compatibility.
// When only individual flags are set, only those features produce output.

#include <cstdint>

// Determine if SIGURDOS_DEBUG master switch is active (numeric 1)
#if defined(SIGURDOS_DEBUG) && (SIGURDOS_DEBUG)
#define SIGURDOS_DEBUG_ACTIVE 1
#else
#define SIGURDOS_DEBUG_ACTIVE 0
#endif

// Per-feature: default to master switch, allow individual -D override
#ifndef SIGURDOS_DEBUG_DISPLAY
#define SIGURDOS_DEBUG_DISPLAY SIGURDOS_DEBUG_ACTIVE
#endif

#ifndef SIGURDOS_DEBUG_MESH
#define SIGURDOS_DEBUG_MESH SIGURDOS_DEBUG_ACTIVE
#endif

#ifndef SIGURDOS_DEBUG_UI
#define SIGURDOS_DEBUG_UI SIGURDOS_DEBUG_ACTIVE
#endif

#ifndef SIGURDOS_DEBUG_MAP
#define SIGURDOS_DEBUG_MAP SIGURDOS_DEBUG_ACTIVE
#endif

#ifndef SIGURDOS_DEBUG_DIAG
#define SIGURDOS_DEBUG_DIAG SIGURDOS_DEBUG_ACTIVE
#endif

// Per-feature runtime state declarations.
// These are always compiled so the test controller can call them
// regardless of compile-time gating. In non-debug builds the stubs
// in debug.h handle them as no-ops.

// Convenience macros for runtime feature checks at call sites.
// When the full debug module (SIGURDOS_DEBUG) is compiled, these wrap
// the call with a runtime feat_get_*() check so 'debug feat 0' works.
// In standalone per-feature builds, they compile to nothing.
#if defined(SIGURDOS_DEBUG) && SIGURDOS_DEBUG
#define SIGURDOS_RUNTIME_FEAT(feat) if (sigurdos::debug::feat_get_##feat())
#else
#define SIGURDOS_RUNTIME_FEAT(feat)
#endif

namespace sigurdos {
namespace debug {

void   feat_set_display(bool on);
bool   feat_get_display();
void   feat_set_mesh(bool on);
bool   feat_get_mesh();
void   feat_set_ui(bool on);
bool   feat_get_ui();
void   feat_set_map(bool on);
bool   feat_get_map();
void   feat_set_diag(bool on);
bool   feat_get_diag();

void   feat_set_all_mask(bool on);
void   feat_from_mask(uint8_t mask);
uint8_t feat_to_mask();

} // namespace debug
} // namespace sigurdos

// ── Telemetry system flags ────────────────────────────
// Set with -D SIGURDOS_TELEMETRY=1 for structured AI-agent telemetry.
// This is mutually exclusive with SIGURDOS_DEBUG=1 (full printf debug).
#ifndef SIGURDOS_TELEMETRY
#define SIGURDOS_TELEMETRY 0
#endif
#ifndef SIGURDOS_TELEMETRY_HEARTBEAT_MS
#define SIGURDOS_TELEMETRY_HEARTBEAT_MS 5000
#endif
#ifndef SIGURDOS_TELEMETRY_DIFF
#define SIGURDOS_TELEMETRY_DIFF 0
#endif
