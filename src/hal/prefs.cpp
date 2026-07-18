// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "prefs.h"
#include "gps_demand.h"
#include "prefs_write_policy.h"
#include "mesh/airtime_policy.h"
#include "diagnostics/log.h"
#include <Preferences.h>
#include <nvs.h>
#include <cmath>

namespace sigurdos {

static constexpr const char* NVS_NS = "sigurdos";
static NodePrefs g_prefs;

namespace {

struct NvsWriterContext {
    nvs_handle_t handle;
};

int32_t setI8(void* raw, const char* key, int8_t value) {
    return nvs_set_i8(static_cast<NvsWriterContext*>(raw)->handle, key, value);
}

int32_t setU8(void* raw, const char* key, uint8_t value) {
    return nvs_set_u8(static_cast<NvsWriterContext*>(raw)->handle, key, value);
}

int32_t setU16(void* raw, const char* key, uint16_t value) {
    return nvs_set_u16(static_cast<NvsWriterContext*>(raw)->handle, key, value);
}

int32_t setI32(void* raw, const char* key, int32_t value) {
    return nvs_set_i32(static_cast<NvsWriterContext*>(raw)->handle, key, value);
}

int32_t setU32(void* raw, const char* key, uint32_t value) {
    return nvs_set_u32(static_cast<NvsWriterContext*>(raw)->handle, key, value);
}

int32_t setBlob(void* raw, const char* key, const void* value, size_t size) {
    return nvs_set_blob(static_cast<NvsWriterContext*>(raw)->handle, key, value, size);
}

int32_t setString(void* raw, const char* key, const char* value) {
    return nvs_set_str(static_cast<NvsWriterContext*>(raw)->handle, key, value);
}

int32_t commit(void* raw) {
    return nvs_commit(static_cast<NvsWriterContext*>(raw)->handle);
}

#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
bool persistBleMigration(bool enabled, bool user_set) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (error == ESP_OK) error = nvs_set_u8(handle, "ble_en", enabled ? 1 : 0);
    if (error == ESP_OK) error = nvs_set_u8(handle, "ble_user", user_set ? 1 : 0);
    if (error == ESP_OK) {
        error = nvs_set_u8(handle, "ble_ver", detail::BLE_PREFS_SCHEMA_VERSION);
    }
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    if (error != ESP_OK) {
        SIG_LOGW("[prefs] BLE preference migration failed: %s", esp_err_to_name(error));
    }
    return error == ESP_OK;
}
#endif

} // namespace

#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
static constexpr bool DEFAULT_BLE_ENABLED = true;
#else
static constexpr bool DEFAULT_BLE_ENABLED = false;
#endif

bool prefs_load(NodePrefs& p) {
    Preferences nvs;
    if (!nvs.begin(NVS_NS, true)) return false;

#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    const detail::BlePrefsState ble_state = detail::resolveBlePrefs(
        nvs.isKey("ble_en"), nvs.getBool("ble_en", DEFAULT_BLE_ENABLED),
        nvs.isKey("ble_user"), nvs.getBool("ble_user", false),
        nvs.getUChar("ble_ver", 0));
#endif

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
    p.kbd_layout = nvs.getUChar("kbd_layout", 0);
    if (p.kbd_layout >= 12) p.kbd_layout = 0;
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
    // E-01: migrate the former duty-cycle-only representation once, while
    // preserving the upstream companion airtime-factor semantic thereafter.
    p.airtime_factor = nvs.isKey("air_fact")
        ? nvs.getFloat("air_fact", 0.0f)
        : mesh::airtime_policy::dutyCyclePercentToFactor(
              nvs.getUChar("duty_cyc", 0));
    if (!std::isfinite(p.airtime_factor) || p.airtime_factor < 0.0f) {
        p.airtime_factor = 0.0f;
    }
    p.rx_boosted_gain = nvs.getBool("rx_boost", false);
    p.duty_cycle = nvs.getUChar("duty_cyc", 0);
    p.advert_interval_h = nvs.getUShort("adv_dur", 0);
    p.advert_type = nvs.getUChar("adv_type", 1);
    p.theme_id = nvs.getUChar("theme", 0);
    p.path_hash_mode = nvs.getUChar("phash_mode", 0);
    if (p.path_hash_mode > 2) p.path_hash_mode = 0;  // clamp (mode 3 reserved)
    p.multi_acks = nvs.getUChar("multi_ack", 0);
    p.buzzer_quiet = nvs.getBool("buzz_q", false);
    p.gps_enabled = nvs.getBool("gps_en", false);
    p.gps_interval = nvs.getUShort("gps_int", 5);
    if (p.gps_interval < 5) p.gps_interval = 5; // migrate legacy every-loop/1s values
    p.autoadd_config = nvs.getUChar("autoadd_cfg", 0x1E);
    p.autoadd_max_hops = nvs.getUChar("autoadd_mh", 0);
    p.client_repeat = nvs.getUChar("clirep", 0);
    // BLE variants migrate stale build-generated false values to the new
    // discoverable default. Non-BLE variants may observe the keys but never
    // rewrite them during unrelated preference saves.
