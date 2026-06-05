// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// GitHub OTA — STA-mode WiFi, HTTPS download from GitHub releases,
// flash via Arduino Update class.

#include "github_ota.h"
#include "prefs.h"
#include "wifi_ota.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_heap_caps.h>

namespace sigurdos {
namespace github_ota {

// ── Constants ───────────────────────────────────────────────────

static const char* GITHUB_FW_URL =
    "https://github.com/hermes-gadget/SigurdOS-tdeck"
    "/releases/latest/download/firmware.bin";

static const char* USER_AGENT = "SigurdOS-TDeck/1.0";

static constexpr int WIFI_CONNECT_TIMEOUT_MS = 20000;   // 20s
static constexpr int HTTP_TIMEOUT_MS        = 30000;    // 30s
static constexpr size_t DOWNLOAD_CHUNK      = 4096;

// ── State ───────────────────────────────────────────────────────

static bool                   s_active = false;
static GitHubOTAStatus        s_status;
static WiFiClientSecure*      s_client = nullptr;
static HTTPClient*            s_http   = nullptr;
static int                    s_http_code = 0;
static int                    s_content_length = 0;
static int                    s_downloaded = 0;
static unsigned long          s_last_progress = 0;
static unsigned long          s_connect_start = 0;
static bool                   s_cancelled = false;

// ── Helpers ─────────────────────────────────────────────────────

static void setStatus(GitHubOTAState state, int pct = 0,
                      const char* msg = "", const char* err = "") {
    s_status.state = state;
    s_status.progress_pct = pct;
    if (msg) {
        strncpy(s_status.status_msg, msg, sizeof(s_status.status_msg) - 1);
        s_status.status_msg[sizeof(s_status.status_msg) - 1] = '\0';
    }
    if (err) {
        strncpy(s_status.error_msg, err, sizeof(s_status.error_msg) - 1);
        s_status.error_msg[sizeof(s_status.error_msg) - 1] = '\0';
    }
}

static void cleanupConnection() {
    if (s_http) {
        s_http->end();
        delete s_http;
        s_http = nullptr;
    }
    if (s_client) {
        s_client->stop();
        delete s_client;
        s_client = nullptr;
    }
}

static void fail(const char* msg) {
    Serial.printf("[gh-ota] FAIL: %s\n", msg);
    setStatus(GitHubOTAState::Failed, 0, "Failed", msg);
    cleanupConnection();
    // Force a clean disconnect to reset any stale LWIP socket state
    // after HTTP client deletion. Use disconnect() without args to
    // clean LWIP without turning WiFi OFF (wifi_sta manages the mode).
    WiFi.disconnect();
    delay(10);
    s_active = false;
}

// ── Public API ──────────────────────────────────────────────────

bool startGitHubUpdate() {
    if (s_active) return true;

    const NodePrefs& p = prefs_get();
    if (!p.wifi_ssid[0]) {
        Serial.println("[gh-ota] No WiFi SSID configured");
        setStatus(GitHubOTAState::Failed, 0, "Failed",
                  "No WiFi configured. Set SSID in Settings.");
        return false;
    }

    s_active = true;
    s_cancelled = false;
    s_downloaded = 0;
    s_http_code = 0;
    s_content_length = 0;
    s_connect_start = millis();

    setStatus(GitHubOTAState::Connecting, 0, "Connecting to WiFi...");

    // If wifi_sta is already connected, don't call WiFi.begin() again —
    // the brief disconnect glitch would kill the WiFi icon.
    // loop() will see WL_CONNECTED immediately and fast-path through.
    if (!sigurdos::wifi_sta::isConnected()) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(p.wifi_ssid, p.wifi_password);
    } else {
        Serial.printf("[gh-ota] Reusing existing WiFi connection\n");
    }

