// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Mock prefs implementation for native test environment.
// Provides stub implementations so keyboard.cpp (which includes prefs.h)
// can compile and link without real NVS (Preferences) hardware.

#include "hal/prefs.h"

namespace sigurdos {

static NodePrefs g_prefs;

bool prefs_load(NodePrefs& p) {
    p = g_prefs;
    return true;
}

bool prefs_save(const NodePrefs& p) {
    g_prefs = p;
    return true;
}

bool prefs_exists() {
    return true;
}

const NodePrefs& prefs_get() {
    return g_prefs;
}

void prefs_set(const NodePrefs& p) {
    g_prefs = p;
    if (g_prefs.gps_interval < 5) g_prefs.gps_interval = 5;
}

} // namespace sigurdos