#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    p.ble_enabled = ble_state.enabled;
    p.ble_user_set = ble_state.user_set;
#else
    p.ble_enabled = nvs.getBool("ble_en", DEFAULT_BLE_ENABLED);
    p.ble_user_set = nvs.getBool("ble_user", false);
#endif
    p.device_pin = nvs.getULong("dev_pin", 0);
    p.ble_pin = nvs.getULong("ble_pin", 0);
    p.telemetry_modes = nvs.getUChar("tele_mod", 0);
    p.manual_add_contacts = nvs.getUChar("man_add", 0);
    // default scope key (hex-encoded)
    size_t dsk_len = nvs.getString("scope_key", p.default_scope_key_hex, sizeof(p.default_scope_key_hex));
    if (dsk_len == 0 || dsk_len > sizeof(p.default_scope_key_hex)) { p.default_scope_key_hex[0] = '\0'; }
    else { p.default_scope_key_hex[sizeof(p.default_scope_key_hex) - 1] = '\0'; }
    // WiFi credentials (GitHub OTA) — use getString guard matching node_name pattern
    size_t ssid_len = nvs.getString("wifi_ssid", p.wifi_ssid, sizeof(p.wifi_ssid));
    if (ssid_len == 0 || ssid_len > sizeof(p.wifi_ssid)) { p.wifi_ssid[0] = '\0'; }
    else { p.wifi_ssid[sizeof(p.wifi_ssid) - 1] = '\0'; }
    size_t pw_len = nvs.getString("wifi_pw", p.wifi_password, sizeof(p.wifi_password));
    if (pw_len == 0 || pw_len > sizeof(p.wifi_password)) { p.wifi_password[0] = '\0'; }
    else { p.wifi_password[sizeof(p.wifi_password) - 1] = '\0'; }
    // Region (flood scope)
    size_t reg_len = nvs.getString("act_reg", p.active_region, sizeof(p.active_region));
    if (reg_len == 0 || reg_len > sizeof(p.active_region)) { p.active_region[0] = '\0'; }
    else { p.active_region[sizeof(p.active_region) - 1] = '\0'; }
    // OTA release channel
    size_t ota_br_len = nvs.getString("ota_br", p.ota_branch, sizeof(p.ota_branch));
    if (ota_br_len == 0 || ota_br_len > sizeof(p.ota_branch)) {
        strncpy(p.ota_branch, "main", sizeof(p.ota_branch) - 1);
        p.ota_branch[sizeof(p.ota_branch) - 1] = '\0';
    } else {
        p.ota_branch[sizeof(p.ota_branch) - 1] = '\0';
    }
    p.ota_allow_prerelease = nvs.getBool("ota_pre", false);

    // Radio profile (backward-compatible: empty = custom/unset)
    size_t rf_prof_len = nvs.getString("rf_prof", p.radio_profile, sizeof(p.radio_profile));
    if (rf_prof_len == 0 || rf_prof_len > sizeof(p.radio_profile)) { p.radio_profile[0] = '\0'; }
    else { p.radio_profile[sizeof(p.radio_profile) - 1] = '\0'; }

    nvs.end();

#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    if (ble_state.needs_migration) {
        (void)persistBleMigration(ble_state.enabled, ble_state.user_set);
    }
#endif
    return true;
}

bool prefs_save(const NodePrefs& p) {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        SIG_LOGE("[prefs] nvs_open failed: %s", esp_err_to_name(error));
        return false;
    }

    NvsWriterContext context{handle};
    const detail::PrefsNvsWriter writer{
        &context, setI8, setU8, setU16, setI32, setU32, setBlob, setString, commit
    };
    detail::PrefsWriteFailure failure;
#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    constexpr detail::BlePrefsWriteMode ble_mode = detail::BlePrefsWriteMode::Write;
#else
    constexpr detail::BlePrefsWriteMode ble_mode = detail::BlePrefsWriteMode::Preserve;
#endif
    const bool saved = detail::prefsWriteAll(p, writer, ble_mode, &failure);
    nvs_close(handle);
    if (!saved) {
        SIG_LOGE("[prefs] NVS write failed at %s: %s", failure.key,
                 esp_err_to_name((esp_err_t)failure.error));
    }
    return saved;
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

bool prefs_set(const NodePrefs& p) {
    NodePrefs candidate = p;
    candidate.gps_interval = sigurdos_gps_normalize_interval(candidate.gps_interval);
    if (!prefs_save(candidate)) return false;
    g_prefs = candidate;
    return true;
}

bool prefs_set_ble_enabled(bool enabled) {
#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    NodePrefs candidate = prefs_get();
    candidate.ble_enabled = enabled;
    candidate.ble_user_set = true;
    if (!prefs_save(candidate)) return false;
    g_prefs = candidate;
    return true;
#else
    (void)enabled;
    return false;
#endif
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
        char existing[32] = {0};
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
        char existing[32] = {0};
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
        char existing[32] = {0};
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
