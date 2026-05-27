#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Runtime radio configuration stored in NVS.
// User MUST explicitly configure before radio transmits —
// incorrect settings may be illegal in certain regions.

#include <cstdint>
#include <cstring>

namespace slopos {

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

    // Sentinel defaults — radio will NOT transmit until user configures
    void set_defaults() {
        strncpy(node_name, "SlopOS T-Deck", sizeof(node_name) - 1);
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
    }
};

// Load/save from NVS (Preferences)
bool prefs_load(NodePrefs& p);
bool prefs_save(const NodePrefs& p);
bool prefs_exists();

// Expose loaded prefs globally (read-only after boot)
const NodePrefs& prefs_get();
void             prefs_set(const NodePrefs& p);

} // namespace slopos
