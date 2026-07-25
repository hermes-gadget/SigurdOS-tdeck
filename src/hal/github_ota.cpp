// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// GitHub OTA — STA-mode WiFi, HTTPS download from GitHub releases,
// selectable by branch (dev/main) with pre-release support, flash via
// Arduino Update class.

#include "github_ota.h"
#include "github_ota_plan.h"
#include "launcher_env.h"
#include "ota_allocation_policy.h"
#include "ota_runtime_policy.h"
#include "ota_security_epoch.h"
#include "ota_write_policy.h"
#include "prefs.h"
#include "wifi_coordinator.h"
#include "wifi_ota.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <new>

namespace sigurdos {
namespace github_ota {

// ── Constants ───────────────────────────────────────────────────

static const char* GITHUB_API_RELEASES =
    "https://api.github.com/repos/hermes-gadget/SigurdOS-tdeck/releases?per_page=10";

static const char* USER_AGENT = "SigurdOS-TDeck/1.0";

// Constrained rotation set: GitHub's current Sectigo chain plus DigiCert Global
// Root G2 as an independently operated transition anchor. This is transport
// authentication; the image security epoch below is a separate boundary.
static const char* GITHUB_ROOT_CA =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDRjCCAsugAwIBAgIQGp6v7G3o4ZtcGTFBto2Q3TAKBggqhkjOPQQDAzCBiDEL\n"
    "MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\n"
    "eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\n"
    "JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMjEwMzIy\n"
    "MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjBfMQswCQYDVQQGEwJHQjEYMBYGA1UEChMP\n"
    "U2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBTZXJ2ZXIg\n"
    "QXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAR2\n"
    "+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccCWvkEN/U0\n"
    "NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+6xnOQ6Oj\n"
    "ggEgMIIBHDAfBgNVHSMEGDAWgBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAdBgNVHQ4E\n"
    "FgQU0SLaTFnxS18mOKqd1u7rDcP7qWEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB\n"
    "/wQFMAMBAf8wHQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMBEGA1UdIAQK\n"
    "MAgwBgYEVR0gADBQBgNVHR8ESTBHMEWgQ6BBhj9odHRwOi8vY3JsLnVzZXJ0cnVz\n"
    "dC5jb20vVVNFUlRydXN0RUNDQ2VydGlmaWNhdGlvbkF1dGhvcml0eS5jcmwwNQYI\n"
    "KwYBBQUHAQEEKTAnMCUGCCsGAQUFBzABhhlodHRwOi8vb2NzcC51c2VydHJ1c3Qu\n"
    "Y29tMAoGCCqGSM49BAMDA2kAMGYCMQCMCyBit99vX2ba6xEkDe+YO7vC0twjbkv9\n"
    "PKpqGGuZ61JZryjFsp+DFpEclCVy4noCMQCwvZDXD/m2Ko1HA5Bkmz7YQOFAiNDD\n"
    "49IWa2wdT7R3DtODaSXH/BiXv8fwB9su4tU=\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
    "MrY=\n"
    "-----END CERTIFICATE-----\n";

static constexpr int WIFI_CONNECT_TIMEOUT_MS = 20000;   // 20s
static constexpr int HTTP_TIMEOUT_MS        = 30000;    // 30s
static constexpr size_t DOWNLOAD_CHUNK      = 4096;
static constexpr size_t API_RESPONSE_MAX    = 32768;    // 32 KB for release list JSON

// ── State ───────────────────────────────────────────────────────

static std::atomic<bool>      s_active{false};
static GitHubOTAStatus        s_status;
static GitHubOTAStatus        s_status_snapshot;
static portMUX_TYPE           s_status_mux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool>      s_reboot_pending{false};
static WiFiClientSecure*      s_client = nullptr;
static HTTPClient*            s_http   = nullptr;
static int                    s_http_code = 0;
static int                    s_content_length = 0;
static int                    s_downloaded = 0;
static bool                   s_epoch_checked = false;
static unsigned long          s_last_progress = 0;
static unsigned long          s_last_data = 0;
static unsigned long          s_connect_start = 0;
static std::atomic<bool>      s_cancelled{false};


static bool verifyPendingOtaImage() {
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part) return false;
    esp_image_metadata_t meta{};
    const esp_partition_pos_t pos = {.offset = part->address, .size = part->size};
    const esp_err_t err = esp_image_verify(ESP_IMAGE_VERIFY, &pos, &meta);
    if (err != ESP_OK) {
        Serial.printf("[gh-ota] pending image verify failed: %s\n",
                      esp_err_to_name(err));
        return false;
    }
    return true;
}

static uint32_t currentSecurityEpoch() {
    uint32_t epoch = SIGURDOS_SECURITY_EPOCH;
    const esp_app_desc_t* running = esp_ota_get_app_description();
    if (running && running->secure_version > epoch) epoch = running->secure_version;
    return epoch;
}

// Current download URL (constructed from API or fallback)
static char                   s_download_url[256] = "";
static char                   s_resolved_tag[64] = "";
static char                   s_resolved_target[16] = "";

// API response buffer (heap-allocated during FetchingRelease)
static char*                  s_api_buf = nullptr;
static int                    s_api_buf_len = 0;

static void* createOtaConnectionObject(void*, hal::OtaAllocationKind kind) {
    switch (kind) {
    case hal::OtaAllocationKind::ApiSecureClient:
    case hal::OtaAllocationKind::DownloadSecureClient:
        return new (std::nothrow) WiFiClientSecure();
    case hal::OtaAllocationKind::ApiHttpClient:
    case hal::OtaAllocationKind::DownloadHttpClient:
        return new (std::nothrow) HTTPClient();
    default:
        return nullptr;
    }
}

static void destroyOtaConnectionObject(void*, hal::OtaAllocationKind kind,
                                       void* object) {
    switch (kind) {
    case hal::OtaAllocationKind::ApiSecureClient:
    case hal::OtaAllocationKind::DownloadSecureClient:
        delete static_cast<WiFiClientSecure*>(object);
        break;
    case hal::OtaAllocationKind::ApiHttpClient:
    case hal::OtaAllocationKind::DownloadHttpClient:
        delete static_cast<HTTPClient*>(object);
        break;
    default:
        break;
    }
}

static const hal::OtaAllocationOps OTA_CONNECTION_ALLOCATOR{
    nullptr, createOtaConnectionObject, destroyOtaConnectionObject
};

// ── Helpers ─────────────────────────────────────────────────────

static void serviceWorker();

static void setStatus(GitHubOTAState state, int pct = 0,
                      const char* msg = "", const char* err = "") {
    portENTER_CRITICAL(&s_status_mux);
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
    portEXIT_CRITICAL(&s_status_mux);
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

static void cleanupTransfer(bool abort_flash) {
    if (abort_flash && Update.isRunning()) Update.abort();
    cleanupConnection();
    if (s_api_buf) {
        free(s_api_buf);
        s_api_buf = nullptr;
    }
    s_api_buf_len = 0;
}

static void fail(const char* msg) {
    Serial.printf("[gh-ota] FAIL: %s\n", msg);
    setStatus(GitHubOTAState::Failed, 0, "Failed", msg);
    cleanupTransfer(true);
    wifi::release(wifi::Owner::GitHubOta);
    s_active.store(false, std::memory_order_release);
}

static ApiBodyStatus readApiResponse(WiFiClient* stream, int content_length,
                                     char* out, size_t out_size,
                                     int* bytes_read) {
    if (bytes_read) *bytes_read = 0;
    if (!stream || !out || out_size < 2) return ApiBodyStatus::Incomplete;

    const size_t payload_capacity = out_size - 1;
    ApiBodyStatus initial = classifyApiResponseBody(
        content_length, 0, payload_capacity, false);
    if (initial == ApiBodyStatus::TooLarge) return initial;

    size_t total = 0;
    unsigned long last_data_at = millis();
    while (total < payload_capacity) {
        const int available = stream->available();
        if (available > 0) {
            size_t to_read = static_cast<size_t>(available);
            const size_t remaining = payload_capacity - total;
            if (to_read > remaining) to_read = remaining;
            if (content_length >= 0) {
                const size_t expected_remaining =
                    static_cast<size_t>(content_length) - total;
                if (to_read > expected_remaining) to_read = expected_remaining;
            }
            if (to_read == 0) break;

            const int read = stream->readBytes(out + total, to_read);
            if (read <= 0) break;
            total += static_cast<size_t>(read);
            last_data_at = millis();
            if (content_length >= 0 &&
                total == static_cast<size_t>(content_length)) {
                break;
            }
            continue;
        }

        if (!stream->connected()) break;
        if (millis() - last_data_at >= HTTP_TIMEOUT_MS) break;
        delay(1);
    }

    out[total] = '\0';
    if (bytes_read) *bytes_read = static_cast<int>(total);
    return classifyApiResponseBody(content_length, total, payload_capacity,
                                   !stream->connected());
}

// Release selection and URL construction live in github_ota_plan.cpp.

// ── Public API ──────────────────────────────────────────────────

bool startGitHubUpdate() {
    if (s_active.load(std::memory_order_acquire)) return true;

    if (sigurdos_is_under_launcher()) {
        Serial.println("[gh-ota] REFUSED: GitHub OTA not available under bmorcelli/Launcher — update SigurdOS through Launcher instead");
        setStatus(GitHubOTAState::Failed, 0, "Failed",
                  "Update SigurdOS through Launcher instead");
        return false;
    }

    const NodePrefs& p = prefs_get();
    if (!p.wifi_ssid[0]) {
        Serial.println("[gh-ota] No WiFi SSID configured");
        setStatus(GitHubOTAState::Failed, 0, "Failed",
                  "No WiFi configured. Set SSID in Settings.");
        return false;
    }

    if (!wifi::acquire(wifi::Owner::GitHubOta, wifi::RadioMode::Sta)) {
        char error[80];
        snprintf(error, sizeof(error), "WiFi busy: %s",
                 wifi::ownerName(wifi::currentOwner()));
        Serial.printf("[gh-ota] REFUSED: %s\n", error);
        setStatus(GitHubOTAState::Failed, 0, "Failed", error);
        return false;
    }

    s_active.store(true, std::memory_order_release);
    s_cancelled.store(false, std::memory_order_release);
    s_reboot_pending.store(false, std::memory_order_release);
    s_downloaded = 0;
    s_epoch_checked = false;
    s_http_code = 0;
    s_content_length = 0;
    s_last_data = 0;
    s_connect_start = millis();
    s_download_url[0] = '\0';
    s_resolved_tag[0] = '\0';
    s_resolved_target[0] = '\0';

    Serial.printf("[gh-ota] Starting update: branch=%s allow_prerelease=%d\n",
                  p.ota_branch, p.ota_allow_prerelease ? 1 : 0);

    setStatus(GitHubOTAState::Connecting, 0, "Connecting to WiFi...");

    if (!sigurdos::wifi_sta::isConnected()) {
        WiFi.begin(p.wifi_ssid, p.wifi_password);
    } else {
        Serial.printf("[gh-ota] Reusing existing WiFi connection\n");
    }

    Serial.printf("[gh-ota] Connecting to WiFi: %s\n", p.wifi_ssid);

    if (xTaskCreatePinnedToCore(
            [](void*) {
                while (s_active.load(std::memory_order_acquire) &&
                       !s_reboot_pending.load(std::memory_order_acquire)) {
                    serviceWorker();
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
                vTaskDelete(nullptr);
            },
            "github-ota", hal::OTA_WORKER_STACK_BYTES, nullptr, 1,
            nullptr, hal::OTA_WORKER_CORE) != pdPASS) {
        fail("Unable to start OTA worker");
        return false;
    }
    return true;
}

static void serviceWorker() {
    if (!s_active.load(std::memory_order_acquire)) return;
    if (s_cancelled.load(std::memory_order_acquire)) {
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
            if (!isSupportedReleaseChannel(p.ota_branch)) {
                fail("Invalid OTA release channel");
                return;
            }
            const bool needs_api = branchNeedsReleaseApi(
                p.ota_branch, p.ota_allow_prerelease);

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
                hal::OtaHttpObjects api_objects;
                if (!hal::allocate_ota_http_objects(
                        OTA_CONNECTION_ALLOCATOR,
                        hal::OtaAllocationKind::ApiSecureClient,
                        hal::OtaAllocationKind::ApiHttpClient,
                        &api_objects)) {
                    fail("Out of memory for API connection");
                    return;
                }
                s_client = static_cast<WiFiClientSecure*>(api_objects.client);
                s_http = static_cast<HTTPClient*>(api_objects.http);
                s_client->setCACert(GITHUB_ROOT_CA);

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
                    char error[64];
                    snprintf(error, sizeof(error), "Release API request failed (%d)",
                             s_http_code);
                    fail(error);
                    return;
                } else if (s_http_code != HTTP_CODE_OK) {
                    char error[64];
                    snprintf(error, sizeof(error), "Release API HTTP %d", s_http_code);
                    fail(error);
                    return;
                } else {
                    // Read response body
                    WiFiClient* stream = s_http->getStreamPtr();
                    const int api_content_length = s_http->getSize();
                    const ApiBodyStatus body_status = readApiResponse(
                        stream, api_content_length, s_api_buf,
                        API_RESPONSE_MAX, &s_api_buf_len);

                    Serial.printf("[gh-ota] API response: %d/%d bytes\n",
                                  s_api_buf_len, api_content_length);
                    if (body_status == ApiBodyStatus::TooLarge) {
                        fail("Release API response too large");
                        return;
                    }
                    if (body_status != ApiBodyStatus::Complete) {
                        fail("Incomplete release API response");
                        return;
                    }

                    // Parse to find matching release
                    char tag_name[64] = "";
                    char target_name[16] = "";
                    const ReleaseSelectionResult selection =
                        selectReleaseTagResultFromJson(
                            s_api_buf, p.ota_branch, p.ota_allow_prerelease,
                            tag_name, sizeof(tag_name),
                            target_name, sizeof(target_name));
                    if (selection == ReleaseSelectionResult::Matched) {
                        buildReleaseDownloadUrl(tag_name, s_download_url,
                                                sizeof(s_download_url));
                        strncpy(s_resolved_tag, tag_name,
                                sizeof(s_resolved_tag) - 1);
                        s_resolved_tag[sizeof(s_resolved_tag) - 1] = '\0';
                        strncpy(s_resolved_target, target_name,
                                sizeof(s_resolved_target) - 1);
                        s_resolved_target[sizeof(s_resolved_target) - 1] = '\0';
                        Serial.printf("[gh-ota] Download URL: %s\n", s_download_url);
                    } else if (selection == ReleaseSelectionResult::NoMatch) {
                        fail("No matching release for selected channel");
                        return;
                    } else {
                        fail("Malformed release API response");
                        return;
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

            if (!needs_api && s_download_url[0] == '\0' &&
                releaseChannelAllowsFallback(p.ota_branch)) {
                // The global fallback is valid only for an explicit "latest"
                // selection that does not need API prerelease filtering.
                copyFallbackDownloadUrl(s_download_url, sizeof(s_download_url));
                strncpy(s_resolved_tag, "latest", sizeof(s_resolved_tag) - 1);
                s_resolved_tag[sizeof(s_resolved_tag) - 1] = '\0';
                strncpy(s_resolved_target, "latest",
                        sizeof(s_resolved_target) - 1);
                s_resolved_target[sizeof(s_resolved_target) - 1] = '\0';
                Serial.printf("[gh-ota] Using fallback URL: %s\n", s_download_url);
            }

            // Start download phase
            if (s_download_url[0] == '\0') {
                fail("No download URL available");
                return;
            }

            char resolved_status[80];
            snprintf(resolved_status, sizeof(resolved_status),
                     "Downloading [%.15s] %.44s...",
                     s_resolved_target, s_resolved_tag);
            setStatus(GitHubOTAState::Downloading, 0, resolved_status);

            hal::OtaHttpObjects download_objects;
            if (!hal::allocate_ota_http_objects(
                    OTA_CONNECTION_ALLOCATOR,
                    hal::OtaAllocationKind::DownloadSecureClient,
                    hal::OtaAllocationKind::DownloadHttpClient,
                    &download_objects)) {
                fail("Out of memory for download connection");
                return;
            }
            s_client = static_cast<WiFiClientSecure*>(download_objects.client);
            s_http = static_cast<HTTPClient*>(download_objects.http);
            s_client->setCACert(GITHUB_ROOT_CA);

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

            if (s_content_length <= 0 ||
                static_cast<size_t>(s_content_length) > hal::OTA_MAX_IMAGE_BYTES) {
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
            s_epoch_checked = false;
            s_last_progress = millis();
            s_last_data = s_last_progress;

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
        if (s_downloaded < s_content_length && Update.isRunning() &&
            githubOtaDownloadIdleTimedOut(s_last_data, millis())) {
            fail("Download idle timeout");
            return;
        }
        if (!s_http || !s_http->connected()) {
            if (s_downloaded >= s_content_length && s_content_length > 0) {
                if (Update.end(true)) {
                    if (!verifyPendingOtaImage()) {
                        const esp_partition_t* running = esp_ota_get_running_partition();
                        if (running) {
                            (void)esp_ota_set_boot_partition(running);
                        }
                        fail("Image failed authenticity verify");
                        return;
                    }
                    Serial.printf("[gh-ota] Update OK — %d bytes written\n",
                                  s_downloaded);
                    setStatus(GitHubOTAState::Success, 100,
                              "Update complete — rebooting...");
                    cleanupTransfer(false);
                    wifi::release(wifi::Owner::GitHubOta);
                    s_reboot_pending.store(true, std::memory_order_release);
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

        const uint32_t slice_started_at = millis();
        size_t slice_bytes = 0;
        while (stream->available() && s_downloaded < s_content_length &&
               !s_cancelled.load(std::memory_order_acquire) &&
               !hal::otaTransferSliceExhausted(
                   slice_bytes, slice_started_at, millis())) {
            size_t avail = (size_t)stream->available();
            if (!s_epoch_checked && avail < hal::SIGURDOS_OTA_EPOCH_MIN_BYTES) return;
            const size_t image_remaining =
                static_cast<size_t>(s_content_length - s_downloaded);
            const size_t slice_remaining =
                hal::OTA_TRANSFER_SLICE_BYTES - slice_bytes;
            const size_t to_read = hal::otaTransferReadLimit(
                avail, image_remaining, slice_remaining, DOWNLOAD_CHUNK);
            if (to_read == 0) break;

            static uint8_t* buf = (uint8_t*)heap_caps_malloc(
                DOWNLOAD_CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buf) buf = (uint8_t*)heap_caps_malloc(
                DOWNLOAD_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!buf) { fail("Out of memory"); return; }
            size_t read = stream->readBytes(buf, to_read);
            if (read == 0) break;

            if (!s_epoch_checked) {
                uint32_t incoming_epoch = 0;
                const hal::OtaEpochStatus epoch_status = hal::otaCheckSecurityEpoch(
                    buf, read, currentSecurityEpoch(), &incoming_epoch);
                if (epoch_status != hal::OtaEpochStatus::Allowed) {
                    Update.abort();
                    char error[80];
                    snprintf(error, sizeof(error), "%s security epoch (%lu)",
                             epoch_status == hal::OtaEpochStatus::Downgrade
                                 ? "Downgrade rejected" : "Malformed image",
                             (unsigned long)incoming_epoch);
                    fail(error);
                    return;
                }
                s_epoch_checked = true;
            }

            size_t written = Update.write(buf, read);
            size_t next_downloaded = static_cast<size_t>(s_downloaded);
            const hal::OtaWriteResult write_result = hal::otaRecordExactWrite(
                static_cast<size_t>(s_downloaded), read, written,
                static_cast<size_t>(s_content_length), &next_downloaded);
            if (!hal::otaWriteAccepted(write_result)) {
                Serial.printf("[gh-ota] Write mismatch: read=%u written=%u\n",
                              (unsigned)read, (unsigned)written);
                fail("Flash write error");
                return;
            }

            s_downloaded = static_cast<int>(next_downloaded);
            slice_bytes += read;

            unsigned long now = millis();
            s_last_data = now;
            if (now - s_last_progress > 500) {
                s_last_progress = now;
                int pct = (s_content_length > 0)
                    ? (int)(((long)s_downloaded * 100) / s_content_length)
                    : 0;
                char msg[64];
                snprintf(msg, sizeof(msg), "[%.15s] %.24s %d%%",
                         s_resolved_target, s_resolved_tag, pct);
                setStatus(GitHubOTAState::Writing, pct, msg);
                Serial.printf("[gh-ota] %s\n", msg);
            }
        }
    }
}

void loop() {
    // Transport, multipart parsing, and flash writes are worker-owned. Keeping
    // reboot finalization on loopTask prevents filesystem teardown racing mesh
    // persistence after the worker has finished writing the OTA partition.
    if (s_reboot_pending.exchange(false, std::memory_order_acq_rel)) {
        SPIFFS.end();
        delay(500);
        ESP.restart();
    }
}

bool isActive() {
    return s_active.load(std::memory_order_acquire);
}

const GitHubOTAStatus& getStatus() {
    portENTER_CRITICAL(&s_status_mux);
    s_status_snapshot = s_status;
    portEXIT_CRITICAL(&s_status_mux);
    return s_status_snapshot;
}

void cancel() {
    if (!s_active.load(std::memory_order_acquire)) return;
    s_cancelled.store(true, std::memory_order_release);
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
