// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "prefs.h"
#include <Preferences.h>

namespace slopos {

static constexpr const char* NVS_NS = "slopos";
static NodePrefs g_prefs;

bool prefs_load(NodePrefs& p) {
    Preferences nvs;
    if (!nvs.begin(NVS_NS, true)) return false;

    nvs.getString("name", p.node_name, sizeof(p.node_name));
    p.freq         = nvs.getFloat("freq", 0.0f);
    p.bw           = nvs.getFloat("bw", 0.0f);
    p.sf           = nvs.getUChar("sf", 0);
    p.cr           = nvs.getUChar("cr", 0);
    p.tx_power_dbm  = nvs.getChar("txpwr", 0);
    if (p.tx_power_dbm > 0 && p.tx_power_dbm < 2) p.tx_power_dbm = 2;
    if (p.tx_power_dbm > 22) p.tx_power_dbm = 22;
    p.configured    = nvs.getBool("cfg", false);
    p.kbd_backlight = nvs.getUChar("kbd_bl", 127);
    p.chat_msg_cap  = nvs.getUShort("chat_cap", 200);

    nvs.end();
    return true;
}

bool prefs_save(const NodePrefs& p) {
    Preferences nvs;
    if (!nvs.begin(NVS_NS, false)) return false;

    nvs.putString("name", p.node_name);
    nvs.putFloat("freq", p.freq);
    nvs.putFloat("bw", p.bw);
    nvs.putUChar("sf", p.sf);
    nvs.putUChar("cr", p.cr);
    nvs.putChar("txpwr", p.tx_power_dbm < 2 ? (int8_t)2 : (p.tx_power_dbm > 22 ? (int8_t)22 : p.tx_power_dbm));
    nvs.putBool("cfg", p.configured);
    nvs.putUChar("kbd_bl", p.kbd_backlight);
    nvs.putUShort("chat_cap", p.chat_msg_cap);

    nvs.end();
    return true;
}

bool prefs_exists() {
    Preferences nvs;
    if (!nvs.begin(NVS_NS, true)) return false;
    bool exists = nvs.isKey("cfg");
    nvs.end();
    return exists;
}

const NodePrefs& prefs_get() {
    static bool loaded = false;
    if (!loaded) {
        g_prefs.set_defaults();
        prefs_load(g_prefs);
        loaded = true;
    }
    return g_prefs;
}

void prefs_set(const NodePrefs& p) {
    g_prefs = p;
    prefs_save(p);
}

} // namespace slopos
