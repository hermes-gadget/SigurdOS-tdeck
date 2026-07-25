// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// WiFi OTA firmware update — starts a WiFi access point with a web
// upload page. Uses Arduino Update class (no external OTA library needed).
// Depends on OTA partition table (board_build.partitions = default_16MB.csv).

#pragma once
#include <cstdint>
#include <cstddef>
#include <climits>

namespace sigurdos {
namespace ota {

// Authentication predicate for the OTA upload endpoint. The endpoint is only
// reachable when a device PIN is configured; an upload is accepted only when a
// non-zero device PIN is set and the submitted PIN matches it. A zero/unset PIN
// or an empty submission is treated as unauthenticated (#687).
// Local OTA requires a strong device PIN (at least 6 decimal digits).
// Weaker 4-digit PINs remain usable for UI gate / settings but cannot open
// the firmware-upload attack surface.
inline bool otaDevicePinEligible(uint32_t device_pin) {
    return device_pin >= 100000u;
}

inline bool otaPinAccepts(uint32_t device_pin, const char* entered) {
    if (device_pin == 0 || entered == nullptr || entered[0] == '\0') return false;
    if (!otaDevicePinEligible(device_pin)) return false;
    uint32_t value = 0;
    for (const char* p = entered; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(*p - '0');
        if (value > (UINT32_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    return value == device_pin;
}

static constexpr uint32_t OTA_SESSION_MAX_MS = 10U * 60U * 1000U;
inline bool otaSessionExpired(uint32_t started_at, uint32_t now) {
    return static_cast<uint32_t>(now - started_at) >= OTA_SESSION_MAX_MS;
}

inline bool otaAccessPointInputsValid(const char* ssid,
                                      const char* password) {
    if (!ssid) return false;
    size_t ssid_len = 0;
    while (ssid[ssid_len] && ssid_len <= 32U) ++ssid_len;
    if (ssid_len == 0U || ssid_len > 32U) return false;

    if (!password || password[0] == '\0') return true;
    size_t password_len = 0;
    while (password[password_len] && password_len <= 63U) ++password_len;
    return password_len >= 8U && password_len <= 63U;
}

// WebServer initializes HTTPUpload::totalSize to zero immediately before the
// multipart START callback. Arduino Update rejects a literal zero, so unknown
// multipart sizes must use its documented sentinel instead.
static constexpr size_t OTA_UPDATE_SIZE_UNKNOWN = 0xFFFFFFFFU;
inline size_t otaUpdateBeginSize(size_t multipart_total_size) {
    return multipart_total_size == 0 ? OTA_UPDATE_SIZE_UNKNOWN
                                     : multipart_total_size;
}

struct OtaUploadSessionState {
    bool authenticated = false;
    bool started = false;
    bool completed = false;
    bool failed = false;
    bool epoch_checked = false;
    size_t received = 0;
};

inline bool otaUploadAcceptsChunk(const OtaUploadSessionState& state) {
    return state.authenticated && state.started &&
           !state.completed && !state.failed;
}

inline bool otaUploadCanFinish(const OtaUploadSessionState& state,
                               size_t multipart_total_size) {
    return otaUploadAcceptsChunk(state) && state.epoch_checked && state.received > 0 &&
           state.received == multipart_total_size;
}

// Start WiFi AP + web server for OTA upload.
// ssid: 1-32 chars. password: 8-63 chars (empty = generate a password).
// Returns true if started successfully. Returns false (refuses to start) when
// no device PIN is configured, since the PIN is the upload endpoint's only auth.
bool start(const char* ssid, const char* password = "");

// Main-loop hook. Multipart parsing and flash writes run on a managed worker.
void loop();

// Request the managed worker to stop and release WiFi/server resources.
void stop();

// Returns true if OTA is active.
bool isActive();

// Human-readable reason for the most recent start failure.
const char* getLastError();

// Returns the AP IP address as a string ("192.168.4.1" format).
// Returns empty string if not active.
const char* getIP();

// WPA2 password for an AP-mode session; empty when OTA uses an existing STA.
const char* getAPPassword();

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
static constexpr int SIGURDOS_WIFI_SCAN_RESULTS_PER_POLL = 4;
static constexpr uint32_t SIGURDOS_WIFI_SCAN_SETTLE_MS = 100;

enum class Status : uint8_t {
    Idle,
    Running,
    Complete,
    Busy,
    Error,
    Cancelled,
};

struct StartResult {
    StartResult(Status initial_status = Status::Error, uint32_t initial_token = 0)
        : status(initial_status), token(initial_token) {}

    Status status;
    uint32_t token;
};

struct PollResult {
    PollResult(Status initial_status = Status::Error, int initial_count = 0)
        : status(initial_status), count(initial_count) {}

    Status status;
    int count;
};

inline int limitScanCount(int found, int capacity);

// Token and result-cursor state shared by production and native tests. Tokens
// prevent a delayed LVGL delete event from cancelling a newer screen's scan.
class AsyncScanState {
public:
    uint32_t start() {
        ++generation_;
        if (generation_ == 0) ++generation_;
        status_ = Status::Running;
        found_ = 0;
        next_ = 0;
        return generation_;
    }

    bool matches(uint32_t token) const {
        return token != 0 && token == generation_;
    }

    Status status(uint32_t token) const {
        return matches(token) ? status_ : Status::Cancelled;
    }

    void resultsReady(uint32_t token, int found, int capacity) {
        if (!matches(token) || status_ != Status::Running) return;
        found_ = limitScanCount(found, capacity);
        next_ = 0;
        if (found_ == 0) status_ = Status::Complete;
    }

    bool takeNext(uint32_t token, int& index) {
        if (!matches(token) || status_ != Status::Running || next_ >= found_) {
            return false;
        }
        index = next_++;
        if (next_ >= found_) status_ = Status::Complete;
        return true;
    }

    int count(uint32_t token) const {
        return matches(token) ? next_ : 0;
    }

    bool finish(uint32_t token, Status terminal) {
        if (!matches(token) || status_ != Status::Running) return false;
        status_ = terminal;
        return true;
    }

private:
    uint32_t generation_ = 0;
    Status status_ = Status::Idle;
    int found_ = 0;
    int next_ = 0;
};

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

// Start a non-blocking site survey through the central WiFi coordinator.
// The returned token identifies this exact scan and must be used for polling
// and cancellation.
StartResult begin();

// Copy a bounded batch of completed driver results into out. The count is
// cumulative and results are sorted strongest-first once complete. budget_ms
// bounds driver result extraction in addition to the fixed per-poll cap.
PollResult poll(uint32_t token, APInfo* out, int max_aps,
                uint32_t budget_ms = 3);

// Cancel only the scan identified by token. Safe to call after completion or
// from an LVGL screen delete callback.
void cancel(uint32_t token);

}  // namespace wifi_scan

// ── WiFi STA Client ──────────────────────────────────────
namespace wifi_sta {

enum class Status {
    Idle,
    Connecting,
    Connected,
    Failed
};

static constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;

inline Status advanceConnectingStatus(Status current, bool hardware_connected,
                                      uint32_t elapsed_ms) {
    if (current != Status::Connecting) return current;
    if (hardware_connected) return Status::Connected;
    return elapsed_ms > CONNECT_TIMEOUT_MS ? Status::Failed
                                           : Status::Connecting;
}

// Start connecting to an access point. Returns immediately.
// Progress is serviced by loop(); getStatus() is a side-effect-free snapshot.
// Returns false without changing the radio when a scan or OTA owns it.
bool beginConnect(const char* ssid, const char* password);

// Returns the current connection status without changing hardware state.
Status getStatus();

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
