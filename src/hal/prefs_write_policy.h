#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "prefs.h"

#include <cstddef>
#include <cstdint>

namespace sigurdos::detail {

struct PrefsNvsWriter {
    void* context;
    int32_t (*setI8)(void*, const char*, int8_t);
    int32_t (*setU8)(void*, const char*, uint8_t);
    int32_t (*setU16)(void*, const char*, uint16_t);
    int32_t (*setI32)(void*, const char*, int32_t);
    int32_t (*setU32)(void*, const char*, uint32_t);
    int32_t (*setBlob)(void*, const char*, const void*, size_t);
    int32_t (*setString)(void*, const char*, const char*);
    int32_t (*commit)(void*);
};

struct PrefsWriteFailure {
    const char* key = nullptr;
    int32_t error = 0;
};

// Persisted before factory-reset deletion begins. This small transaction is
// intentionally independent of the active build's BLE transport so a reset
// performed by USB-only firmware still revokes bonds on the next BLE boot.
inline bool prefsWriteFactoryResetInterlock(const PrefsNvsWriter& writer,
                                            PrefsWriteFailure* failure = nullptr)
{
    auto fail = [failure](const char* key, int32_t error) {
        if (failure) {
            failure->key = key;
            failure->error = error;
        }
        return false;
    };
    if (!writer.context || !writer.setU8 || !writer.commit) {
        return fail("writer", -1);
    }
    auto wrote = [&fail](const char* key, int32_t error) {
        return error == 0 ? true : fail(key, error);
    };
    if (!wrote("ble_en", writer.setU8(writer.context, "ble_en", 0))) return false;
    if (!wrote("ble_user", writer.setU8(writer.context, "ble_user", 1))) return false;
    if (!wrote("ble_ver", writer.setU8(
            writer.context, "ble_ver", BLE_PREFS_SCHEMA_VERSION))) return false;
    if (!wrote("ble_bond_rst", writer.setU8(
            writer.context, "ble_bond_rst", 1))) return false;
    return wrote("commit", writer.commit(writer.context));
}

inline bool prefsWriteAll(const NodePrefs& prefs, const PrefsNvsWriter& writer,
    BlePrefsWriteMode ble_mode,
    PrefsWriteFailure* failure = nullptr) {
    auto fail = [failure](const char* key, int32_t error) {
        if (failure) {
            failure->key = key;
            failure->error = error;
        }
        return false;
    };
    if (!writer.context || !writer.setI8 || !writer.setU8 || !writer.setU16 ||
        !writer.setI32 || !writer.setU32 || !writer.setBlob || !writer.setString ||
        !writer.commit) {
        return fail("writer", -1);
    }

    auto wrote = [&fail](const char* key, int32_t error) {
        return error == 0 ? true : fail(key, error);
    };

    if (!wrote("name", writer.setString(writer.context, "name", prefs.node_name))) return false;
    if (!wrote("freq", writer.setBlob(writer.context, "freq", &prefs.freq, sizeof(prefs.freq)))) return false;
    if (!wrote("bw", writer.setBlob(writer.context, "bw", &prefs.bw, sizeof(prefs.bw)))) return false;
    if (!wrote("sf", writer.setU8(writer.context, "sf", prefs.sf))) return false;
    if (!wrote("cr", writer.setU8(writer.context, "cr", prefs.cr))) return false;
    const int8_t tx_power = prefs.tx_power_dbm < -9 ? (int8_t)-9
        : (prefs.tx_power_dbm > 22 ? (int8_t)22 : prefs.tx_power_dbm);
    if (!wrote("txpwr", writer.setI8(writer.context, "txpwr", tx_power))) return false;
    if (!wrote("cfg", writer.setU8(writer.context, "cfg", prefs.configured ? 1 : 0))) return false;
    if (!wrote("kbd_bl", writer.setU8(writer.context, "kbd_bl", prefs.kbd_backlight))) return false;
    if (!wrote("kbd_layout", writer.setU8(writer.context, "kbd_layout", prefs.kbd_layout))) return false;
    if (!wrote("disp_bl", writer.setU8(writer.context, "disp_bl", prefs.display_brightness))) return false;
    if (!wrote("auto_off", writer.setU16(writer.context, "auto_off", prefs.auto_off_timeout))) return false;
    if (!wrote("chat_cap", writer.setU16(writer.context, "chat_cap", prefs.chat_msg_cap))) return false;
    if (!wrote("flood_mh", writer.setU8(writer.context, "flood_mh", prefs.flood_max_hops))) return false;
    if (!wrote("sh_loc", writer.setU8(writer.context, "sh_loc", prefs.advert_loc_policy))) return false;
    if (!wrote("adv_loc", writer.setU8(writer.context, "adv_loc", prefs.advert_location_valid ? 1 : 0))) return false;
    if (!wrote("adv_lat", writer.setI32(writer.context, "adv_lat", prefs.advert_lat))) return false;
    if (!wrote("adv_lon", writer.setI32(writer.context, "adv_lon", prefs.advert_lon))) return false;
    if (!wrote("rx_del", writer.setBlob(writer.context, "rx_del", &prefs.rx_delay_base, sizeof(prefs.rx_delay_base)))) return false;
    if (!wrote("tx_del", writer.setBlob(writer.context, "tx_del", &prefs.tx_delay_factor, sizeof(prefs.tx_delay_factor)))) return false;
    if (!wrote("dir_tx", writer.setBlob(writer.context, "dir_tx", &prefs.direct_tx_delay_factor, sizeof(prefs.direct_tx_delay_factor)))) return false;
    if (!wrote("air_fact", writer.setBlob(writer.context, "air_fact", &prefs.airtime_factor, sizeof(prefs.airtime_factor)))) return false;
    if (!wrote("rx_boost", writer.setU8(writer.context, "rx_boost", prefs.rx_boosted_gain ? 1 : 0))) return false;
    if (!wrote("duty_cyc", writer.setU8(writer.context, "duty_cyc", prefs.duty_cycle))) return false;
    if (!wrote("adv_dur", writer.setU16(writer.context, "adv_dur", prefs.advert_interval_h))) return false;
    if (!wrote("adv_type", writer.setU8(writer.context, "adv_type", prefs.advert_type))) return false;
    if (!wrote("theme", writer.setU8(writer.context, "theme", prefs.theme_id))) return false;
    if (!wrote("phash_mode", writer.setU8(writer.context, "phash_mode", prefs.path_hash_mode))) return false;
    if (!wrote("multi_ack", writer.setU8(writer.context, "multi_ack", prefs.multi_acks))) return false;
    if (!wrote("buzz_q", writer.setU8(writer.context, "buzz_q", prefs.buzzer_quiet ? 1 : 0))) return false;
    if (!wrote("gps_en", writer.setU8(writer.context, "gps_en", prefs.gps_enabled ? 1 : 0))) return false;
    if (!wrote("gps_int32", writer.setU32(writer.context, "gps_int32", prefs.gps_interval))) return false;
    if (!wrote("gps_trk_en", writer.setU8(
            writer.context, "gps_trk_en", prefs.gps_track_enabled ? 1 : 0))) return false;
    if (!wrote("gps_trk_int", writer.setU32(
            writer.context, "gps_trk_int", prefs.gps_track_interval))) return false;
    if (!wrote("autoadd_cfg", writer.setU8(writer.context, "autoadd_cfg", prefs.autoadd_config))) return false;
    if (!wrote("autoadd_mh", writer.setU8(writer.context, "autoadd_mh", prefs.autoadd_max_hops))) return false;
    if (!wrote("clirep", writer.setU8(writer.context, "clirep", prefs.client_repeat))) return false;
    if (ble_mode == BlePrefsWriteMode::Write) {
        if (!wrote("ble_en", writer.setU8(writer.context, "ble_en", prefs.ble_enabled ? 1 : 0))) return false;
        if (!wrote("ble_user", writer.setU8(writer.context, "ble_user", prefs.ble_user_set ? 1 : 0))) return false;
        if (!wrote("ble_ver", writer.setU8(writer.context, "ble_ver", BLE_PREFS_SCHEMA_VERSION))) return false;
    }
    if (!wrote("dev_pin", writer.setU32(writer.context, "dev_pin", prefs.device_pin))) return false;
    if (!wrote("ble_pin", writer.setU32(writer.context, "ble_pin", prefs.ble_pin))) return false;
    if (!wrote("ble_bond_rst", writer.setU8(writer.context, "ble_bond_rst", prefs.ble_bond_reset_pending ? 1 : 0))) return false;
    if (!wrote("tele_mod", writer.setU8(writer.context, "tele_mod", prefs.telemetry_modes))) return false;
    if (!wrote("man_add", writer.setU8(writer.context, "man_add", prefs.manual_add_contacts))) return false;
    if (!wrote("scope_key", writer.setString(writer.context, "scope_key", prefs.default_scope_key_hex))) return false;
    if (!wrote("wifi_ssid", writer.setString(writer.context, "wifi_ssid", prefs.wifi_ssid))) return false;
    if (!wrote("wifi_pw", writer.setString(writer.context, "wifi_pw", prefs.wifi_password))) return false;
    if (!wrote("act_reg", writer.setString(writer.context, "act_reg", prefs.active_region))) return false;
    if (!wrote("ota_br", writer.setString(writer.context, "ota_br", prefs.ota_branch))) return false;
    if (!wrote("ota_pre", writer.setU8(writer.context, "ota_pre", prefs.ota_allow_prerelease ? 1 : 0))) return false;
    if (!wrote("rf_prof", writer.setString(writer.context, "rf_prof", prefs.radio_profile))) return false;
    if (!wrote("commit", writer.commit(writer.context))) return false;
    return true;
}

} // namespace sigurdos::detail
