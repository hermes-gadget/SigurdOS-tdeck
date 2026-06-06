// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// GitHub OTA — STA-mode WiFi, HTTPS download from GitHub releases,
// selectable by branch (dev/main) with pre-release support, flash via
// Arduino Update class.

#include "github_ota.h"
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

// Fallback URL when API is unavailable (latest non-prerelease from any branch)
static const char* GITHUB_FW_FALLBACK_URL =
    "https://github.com/hermes-gadget/SigurdOS-tdeck"
    "/releases/latest/download/firmware.bin";

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

// ── Lightweight JSON parser for release selection ───────────────
//
// Scans a GitHub API releases response looking for the first release
// whose target_commitish matches `branch` and whose prerelease flag
// is allowed per `allow_prerelease`. Writes the matching tag_name to
// `tag_out` (up to `tag_max` chars). Returns true if a match is found.
//
// GitHub API v3 releases format (array of objects):
// [
//   {"tag_name":"v1.0","target_commitish":"main","prerelease":false,...},
//   ...
// ]

static const char* json_skip_ws(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

// Read a JSON string value at *p (must point at opening ").
// Advances p past the closing " and writes the unescaped string content
// to out (up to out_size). Returns true on success.
static bool json_read_string(const char*& p, char* out, size_t out_size) {
    p = json_skip_ws(p);
    if (*p != '"') return false;
    p++; // skip opening "
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++; // skip escape
            if (*p == 'n') out[i++] = '\n';
            else if (*p == 'r') out[i++] = '\r';
            else if (*p == 't') out[i++] = '\t';
            else if (*p == '"') out[i++] = '"';
            else if (*p == '\\') out[i++] = '\\';
            else out[i++] = *p;
            p++;
        } else {
            out[i++] = *p;
            p++;
        }
    }
    out[i] = '\0';
    if (*p == '"') p++; // skip closing "
    return true;
}

// Read a JSON value (string or bare token like true/false/null).
// For strings returns true with out filled. For booleans returns
// true and sets *out_bool if non-null. Otherwise returns false.
static bool json_read_value(const char*& p, char* out_str, size_t out_size,
                            bool* out_bool = nullptr) {
    p = json_skip_ws(p);
    if (*p == '"') {
        return json_read_string(p, out_str, out_size);
    }
    if (strncmp(p, "true", 4) == 0) {
        if (out_bool) *out_bool = true;
        p += 4;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        if (out_bool) *out_bool = false;
        p += 5;
        return true;
    }
    // Skip null and numbers (not needed for our fields)
    if (strncmp(p, "null", 4) == 0) {
        p += 4;
        return true;
    }
    // Skip numbers
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        while (*p && (*p == '-' || *p == '.' || *p == 'e' || *p == 'E' ||
               (*p >= '0' && *p <= '9'))) p++;
        return true;
    }
    return false;
}

// Find a key-value pair in the current JSON object context.
// Scans from *p, looking for "key":value patterns at the current depth.
// Stops at the next sibling '}' at depth curr_depth or end of buffer.
static bool json_find_key(const char*& p, const char* key,
                          char* val_str, size_t val_size,
                          bool* val_bool = nullptr) {
    size_t key_len = strlen(key);
    while (*p) {
        p = json_skip_ws(p);
        if (*p == '}') {
            // End of current object — not found
            return false;
        }
        if (*p == '"') {
            // Peek at key
            const char* key_start = p + 1;
            const char* colon = (const char*)memchr(key_start, '"', 64);
            if (colon && (size_t)(colon - key_start) == key_len &&
                strncmp(key_start, key, key_len) == 0) {
                p = colon + 1; // skip past closing "
                p = json_skip_ws(p);
                if (*p == ':') {
                    p++; // skip colon
                    return json_read_value(p, val_str, val_size, val_bool);
                }
                return false;
            }
            // Skip this key-value pair
            p = colon ? colon + 1 : p + 1;
            p = json_skip_ws(p);
            if (*p == ':') {
                p++;
                // Skip the value
                char dummy[2];
                json_read_value(p, dummy, sizeof(dummy));
            }
        } else {
            // Unexpected char — skip
            p++;
        }
    }
    return false;
}

