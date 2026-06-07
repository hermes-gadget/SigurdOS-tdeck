// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "prefs.h"
#include <Preferences.h>

namespace sigurdos {

static constexpr const char* NVS_NS = "sigurdos";
static NodePrefs g_prefs;

#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
static constexpr bool DEFAULT_BLE_ENABLED = true;
#else
static constexpr bool DEFAULT_BLE_ENABLED = false;
#endif

bool prefs_load(NodePrefs& p) {
    Preferences nvs;
    if (!nvs.begin(NVS_NS, true)) return false;

    // Use a temp buffer so the default set by set_defaults() is preserved
    // if the NVS key is absent (defensive against library version differences)
    char name_tmp[sizeof(p.node_name)] = {0};
    size_t name_len = nvs.getString("name", name_tmp, sizeof(name_tmp));
    // Accept any non-empty string that fits (Preferences may return
    // length including terminator; also accept exactly-full 31-char names)
    if (name_len > 0 && name_len <= sizeof(p.node_name)) {
        memcpy(p.node_name, name_tmp, sizeof(p.node_name) - 1);
        p.node_name[sizeof(p.node_name) - 1] = '\0';
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
    // Clamp recovered brightness to safe range (0 = dead screen, >240 may wrap)
    if (p.display_brightness < 20) p.display_brightness = 20;
    if (p.display_brightness > 240) p.display_brightness = 240;
    p.auto_off_timeout = nvs.getUShort("auto_off", 30);
    p.chat_msg_cap  = nvs.getUShort("chat_cap", 200);
    p.flood_max_hops = nvs.getUChar("flood_mh", 0);
    p.share_location = nvs.getBool("sh_loc", false);
    p.advert_location_valid = nvs.getBool("adv_loc", false);
    p.advert_lat = nvs.getInt("adv_lat", 0);
    p.advert_lon = nvs.getInt("adv_lon", 0);
    p.rx_delay_base  = nvs.getFloat("rx_del", 10.0f);
    p.tx_delay_factor = nvs.getFloat("tx_del", 1.0f);
    p.direct_tx_delay_factor = nvs.getFloat("dir_tx", 1.0f);
    p.rx_boosted_gain = nvs.getBool("rx_boost", false);
    p.duty_cycle = nvs.getUChar("duty_cyc", 0);
    p.advert_interval = nvs.getUChar("adv_int", 0);
    p.advert_type = nvs.getUChar("adv_type", 1);
    p.theme_id = nvs.getUChar("theme", 0);
    p.multi_acks = nvs.getBool("multi_ack", false);
    p.buzzer_quiet = nvs.getBool("buzz_q", false);
    p.gps_enabled = nvs.getBool("gps_en", false);
    p.gps_interval = nvs.getUShort("gps_int", 0);
    p.autoadd_config = nvs.getUChar("autoadd_cfg", 0x1E);
    p.autoadd_max_hops = nvs.getUChar("autoadd_mh", 0);
    p.client_repeat = nvs.getUChar("clirep", 0);
    // BLE-specific firmware should be discoverable on first boot, while a
    // saved user preference still controls later boots.
    p.ble_enabled = nvs.getBool("ble_en", DEFAULT_BLE_ENABLED);
    p.device_pin = nvs.getULong("dev_pin", 0);
    // WiFi credentials (GitHub OTA)
    size_t ssid_len = nvs.getString("wifi_ssid", p.wifi_ssid, sizeof(p.wifi_ssid));
    if (ssid_len == 0) p.wifi_ssid[0] = '\0';
    size_t pw_len = nvs.getString("wifi_pw", p.wifi_password, sizeof(p.wifi_password));
    if (pw_len == 0) p.wifi_password[0] = '\0';
    // Region (flood scope)
    size_t reg_len = nvs.getString("act_reg", p.active_region, sizeof(p.active_region));
    if (reg_len == 0) p.active_region[0] = '\0';
    // OTA release channel
    size_t ota_br_len = nvs.getString("ota_br", p.ota_branch, sizeof(p.ota_branch));
    if (ota_br_len == 0) {
        strncpy(p.ota_branch, "main", sizeof(p.ota_branch) - 1);
        p.ota_branch[sizeof(p.ota_branch) - 1] = '\0';
    }
    p.ota_allow_prerelease = nvs.getBool("ota_pre", false);

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
    nvs.putBool("adv_loc", p.advert_location_valid);
    nvs.putInt("adv_lat", p.advert_lat);
    nvs.putInt("adv_lon", p.advert_lon);
    nvs.putFloat("rx_del", p.rx_delay_base);
    nvs.putFloat("tx_del", p.tx_delay_factor);
    nvs.putFloat("dir_tx", p.direct_tx_delay_factor);
    nvs.putBool("rx_boost", p.rx_boosted_gain);
    nvs.putUChar("duty_cyc", p.duty_cycle);
    nvs.putUChar("adv_int", p.advert_interval);
    nvs.putUChar("adv_type", p.advert_type);
    nvs.putUChar("theme", p.theme_id);
    nvs.putBool("multi_ack", p.multi_acks);
    nvs.putBool("buzz_q", p.buzzer_quiet);
    nvs.putBool("gps_en", p.gps_enabled);
    nvs.putUShort("gps_int", p.gps_interval);
    nvs.putUChar("autoadd_cfg", p.autoadd_config);
    nvs.putUChar("autoadd_mh", p.autoadd_max_hops);
    nvs.putUChar("clirep", p.client_repeat);
    nvs.putBool("ble_en", p.ble_enabled);
    nvs.putULong("dev_pin", p.device_pin);
    nvs.putString("wifi_ssid", p.wifi_ssid);
    nvs.putString("wifi_pw", p.wifi_password);
    nvs.putString("act_reg", p.active_region);
    nvs.putString("ota_br", p.ota_branch);
    nvs.putBool("ota_pre", p.ota_allow_prerelease);

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

// ── Repeater password storage ─────────────────────────────────────────
static constexpr const char* PW_NS = "sigurdos_pw";
static constexpr int MAX_SAVED_PWS = 8;

static uint8_t clampPasswordStoreCount(uint8_t count) {
    return (count > MAX_SAVED_PWS) ? MAX_SAVED_PWS : count;
}

static void makePasswordStoreKey(char* out, size_t out_size,
                                 const char* prefix, uint8_t slot) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!prefix || slot >= MAX_SAVED_PWS) return;

    const size_t prefix_len = strlen(prefix);
    if (prefix_len + 2 > out_size) return;

    memcpy(out, prefix, prefix_len);
    out[prefix_len] = (char)('0' + slot);
    out[prefix_len + 1] = '\0';
}

bool saveRepeaterPassword(const char* name, const char* password) {
    if (!name || !name[0] || !password) return false;
    Preferences nvs;
    if (!nvs.begin(PW_NS, false)) return false;

    uint8_t count = clampPasswordStoreCount(nvs.getUChar("count", 0));
    int slot = -1;

    // Find existing entry for this name
    for (uint8_t i = 0; i < count; i++) {
        char key[10];
        makePasswordStoreKey(key, sizeof(key), "name", i);
        char existing[32];
        size_t len = nvs.getString(key, existing, sizeof(existing));
        if (len > 0 && strcmp(existing, name) == 0) {
            slot = i;
            break;
        }
    }

    // Find first free slot
    if (slot < 0 && count < MAX_SAVED_PWS) {
        slot = count;
        count++;
    }

    if (slot < 0) {
        nvs.end();
        return false;
    }

    char nk[10], pk[10];
    makePasswordStoreKey(nk, sizeof(nk), "name", (uint8_t)slot);
    makePasswordStoreKey(pk, sizeof(pk), "pw", (uint8_t)slot);
    nvs.putString(nk, name);
    nvs.putString(pk, password);
    nvs.putUChar("count", count);
    nvs.end();
    return true;
}

bool loadRepeaterPassword(const char* name, char* password, size_t max_len) {
    if (!name || !name[0] || !password || max_len == 0) return false;
    Preferences nvs;
    if (!nvs.begin(PW_NS, true)) return false;

    uint8_t count = clampPasswordStoreCount(nvs.getUChar("count", 0));
    for (uint8_t i = 0; i < count; i++) {
        char key[10];
        makePasswordStoreKey(key, sizeof(key), "name", i);
        char existing[32];
        size_t len = nvs.getString(key, existing, sizeof(existing));
        if (len > 0 && strcmp(existing, name) == 0) {
            char pwkey[10];
            makePasswordStoreKey(pwkey, sizeof(pwkey), "pw", i);
            size_t ret = nvs.getString(pwkey, password, max_len);
            nvs.end();
            password[max_len - 1] = '\0';
            return ret > 0;
        }
    }
    nvs.end();
    return false;
}

void removeRepeaterPassword(const char* name) {
    if (!name || !name[0]) return;
    Preferences nvs;
    if (!nvs.begin(PW_NS, false)) return;

    uint8_t count = clampPasswordStoreCount(nvs.getUChar("count", 0));
    for (uint8_t i = 0; i < count; i++) {
        char key[10];
        makePasswordStoreKey(key, sizeof(key), "name", i);
        char existing[32];
        size_t len = nvs.getString(key, existing, sizeof(existing));
        if (len > 0 && strcmp(existing, name) == 0) {
            char nk[10], pk[10];
            makePasswordStoreKey(nk, sizeof(nk), "name", i);
            makePasswordStoreKey(pk, sizeof(pk), "pw", i);
            nvs.remove(nk);
            nvs.remove(pk);
            // Shift remaining entries down
            for (uint8_t j = i; j + 1 < count; j++) {
                char oldnk[10], oldpk[10], newnk[10], newpk[10];
                makePasswordStoreKey(oldnk, sizeof(oldnk), "name", (uint8_t)(j + 1));
                makePasswordStoreKey(oldpk, sizeof(oldpk), "pw", (uint8_t)(j + 1));
                makePasswordStoreKey(newnk, sizeof(newnk), "name", j);
                makePasswordStoreKey(newpk, sizeof(newpk), "pw", j);
                char tmp_name[32] = {0}, tmp_pw[64] = {0};
                if (nvs.getString(oldnk, tmp_name, sizeof(tmp_name)) > 0) {
                    nvs.putString(newnk, tmp_name);
                    nvs.remove(oldnk);
                }
                if (nvs.getString(oldpk, tmp_pw, sizeof(tmp_pw)) > 0) {
                    nvs.putString(newpk, tmp_pw);
                    nvs.remove(oldpk);
                }
            }
            count = (count > 0) ? count - 1 : 0;
            nvs.putUChar("count", count);
            break;
        }
    }
    nvs.end();
}

} // namespace sigurdos
