// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// WiFi OTA firmware update — starts a WiFi access point with a web
// upload page. Uses Arduino Update class (no external OTA library needed).
// Depends on OTA partition table (board_build.partitions = default_16MB.csv).

#pragma once
#include <cstdint>

namespace sigurdos {
namespace ota {

// Start WiFi AP + web server for OTA upload.
// ssid: max 32 chars (truncated), password: max 63 (empty = open AP).
// Returns true if started successfully.
bool start(const char* ssid, const char* password = "");

// Call in main loop to handle web server requests.
void loop();

// Stop WiFi and web server, free resources.
void stop();

// Returns true if OTA is active.
bool isActive();

// Returns the AP IP address as a string ("192.168.4.1" format).
// Returns empty string if not active.
const char* getIP();

}  // namespace ota

// ── WiFi Site Survey ─────────────────────────────────────
namespace wifi_scan {

struct APInfo {
    char ssid[33];
    int  rssi;
    int  channel;
    bool encrypted;
};

static constexpr int SIGURDOS_WIFI_SCAN_MAX_APS = 30;

inline int limitScanCount(int found, int capacity) {
    if (found <= 0 || capacity <= 0) return 0;

    int count = found;
    if (count > SIGURDOS_WIFI_SCAN_MAX_APS) count = SIGURDOS_WIFI_SCAN_MAX_APS;
    if (count > capacity) count = capacity;
    return count;
}

inline void sortByRssi(APInfo* aps, int count) {
    if (!aps || count <= 1) return;

    for (int i = 1; i < count; ++i) {
        APInfo current = aps[i];
        int j = i - 1;
        while (j >= 0 && aps[j].rssi < current.rssi) {
            aps[j + 1] = aps[j];
            --j;
        }
        aps[j + 1] = current;
    }
}

// Scan nearby WiFi access points. Returns count of found APs
// (max 30). Caller provides buffer; results sorted by RSSI
// (strongest first). WiFi is left in STA mode after scan so
// beginConnect() can reuse the settled radio path.
int scan(APInfo* out, int max_aps);

}  // namespace wifi_scan

// ── WiFi STA Client ──────────────────────────────────────
namespace wifi_sta {

enum class Status {
    Idle,
    Connecting,
    Connected,
    Failed
};

// Start connecting to an access point. Returns immediately.
// Poll with getStatus() to check progress.
void beginConnect(const char* ssid, const char* password);

// Returns current connection status.
Status getStatus();

// Connect to an access point. Blocks up to 15s for connection.
// Returns true if connected (WL_CONNECTED).
// Prefer beginConnect() + timer polling for UI responsiveness.
[[deprecated("Blocks up to 15 s — use beginConnect()/getStatus() instead")]]
bool connect(const char* ssid, const char* password);

// Disconnect and turn WiFi off.
void disconnect();

// Returns true if currently connected.
bool isConnected();

// Returns RSSI in dBm, or 0 if not connected.
int getRSSI();

// Call periodically from main loop to maintain connection.
void loop();

}  // namespace wifi_sta
}  // namespace sigurdos