// Walk to the next top-level object ({ or [). Skips past array/object boundaries.
static const char* json_next_object(const char* p) {
    int depth = 0;
    bool in_string = false;
    while (*p) {
        if (in_string) {
            if (*p == '\\' && *(p+1)) { p += 2; continue; }
            if (*p == '"') in_string = false;
            p++;
            continue;
        }
        if (*p == '"') { in_string = true; p++; continue; }
        if (*p == '{' || *p == '[') { depth++; p++; continue; }
        if (*p == '}' || *p == ']') {
            depth--;
            if (depth <= 0) return p + 1;
            p++;
            continue;
        }
        p++;
    }
    return p;
}

// Find the first release matching our criteria in the API response JSON.
// Returns true and writes the tag_name to tag_out if found.
static bool findMatchingRelease(const char* json, const char* branch,
                                bool allow_prerelease,
                                char* tag_out, size_t tag_max) {
    if (!json || !branch || !tag_out || tag_max == 0) return false;

    const char* p = json;

    // Skip past the opening array bracket if present
    p = json_skip_ws(p);
    if (*p == '[') p++;
    p = json_skip_ws(p);

    while (*p) {
        p = json_skip_ws(p);
        if (*p == '\0' || *p == ']') break; // end of array or string

        if (*p != '{') {
            // Not an object — skip
            if (*p) p++;
            continue;
        }

        // We're at a release object — extract fields
        const char* obj_start = p;
        char tag_name[64] = "";
        char target_commitish[32] = "";
        bool prerelease = false;

        // Walk the object looking for our keys
        const char* scan = obj_start + 1; // skip past '{'
        bool got_tag = false, got_branch = false, got_prerelease = false;

        while (*scan && *scan != '}') {
            scan = json_skip_ws(scan);
            if (*scan == '}') break;

            // Try each key
            char key[32] = "";
            const char* key_save = scan;
            if (!json_read_string(scan, key, sizeof(key))) {
                scan++;
                continue;
            }
            scan = json_skip_ws(scan);
            if (*scan != ':') { scan++; continue; }
            scan++; // skip ':'

            if (strcmp(key, "tag_name") == 0) {
                if (json_read_value(scan, tag_name, sizeof(tag_name))) {
                    got_tag = true;
                }
            } else if (strcmp(key, "target_commitish") == 0) {
                if (json_read_value(scan, target_commitish, sizeof(target_commitish))) {
                    got_branch = true;
                }
            } else if (strcmp(key, "prerelease") == 0) {
                if (json_read_value(scan, nullptr, 0, &prerelease)) {
                    got_prerelease = true;
                }
            } else {
                // Skip this value
                char dummy[2];
                json_read_value(scan, dummy, sizeof(dummy));
            }
        }

        // Check if this release matches
        if (got_tag && got_branch && tag_name[0]) {
            bool branch_matches = (strcmp(target_commitish, branch) == 0);
            bool prerelease_ok = allow_prerelease || !prerelease;

            if (branch_matches && prerelease_ok) {
                strncpy(tag_out, tag_name, tag_max - 1);
                tag_out[tag_max - 1] = '\0';
                Serial.printf("[gh-ota] Found matching release: tag=%s branch=%s prerelease=%d\n",
                              tag_name, target_commitish, prerelease ? 1 : 0);
                return true;
            }
        }

        // Move to next object
        p = obj_start;
        p = json_next_object(p + 1);
    }

    Serial.printf("[gh-ota] No matching release found for branch=%s allow_prerelease=%d\n",
                  branch, allow_prerelease ? 1 : 0);
    return false;
}

// ── Download URL construction ───────────────────────────────────

// Build the download URL from a release tag name.
// Result: https://github.com/hermes-gadget/SigurdOS-tdeck/releases/download/<tag>/firmware.bin
static void buildDownloadUrl(const char* tag, char* out, size_t out_size) {
    snprintf(out, out_size,
             "https://github.com/hermes-gadget/SigurdOS-tdeck"
             "/releases/download/%s/firmware.bin",
             tag);
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
            bool needs_api = true;

            // If branch is "latest" or empty, skip API and use fallback URL directly
            if (p.ota_branch[0] == '\0' || strcmp(p.ota_branch, "latest") == 0) {
                needs_api = false;
            }

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
                    if (findMatchingRelease(s_api_buf, p.ota_branch,
                                            p.ota_allow_prerelease,
                                            tag_name, sizeof(tag_name))) {
                        buildDownloadUrl(tag_name, s_download_url,
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
                strncpy(s_download_url, GITHUB_FW_FALLBACK_URL,
                        sizeof(s_download_url) - 1);
                s_download_url[sizeof(s_download_url) - 1] = '\0';
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
