// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// GitHub OTA — STA-mode WiFi, HTTPS download from GitHub releases,
// selectable by branch (dev/main) with pre-release support, flash via
// Arduino Update class.

#include "github_ota.h"
#include "github_ota_plan.h"
#include "prefs.h"
#include "wifi_ota.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cstdlib>

namespace sigurdos {
namespace github_ota {

// ── Constants ───────────────────────────────────────────────────

static const char* GITHUB_API_RELEASES =
    "https://api.github.com/repos/hermes-gadget/SigurdOS-tdeck/releases?per_page=10";

static const char* USER_AGENT = "SigurdOS-TDeck/1.0";

static constexpr int WIFI_CONNECT_TIMEOUT_MS = 20000;   // 20s
static constexpr int HTTP_TIMEOUT_MS        = 30000;    // 30s
static constexpr size_t DOWNLOAD_CHUNK      = 4096;
static constexpr size_t API_RESPONSE_MAX    = 32768;    // 32 KB for release list JSON

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

// Current download URL (constructed from API or fallback)
static char                   s_download_url[256] = "";

// API response buffer (heap-allocated during FetchingRelease)
static char*                  s_api_buf = nullptr;
static int                    s_api_buf_len = 0;

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
    // Free API buffer if allocated
    if (s_api_buf) {
        free(s_api_buf);
        s_api_buf = nullptr;
        s_api_buf_len = 0;
    }
    WiFi.disconnect();
    delay(10);
    s_active = false;
}

// Release selection and URL construction live in github_ota_plan.cpp.

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
    s_download_url[0] = '\0';

    Serial.printf("[gh-ota] Starting update: branch=%s allow_prerelease=%d\n",
                  p.ota_branch, p.ota_allow_prerelease ? 1 : 0);

    setStatus(GitHubOTAState::Connecting, 0, "Connecting to WiFi...");

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
            Serial.printf("[gh-ota] WiFi connected. IP: %s\n",
                          WiFi.localIP().toString().c_str());

            // Check if we need to fetch release info from API
            const NodePrefs& p = prefs_get();
            bool needs_api = branchNeedsReleaseApi(p.ota_branch);

            if (needs_api) {
                setStatus(GitHubOTAState::FetchingRelease, 0,
                          "Fetching release info...");
                s_connect_start = millis();

                // Allocate API response buffer from PSRAM
                s_api_buf = (char*)heap_caps_malloc(API_RESPONSE_MAX,
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!s_api_buf) {
                    s_api_buf = (char*)malloc(API_RESPONSE_MAX);
                }
                if (!s_api_buf) {
                    fail("Out of memory for API request");
                    return;
                }
                s_api_buf_len = 0;
                s_api_buf[0] = '\0';

                // Start API request
                s_client = new WiFiClientSecure();
                s_client->setInsecure();

                s_http = new HTTPClient();
                s_http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
                s_http->setTimeout(HTTP_TIMEOUT_MS);
                s_http->setUserAgent(USER_AGENT);

                Serial.printf("[gh-ota] GET %s\n", GITHUB_API_RELEASES);
                if (!s_http->begin(*s_client, GITHUB_API_RELEASES)) {
                    fail("HTTP begin failed for API");
                    return;
                }

                s_http_code = s_http->GET();
                if (s_http_code <= 0) {
                    Serial.printf("[gh-ota] API GET failed (HTTP %d), using fallback URL\n",
                                  s_http_code);
                    // Fall through to fallback URL
                    needs_api = false;
                } else if (s_http_code != HTTP_CODE_OK) {
                    Serial.printf("[gh-ota] API returned HTTP %d, using fallback URL\n",
                                  s_http_code);
                    needs_api = false;
                } else {
                    // Read response body
                    WiFiClient* stream = s_http->getStreamPtr();
                    int total_read = 0;
                    while (stream->available() && total_read < (int)API_RESPONSE_MAX - 1) {
                        int r = stream->readBytes(
                            s_api_buf + total_read,
                            API_RESPONSE_MAX - 1 - total_read);
                        if (r <= 0) break;
                        total_read += r;
                    }
                    s_api_buf[total_read] = '\0';
                    s_api_buf_len = total_read;

                    Serial.printf("[gh-ota] API response: %d bytes\n", total_read);

                    // Parse to find matching release
                    char tag_name[64] = "";
                    if (selectReleaseTagFromJson(s_api_buf, p.ota_branch,
                                                 p.ota_allow_prerelease,
                                                 tag_name, sizeof(tag_name))) {
                        buildReleaseDownloadUrl(tag_name, s_download_url,
                                                sizeof(s_download_url));
                        Serial.printf("[gh-ota] Download URL: %s\n", s_download_url);
                    } else {
                        Serial.printf("[gh-ota] No matching release found, using fallback\n");
                        needs_api = false;
                    }
                }

                // Clean up API HTTP resources
                cleanupConnection();

                // Free API buffer
                if (s_api_buf) {
                    free(s_api_buf);
                    s_api_buf = nullptr;
                    s_api_buf_len = 0;
                }
            }

