#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Runtime radio configuration stored in NVS.
// User MUST explicitly configure before radio transmits —
// incorrect settings may be illegal in certain regions.

#include <cstdint>
#include <cstring>
#include <cmath>

namespace sigurdos {

struct NodePrefs {
    char    node_name[32];
    float   freq;           // MHz (e.g. 869.618)
    float   bw;             // kHz (e.g. 62.5)
    uint8_t sf;             // LoRa spreading factor (6-12)
    uint8_t cr;             // coding rate denominator (5=4/5, 6=4/6, etc.)
    int8_t  tx_power_dbm;   // dBm (2-22)
    bool    configured;     // false until user explicitly saves settings
    uint8_t kbd_backlight;      // 0-255, keyboard backlight brightness
    uint8_t kbd_layout;         // KeyboardLayoutId (0=English, 1-11 alternate layouts)
    uint8_t display_brightness; // 0-255, display backlight brightness
    uint16_t auto_off_timeout;  // seconds, auto-off timeout (0=off, default 30)
    uint16_t chat_msg_cap;      // Per-channel in-memory message history cap
    uint8_t  flood_max_hops;    // 0=no limit, otherwise max flood hops for contact auto-add
    uint8_t  advert_loc_policy; // ADVERT_LOC_* policy; nonzero currently shares location
    bool     advert_location_valid; // true when companion app supplied fixed-point lat/lon
    int32_t  advert_lat;        // fixed-point degrees * 1e6, companion advert fallback
    int32_t  advert_lon;        // fixed-point degrees * 1e6, companion advert fallback
    float    rx_delay_base;        // 0-20.0, RX delay base factor for collision avoidance
    float    tx_delay_factor;      // 0-2.0, TX flood retransmit delay multiplier
    float    direct_tx_delay_factor; // 0-2.0, TX direct retransmit delay multiplier
    float    airtime_factor;         // MeshCore airtime budget factor (0 = unlimited)
    bool     rx_boosted_gain;        // enable SX1262 boosted RX sensitivity mode
    uint8_t  duty_cycle;             // 0-100, duty cycle budget percent (0 = disabled)
    uint16_t advert_interval_h;      // 0=disabled, 24/72/168=hours between adverts (one per period)
    uint8_t  advert_type;            // ADV_TYPE_CHAT(1)/REPEATER(2)/ROOM(3)/SENSOR(4)
    bool     gps_enabled;            // GPS polling enabled
    uint32_t gps_interval;           // background fix publication interval (seconds, 0..86400)
    uint8_t  autoadd_config;         // bitmask: bit1=chat, bit2=repeater, bit3=room, bit4=sensor
    uint8_t  autoadd_max_hops;       // 0=no limit, max flood hops for auto-add
    uint8_t  theme_id;                // 0=Default, 1-5 preset themes
    uint8_t  path_hash_mode;          // 0=1-byte, 1=2-byte, 2=3-byte path hash for originated adverts/messages
    uint8_t  multi_acks;              // extra redundant ACK transmissions for lossy links
    bool     buzzer_quiet;            // mute message-arrival buzzer
    uint8_t  client_repeat;           // 0=no forwarding, !=0=opportunistic relay (client-repeat mode)
    bool     ble_enabled;             // BLE companion advertising enabled in BLE build
    bool     ble_user_set;            // true only after an explicit BLE user toggle
    uint32_t device_pin;               // 4-6 digit device PIN (0 = disabled)
    uint32_t ble_pin;                  // random per-device BLE pairing PIN (0 = not generated yet)
    bool     ble_bond_reset_pending;   // block advertising until old BLE bonds are purged
    uint8_t  telemetry_modes;          // bitmask for companion telemetry modes
    uint8_t  manual_add_contacts;      // companion manual-add-contacts mode (0=auto, 1=prompt)
    char     default_scope_key_hex[33];  // hex-encoded 16-byte companion default flood-scope key
    char     wifi_ssid[33];            // WiFi STA SSID for GitHub OTA (empty = not set)
    char     wifi_password[64];        // WiFi STA password
    char     active_region[31];        // active flood scope region name (empty = wildcard/unscoped)
    char     ota_branch[16];            // GitHub OTA release channel: "dev" or "main" (or "latest")
    bool     ota_allow_prerelease;      // include pre-release tags when searching for OTA updates
    char     radio_profile[16];          // radio profile id (e.g. "us_902_928"), or empty/"custom" for manual

