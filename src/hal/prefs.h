#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Runtime radio configuration stored in NVS.
// User MUST explicitly configure before radio transmits —
// incorrect settings may be illegal in certain regions.

#include <cstdint>
#include <cstring>

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
    uint8_t display_brightness; // 0-255, display backlight brightness
    uint16_t auto_off_timeout;  // seconds, auto-off timeout (0=off, default 30)
    uint16_t chat_msg_cap;      // Per-channel in-memory message history cap
    uint8_t  flood_max_hops;    // 0=no limit, otherwise max flood hops for contact auto-add
    bool     share_location;    // include GPS coordinates in adverts
    float    rx_delay_base;        // 0-20.0, RX delay base factor for collision avoidance
    float    tx_delay_factor;      // 0-2.0, TX flood retransmit delay multiplier
    float    direct_tx_delay_factor; // 0-2.0, TX direct retransmit delay multiplier
    bool     rx_boosted_gain;        // enable SX1262 boosted RX sensitivity mode
    uint8_t  duty_cycle;             // 0-100, duty cycle budget percent (0 = disabled)
    uint8_t  advert_interval;        // 0=disabled, 1-255=interval in half-minutes (e.g. 10=5min)
    uint8_t  advert_type;            // ADV_TYPE_CHAT(1)/REPEATER(2)/ROOM(3)/SENSOR(4)
    bool     gps_enabled;            // GPS polling enabled
    uint16_t gps_interval;           // GPS read interval in seconds (0 = every loop)
    uint8_t  autoadd_config;         // bitmask: bit1=chat, bit2=repeater, bit3=room, bit4=sensor
    uint8_t  autoadd_max_hops;       // 0=no limit, max flood hops for auto-add
    uint8_t  theme_id;                // 0=Default, 1-5 preset themes
    bool     multi_acks;              // send extra redundant ACK transmissions for lossy links
    bool     buzzer_quiet;            // mute message-arrival buzzer
    uint8_t  client_repeat;           // 0=no forwarding, !=0=opportunistic relay (client-repeat mode)

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
        display_brightness = 200;
        auto_off_timeout = 30;
        chat_msg_cap = 200;
        flood_max_hops = 0;  // 0 = no limit
        share_location = true;
        rx_delay_base = 10.0f;      // default RX delay base (matching MeshCore companion default)
        tx_delay_factor = 1.0f;     // default TX flood delay factor
        direct_tx_delay_factor = 1.0f; // default TX direct delay factor
        rx_boosted_gain = false;      // default: normal sensitivity mode
        duty_cycle = 0;               // 0 = disabled
        advert_interval = 0;          // 0 = disabled
        advert_type = 1;              // 1 = ADV_TYPE_CHAT (default: chat companion)
        gps_enabled = true;           // GPS on by default
        gps_interval = 0;             // 0 = poll every loop
        autoadd_config = 0x1E;        // auto-add: chat|repeater|room|sensor (bits 1-4), no overwrite (bit 0)
        autoadd_max_hops = 0;         // 0 = no limit
        theme_id = 0;                 // default theme
        multi_acks = false;           // default: send minimum ACKs
        buzzer_quiet = false;         // default: buzzer enabled
        client_repeat = 0;            // default: no forwarding
    }
};

// Load/save from NVS (Preferences)
bool prefs_load(NodePrefs& p);
bool prefs_save(const NodePrefs& p);
bool prefs_exists();

// Expose loaded prefs globally (read-only after boot)
const NodePrefs& prefs_get();
void             prefs_set(const NodePrefs& p);

// ── Saved repeater passwords (persist across firmware updates in NVS) ──
bool saveRepeaterPassword(const char* name, const char* password);
bool loadRepeaterPassword(const char* name, char* password, size_t max_len);
void removeRepeaterPassword(const char* name);

} // namespace sigurdos
