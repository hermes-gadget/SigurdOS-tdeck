#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// debug_cfg.h — Per-feature debug flags for SlopOS-TDeck.
//
// Each feature can be enabled independently by setting its -D flag:
//   -DSLOPOS_DEBUG_DISPLAY=1  — flush/invalidate/auto-off display debug
//   -DSLOPOS_DEBUG_MESH=1     — mesh message rx/tx, radio init, packet logging
//   -DSLOPOS_DEBUG_UI=1       — UI boot steps, screen transitions, layout
//   -DSLOPOS_DEBUG_MAP=1      — map tile loading, rendering diagnostics
//   -DSLOPOS_DEBUG_DIAG=1     — periodic stats & system dumps (debug.cpp)
//   -DSLOPOS_TRACKBALL_DEBUG=1— trackball event/state debug
//
// When SLOPOS_DEBUG=1 is set (no individual specifier), ALL per-feature
// flags default to enabled for backward compatibility.
// When only individual flags are set, only those features produce output.

#include <cstdint>

// Determine if SLOPOS_DEBUG master switch is active (numeric 1)
#if defined(SLOPOS_DEBUG) && (SLOPOS_DEBUG)
#define SLOPOS_DEBUG_ACTIVE 1
#else
#define SLOPOS_DEBUG_ACTIVE 0
#endif

// Per-feature: default to master switch, allow individual -D override
#ifndef SLOPOS_DEBUG_DISPLAY
#define SLOPOS_DEBUG_DISPLAY SLOPOS_DEBUG_ACTIVE
#endif

#ifndef SLOPOS_DEBUG_MESH
#define SLOPOS_DEBUG_MESH SLOPOS_DEBUG_ACTIVE
#endif

#ifndef SLOPOS_DEBUG_UI
#define SLOPOS_DEBUG_UI SLOPOS_DEBUG_ACTIVE
#endif

#ifndef SLOPOS_DEBUG_MAP
#define SLOPOS_DEBUG_MAP SLOPOS_DEBUG_ACTIVE
#endif

#ifndef SLOPOS_DEBUG_DIAG
#define SLOPOS_DEBUG_DIAG SLOPOS_DEBUG_ACTIVE
#endif

// Per-feature runtime state declarations.
// These are always compiled so the test controller can call them
// regardless of compile-time gating. In non-debug builds the stubs
// in debug.h handle them as no-ops.

// Convenience macros for runtime feature checks at call sites.
// When the full debug module (SLOPOS_DEBUG) is compiled, these wrap
// the call with a runtime feat_get_*() check so 'debug feat 0' works.
// In standalone per-feature builds, they compile to nothing.
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
#define SLOPOS_RUNTIME_FEAT(feat) if (slopos::debug::feat_get_##feat())
#else
#define SLOPOS_RUNTIME_FEAT(feat)
#endif

namespace slopos {
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
} // namespace slopos
