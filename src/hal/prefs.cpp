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

    // Use a temp buffer so the default set by set_defaults() is preserved
    // if the NVS key is absent (defensive against library version differences)
    char name_tmp[sizeof(p.node_name)] = {0};
    size_t name_len = nvs.getString("name", name_tmp, sizeof(name_tmp));
    if (name_len > 0 && name_len < sizeof(p.node_name)) {
        memcpy(p.node_name, name_tmp, name_len);
        p.node_name[name_len] = '\0';
    }
    p.freq         = nvs.getFloat("freq", 0.0f);
    p.bw           = nvs.getFloat("bw", 0.0f);
    p.sf           = nvs.getUChar("sf", 0);
    p.cr           = nvs.getUChar("cr", 0);
    p.tx_power_dbm  = nvs.getChar("txpwr", 0);
    if (p.tx_power_dbm > 0 && p.tx_power_dbm < 2) p.tx_power_dbm = 2;
    if (p.tx_power_dbm > 22) p.tx_power_dbm = 22;
    p.configured    = nvs.getBool("cfg", false);
    p.kbd_backlight = nvs.getUChar("kbd_bl", 127);
    p.display_brightness = nvs.getUChar("disp_bl", 200);
    p.auto_off_timeout = nvs.getUShort("auto_off", 30);
    p.chat_msg_cap  = nvs.getUShort("chat_cap", 200);
    p.flood_max_hops = nvs.getUChar("flood_mh", 0);
    p.share_location = nvs.getBool("sh_loc", true);
    p.rx_delay_base  = nvs.getFloat("rx_del", 10.0f);
    p.tx_delay_factor = nvs.getFloat("tx_del", 1.0f);
    p.direct_tx_delay_factor = nvs.getFloat("dir_tx", 1.0f);
    p.rx_boosted_gain = nvs.getBool("rx_boost", false);
    p.duty_cycle = nvs.getUChar("duty_cyc", 0);
    p.advert_interval = nvs.getUChar("adv_int", 0);
    p.advert_type = nvs.getUChar("adv_type", 1);

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
    nvs.putUChar("disp_bl", p.display_brightness);
    nvs.putUShort("auto_off", p.auto_off_timeout);
    nvs.putUShort("chat_cap", p.chat_msg_cap);
    nvs.putUChar("flood_mh", p.flood_max_hops);
    nvs.putBool("sh_loc", p.share_location);
    nvs.putFloat("rx_del", p.rx_delay_base);
    nvs.putFloat("tx_del", p.tx_delay_factor);
    nvs.putFloat("dir_tx", p.direct_tx_delay_factor);
    nvs.putBool("rx_boost", p.rx_boosted_gain);
    nvs.putUChar("duty_cyc", p.duty_cycle);
    nvs.putUChar("adv_int", p.advert_interval);
    nvs.putUChar("adv_type", p.advert_type);

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
