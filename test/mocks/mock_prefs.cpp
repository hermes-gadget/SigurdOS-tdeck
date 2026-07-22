// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Mock prefs implementation for native test environment.
// Provides stub implementations so keyboard.cpp (which includes prefs.h)
// can compile and link without real NVS (Preferences) hardware.

#include "hal/prefs.h"
#include "mocks/mock_state.h"

namespace sigurdos {

static NodePrefs g_prefs;
static bool g_prefs_set_result = true;

void prefs_mock_set_save_result(bool result) { g_prefs_set_result = result; }

void prefs_mock_reset() {
    g_prefs = NodePrefs{};
}

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

bool prefs_set(const NodePrefs& p) {
    if (!g_prefs_set_result) return false;
    g_prefs = p;
    if (g_prefs.gps_interval > 86400) g_prefs.gps_interval = 86400;
    return true;
}

bool prefs_set_ble_enabled(bool enabled) {
    g_prefs.ble_enabled = enabled;
    g_prefs.ble_user_set = true;
    return true;
}

int loadChatScopePreferences(ChatScopePreference*, int) { return 0; }
bool saveChatScopePreference(const char*, const char*, const uint8_t[16]) { return true; }
bool removeChatScopePreference(const char*) { return true; }

} // namespace sigurdos