    Serial.printf("[gh-ota] Connecting to WiFi: %s\n", p.wifi_ssid);
    return true;
}

void loop() {
    if (!s_active) return;
    if (s_cancelled) {
        fail("Cancelled");
        return;
    }

    GitHubOTAState st = s_status.state;

    // ── Phase 1: WiFi connect ───────────────────────────────────
    if (st == GitHubOTAState::Connecting) {
        if (WiFi.status() == WL_CONNECTED) {
            // WiFi ready (either just connected, or was already connected
            // via wifi_sta — we re-used the existing link).
            Serial.printf("[gh-ota] WiFi connected. IP: %s\n",
                          WiFi.localIP().toString().c_str());
            setStatus(GitHubOTAState::Downloading, 0,
                      "Downloading firmware...");

            // Start HTTPS download
            s_client = new WiFiClientSecure();
            s_client->setInsecure();  // skip cert verify (binary verified by Update)

            s_http = new HTTPClient();
            s_http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            s_http->setTimeout(HTTP_TIMEOUT_MS);
            s_http->setUserAgent(USER_AGENT);

            Serial.printf("[gh-ota] GET %s\n", GITHUB_FW_URL);
            if (!s_http->begin(*s_client, GITHUB_FW_URL)) {
                fail("HTTP begin failed");
                return;
            }

            s_http_code = s_http->GET();
            if (s_http_code <= 0) {
                fail("HTTP GET failed — check WiFi + GitHub reachability");
                return;
            }

            Serial.printf("[gh-ota] HTTP %d\n", s_http_code);
            if (s_http_code != HTTP_CODE_OK) {
                char buf[64];
                snprintf(buf, sizeof(buf), "HTTP %d", s_http_code);
                fail(buf);
                return;
            }

            s_content_length = s_http->getSize();
            Serial.printf("[gh-ota] Content-Length: %d\n", s_content_length);

            if (s_content_length <= 0 || s_content_length > 6*1024*1024) {
                fail("Invalid Content-Length");
                return;
            }

            // Prepare OTA partition
            if (!Update.begin((size_t)s_content_length)) {
                Serial.printf("[gh-ota] Update.begin failed: %s\n",
                              Update.errorString());
                fail("Flash init failed");
                return;
            }

            s_downloaded = 0;
            s_last_progress = millis();
        } else if (WiFi.status() == WL_CONNECT_FAILED ||
                   WiFi.status() == WL_NO_SSID_AVAIL) {
            char buf[64];
            snprintf(buf, sizeof(buf), "WiFi connect failed (status=%d)",
                     (int)WiFi.status());
            fail(buf);
            return;
        } else if (millis() - s_connect_start > WIFI_CONNECT_TIMEOUT_MS) {
            fail("WiFi connect timeout");
            return;
        }
        // Still connecting — wait
        return;
    }

    // ── Phase 2: Download + write ───────────────────────────────
    if (st == GitHubOTAState::Downloading || st == GitHubOTAState::Writing) {
        if (!s_http || !s_http->connected()) {
            // Stream finished — finalize
            if (s_downloaded >= s_content_length && s_content_length > 0) {
                if (Update.end(true)) {
                    Serial.printf("[gh-ota] Update OK — %d bytes written\n",
                                  s_downloaded);
                    setStatus(GitHubOTAState::Success, 100,
                              "Update complete — rebooting...");
                    delay(500);
                    ESP.restart();
                } else {
                    Serial.printf("[gh-ota] Update.end failed: %s\n",
                                  Update.errorString());
                    fail("Flash write failed");
                }
            } else {
                if (s_downloaded > 0) {
                    fail("Download truncated");
                } else {
                    fail("Connection lost");
                }
            }
            return;
        }

        // Get the stream
        WiFiClient* stream = s_http->getStreamPtr();
        if (!stream) {
            fail("No stream");
            return;
        }

        // Read available data in chunks
        while (stream->available() && s_downloaded < s_content_length && !s_cancelled) {
            size_t avail = (size_t)stream->available();
            size_t to_read = avail;
            if (to_read > DOWNLOAD_CHUNK) to_read = DOWNLOAD_CHUNK;

            // 4 KB transfer buffer — only needed while an OTA is downloading.
            // Allocate once from PSRAM (DRAM fallback) instead of holding it
            // permanently in internal DRAM as a static array.
            static uint8_t* buf = (uint8_t*)heap_caps_malloc(
                DOWNLOAD_CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buf) buf = (uint8_t*)heap_caps_malloc(
                DOWNLOAD_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!buf) { fail("Out of memory"); return; }
            size_t read = stream->readBytes(buf, to_read);
            if (read == 0) break;

            size_t written = Update.write(buf, read);
            if (written != read) {
                Serial.printf("[gh-ota] Write mismatch: read=%u written=%u\n",
                              (unsigned)read, (unsigned)written);
                fail("Flash write error");
                return;
            }

            s_downloaded += read;

            // Progress
            unsigned long now = millis();
            if (now - s_last_progress > 500) {
                s_last_progress = now;
                int pct = (s_content_length > 0)
                    ? (int)(((long)s_downloaded * 100) / s_content_length)
                    : 0;
                char msg[64];
                snprintf(msg, sizeof(msg), "Downloading... %d%% (%d/%d KB)",
                         pct, s_downloaded / 1024,
                         s_content_length / 1024);
                setStatus(GitHubOTAState::Writing, pct, msg);
                Serial.printf("[gh-ota] %s\n", msg);
            }
        }
    }
}

bool isActive() {
    return s_active;
}

const GitHubOTAStatus& getStatus() {
    return s_status;
}

void cancel() {
    s_cancelled = true;
    Serial.println("[gh-ota] Cancel requested");
}

}  // namespace github_ota
}  // namespace sigurdos