            if (!needs_api && s_download_url[0] == '\0') {
                // Use fallback URL (latest non-prerelease)
                copyFallbackDownloadUrl(s_download_url, sizeof(s_download_url));
                Serial.printf("[gh-ota] Using fallback URL: %s\n", s_download_url);
            }

            // Start download phase
            if (s_download_url[0] == '\0') {
                fail("No download URL available");
                return;
            }

            setStatus(GitHubOTAState::Downloading, 0,
                      "Downloading firmware...");

            s_client = new WiFiClientSecure();
            s_client->setInsecure();

            s_http = new HTTPClient();
            s_http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            s_http->setTimeout(HTTP_TIMEOUT_MS);
            s_http->setUserAgent(USER_AGENT);

            Serial.printf("[gh-ota] Downloading from: %s\n", s_download_url);
            if (!s_http->begin(*s_client, s_download_url)) {
                fail("HTTP begin failed for download");
                return;
            }

            s_http_code = s_http->GET();
            if (s_http_code <= 0) {
                fail("HTTP GET failed for download");
                return;
            }

            Serial.printf("[gh-ota] Download HTTP %d\n", s_http_code);
            if (s_http_code != HTTP_CODE_OK && s_http_code != HTTP_CODE_PARTIAL_CONTENT) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Download HTTP %d", s_http_code);
                fail(buf);
                return;
            }

            s_content_length = s_http->getSize();
            Serial.printf("[gh-ota] Content-Length: %d\n", s_content_length);

            if (s_content_length <= 0 || s_content_length > 6*1024*1024) {
                fail("Invalid Content-Length");
                return;
            }

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

    // ── Phase 2: Fetch release info from API ────────────────────
    // (This is now handled inline in the Connecting phase above, after
    // WiFi connects. The FetchingRelease state is a transitional state
    // shown in the UI only. We shouldn't normally get here in the loop,
    // but if we do (e.g. after a deferred yield), just wait.)

    // ── Phase 3: Download + write ───────────────────────────────
    if (st == GitHubOTAState::Downloading || st == GitHubOTAState::Writing) {
        if (!s_http || !s_http->connected()) {
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

        WiFiClient* stream = s_http->getStreamPtr();
        if (!stream) {
            fail("No stream");
            return;
        }

        while (stream->available() && s_downloaded < s_content_length && !s_cancelled) {
            size_t avail = (size_t)stream->available();
            size_t to_read = avail;
            if (to_read > DOWNLOAD_CHUNK) to_read = DOWNLOAD_CHUNK;

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

const char* getDownloadLabel() {
    const NodePrefs& p = prefs_get();
    if (p.ota_branch[0] == '\0' || strcmp(p.ota_branch, "latest") == 0) {
        return "GitHub: latest release";
    }
    // Return a static string showing the channel
    static char label[40];
    snprintf(label, sizeof(label), "GitHub: %s%s",
             p.ota_branch,
             p.ota_allow_prerelease ? " (+pre)" : "");
    return label;
}

}  // namespace github_ota
}  // namespace sigurdos