    // Sentinel defaults — radio will NOT transmit until user configures
    void set_defaults() {
        strncpy(node_name, "SigurdOS T-Deck", sizeof(node_name) - 1);
        node_name[sizeof(node_name) - 1] = '\0';
        freq = 0.0f;         // 0 = not configured
        bw   = 0.0f;
        sf   = 0;
        cr   = 0;
        tx_power_dbm = 0;
        configured = false;
        kbd_backlight = 127;
        kbd_layout = 0;
        display_brightness = 200;
        auto_off_timeout = 30;
        chat_msg_cap = 200;
        flood_max_hops = 0;  // 0 = no limit
        advert_loc_policy = 0;  // location OFF by default (privacy-first)
        advert_location_valid = false;
        advert_lat = 0;
        advert_lon = 0;
        rx_delay_base = 10.0f;      // default RX delay base (matching MeshCore companion default)
        tx_delay_factor = 1.0f;     // default TX flood delay factor
        direct_tx_delay_factor = 1.0f; // default TX direct delay factor
        airtime_factor = 0.0f;       // default: unlimited airtime policy
        rx_boosted_gain = false;      // default: normal sensitivity mode
        duty_cycle = 0;               // 0 = disabled
        advert_interval_h = 0;        // 0 = disabled
        advert_type = 1;              // 1 = ADV_TYPE_CHAT (default: chat companion)
        gps_enabled = false;          // GPS off by default (privacy, battery)
        gps_interval = 5;             // background position update cadence
        autoadd_config = 0x1E;        // auto-add: chat|repeater|room|sensor (bits 1-4), no overwrite (bit 0)
        autoadd_max_hops = 0;         // 0 = no limit
        theme_id = 0;                 // default theme
        path_hash_mode = 0;           // default: 1-byte path hash (backward compatible with pre-1.14 repeaters)
        multi_acks = 0;               // default: send minimum ACKs
        buzzer_quiet = false;         // default: buzzer enabled
        client_repeat = 0;            // default: no forwarding
        ble_enabled = true;           // default: BLE companion on
        ble_user_set = false;          // default may migrate; no explicit user choice yet
        device_pin = 0;               // default: no PIN
        ble_pin = 0;                  // default: not generated (will generate on first BLE boot)
        ble_bond_reset_pending = false;
        telemetry_modes = 0;          // default: no telemetry sharing
        manual_add_contacts = 0;      // default: auto-add contacts
        default_scope_key_hex[0] = '\0';  // default: no companion flood scope
        wifi_ssid[0] = '\0';          // default: no WiFi
        wifi_password[0] = '\0';
        active_region[0] = '\0';       // default: wildcard (unscoped flood)
        strncpy(ota_branch, "main", sizeof(ota_branch) - 1);
        ota_branch[sizeof(ota_branch) - 1] = '\0';
        ota_allow_prerelease = false;
        radio_profile[0] = '\0';        // empty = not set / custom
    }
};

namespace detail {

constexpr uint8_t BLE_PREFS_SCHEMA_VERSION = 1;

inline uint8_t normalizePathHashMode(uint8_t mode) {
    return mode <= 2 ? mode : 0;
}

inline bool validRadioBandwidth(float bw) {
    static constexpr float allowed[] = {
        7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f,
    };
    if (!std::isfinite(bw)) return false;
    for (float value : allowed) if (std::fabs(bw - value) < 0.02f) return true;
    return false;
}

inline bool normalizeAndValidate(NodePrefs& prefs) {
    if (prefs.gps_interval > 86400) prefs.gps_interval = 86400;
    if (prefs.kbd_layout >= 12) prefs.kbd_layout = 0;
    if (prefs.display_brightness < 20) prefs.display_brightness = 20;
    if (prefs.display_brightness > 240) prefs.display_brightness = 240;
    prefs.path_hash_mode = normalizePathHashMode(prefs.path_hash_mode);
    if (!std::isfinite(prefs.rx_delay_base) || prefs.rx_delay_base < 0.0f ||
        prefs.rx_delay_base > 20.0f) prefs.rx_delay_base = 10.0f;
    if (!std::isfinite(prefs.tx_delay_factor) || prefs.tx_delay_factor < 0.0f ||
        prefs.tx_delay_factor > 2.0f) prefs.tx_delay_factor = 1.0f;
    if (!std::isfinite(prefs.direct_tx_delay_factor) ||
        prefs.direct_tx_delay_factor < 0.0f || prefs.direct_tx_delay_factor > 2.0f) {
        prefs.direct_tx_delay_factor = 1.0f;
    }
    if (!std::isfinite(prefs.airtime_factor) || prefs.airtime_factor < 0.0f) {
        prefs.airtime_factor = 0.0f;
    }
    if (!prefs.configured) {
        prefs.freq = prefs.bw = 0.0f;
        prefs.sf = prefs.cr = 0;
        prefs.tx_power_dbm = 0;
        prefs.radio_profile[0] = '\0';
        return true;
    }
    const bool valid = std::isfinite(prefs.freq) && prefs.freq >= 400.0f &&
        prefs.freq <= 930.0f && validRadioBandwidth(prefs.bw) &&
        prefs.sf >= 6 && prefs.sf <= 12 && prefs.cr >= 5 && prefs.cr <= 8 &&
        prefs.tx_power_dbm >= 2 && prefs.tx_power_dbm <= 22;
    if (!valid) {
        prefs.configured = false;
        prefs.freq = prefs.bw = 0.0f;
        prefs.sf = prefs.cr = 0;
        prefs.tx_power_dbm = 0;
        prefs.radio_profile[0] = '\0';
    }
    return valid;
}

inline bool repeaterPasswordFitsSchema(const char* name, const char* password) {
    if (!name || !password) return false;
    const size_t name_len = std::strlen(name);
    const size_t password_len = std::strlen(password);
    return name_len > 0 && name_len < 32 && password_len > 0 && password_len < 64;
}

enum class BlePrefsWriteMode : uint8_t {
    Preserve,
    Write,
};

struct BlePrefsState {
    bool enabled;
    bool user_set;
    bool needs_migration;
};

// Resolve BLE-capable firmware state without mistaking a legacy build-written
// false for an explicit user choice. A new-schema intent marker always wins.
inline BlePrefsState resolveBlePrefs(bool value_present, bool stored_value,
                                    bool intent_present, bool stored_intent,
                                    uint8_t schema_version) {
    const bool user_set = intent_present && stored_intent;
    const bool legacy = schema_version < BLE_PREFS_SCHEMA_VERSION;
    const bool enabled = legacy && !user_set
        ? true
        : (value_present ? stored_value : true);
    return {enabled, user_set, legacy};
}

} // namespace detail

// Load/save from NVS (Preferences)
bool prefs_load(NodePrefs& p);
bool prefs_save(const NodePrefs& p);
bool prefs_exists();

// Expose loaded prefs globally (read-only after boot)
const NodePrefs& prefs_get();
bool             prefs_set(const NodePrefs& p);

// Persist an explicit user BLE choice. Non-BLE variants return false and
// leave the cross-variant BLE keys untouched.
bool prefs_set_ble_enabled(bool enabled);

// ── Saved repeater passwords (persist across firmware updates in NVS) ──
bool saveRepeaterPassword(const char* name, const char* password);
bool loadRepeaterPassword(const char* name, char* password, size_t max_len);
bool removeRepeaterPassword(const char* name);

} // namespace sigurdos
