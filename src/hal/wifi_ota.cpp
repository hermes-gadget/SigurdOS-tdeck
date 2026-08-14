// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// WiFi OTA implementation — WebServer-based firmware upload.

#include "wifi_ota.h"
#include "../diagnostics/log.h"
#include "../comms/secure_wipe.h"
#include "launcher_env.h"
#include "ota_allocation_policy.h"
#include "ota_runtime_policy.h"
#include "ota_security_epoch.h"
#include "ota_write_policy.h"
#include "prefs.h"
#include "wifi_coordinator.h"
#include "../mesh/mesh_wrapper.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <Update.h>
#include <SPIFFS.h>
#include <esp_random.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <atomic>
#include <new>

extern "C" void sigurdosWebServerCancelMultipartClient();

namespace sigurdos {
namespace ota {

static WebServer* server = nullptr;
static std::atomic<bool> active{false};
static std::atomic<bool> stop_requested{false};
static std::atomic<bool> worker_exited{true};
static std::atomic<bool> worker_owns_server{false};
static std::atomic<bool> worker_exit_overdue{false};
static std::atomic<bool> reboot_pending{false};
static std::atomic<StartStatus> start_status{StartStatus::Idle};
static int  save_state_retries  = 0;
static constexpr int MAX_SAVE_STATE_RETRIES = 5;
static char server_ip[16] = "";
static char ap_password[64] = "";
static char last_error[96] = "";
static uint32_t session_started_at = 0;
static std::atomic<uint32_t> worker_tick_ms{0};
static constexpr uint32_t OTA_WORKER_STALE_LIMIT_MS = 3000U;
static constexpr uint32_t OTA_WORKER_JOIN_TIMEOUT_MS = 5000U;
static std::atomic<bool> using_access_point{false};
static std::atomic<bool> ap_release_pending{false};
static std::atomic<bool> companion_transport_parked{false};
static std::atomic<CompanionTransportParkHook> companion_transport_park_hook{nullptr};
static char startup_ssid[33] = "";
static char startup_password[64] = "";
static bool startup_reuse_sta = false;
static String csrf_token;  // regenerated per OTA session
static OtaUploadSessionState upload_state;

extern "C" bool sigurdosWebServerMultipartCancelled() {
    return stop_requested.load(std::memory_order_acquire);
}

// OTA PIN brute-force protection (SEC-001)
static constexpr int MAX_PIN_FAILURES = 5;
static int pin_fail_count = 0;

static uint32_t currentSecurityEpoch() {
    uint32_t epoch = SIGURDOS_SECURITY_EPOCH;
    const esp_app_desc_t* running = esp_ota_get_app_description();
    if (running && running->secure_version > epoch) epoch = running->secure_version;
    return epoch;
}

static void* createOtaServer(void*, hal::OtaAllocationKind kind) {
    if (kind != hal::OtaAllocationKind::WebServer) return nullptr;
    return new (std::nothrow) WebServer(80);
}

static const hal::OtaAllocationOps OTA_SERVER_ALLOCATOR{
    nullptr, createOtaServer, nullptr
};

static void notifyCompanionTransportParked(bool parked)
{
    companion_transport_parked.store(parked, std::memory_order_release);
    const CompanionTransportParkHook hook =
        companion_transport_park_hook.load(std::memory_order_acquire);
    if (hook) hook(parked);
}

static void finalizePendingApRelease()
{
    if (!ap_release_pending.load(std::memory_order_acquire) ||
        wifi::hasOwner(wifi::Owner::ApOta)) {
        return;
    }

    ap_release_pending.store(false, std::memory_order_release);
    using_access_point.store(false, std::memory_order_release);
    notifyCompanionTransportParked(false);
}

enum class ServerCleanupContext : uint8_t {
    Starter,
    Worker,
};

static bool cleanupServer(ServerCleanupContext context) {
    if (worker_owns_server.load(std::memory_order_acquire) &&
        context != ServerCleanupContext::Worker) {
        SIG_LOGE("[ota] refused cross-task cleanup of worker-owned server");
        return false;
    }
    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }
    if (Update.isRunning()) Update.abort();
    const bool was_ap = using_access_point.load(std::memory_order_acquire);
    if (was_ap) WiFi.softAPdisconnect(true);
    if (was_ap) {
        // Keep the AP lease and transport park state visible until the main
        // loop successfully restores the previous hardware mode.
        ap_release_pending.store(true, std::memory_order_release);
    } else {
        using_access_point.store(false, std::memory_order_release);
    }
    sigurdos::comms::secureWipe(ap_password, sizeof(ap_password));
    wifi::requestRelease(wifi::Owner::ApOta);
    session_started_at = 0;
    upload_state = {};
    // #1495: startAccessPoint() runs a full driver deinit/init cycle; re-init
    // the driver here (no-op if already inited) so the Arduino WiFi library
    // keeps working for scans and STA reconnects after an OTA session.
    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    (void)esp_wifi_init(&icfg);
    return true;
}

void setCompanionTransportParkHook(CompanionTransportParkHook hook)
{
    companion_transport_park_hook.store(hook, std::memory_order_release);
    const bool parked = using_access_point.load(std::memory_order_acquire);
    companion_transport_parked.store(parked, std::memory_order_release);
    if (hook) hook(parked);
}

bool companionTransportsAllowed()
{
    return companionTransportsAllowed(
               using_access_point.load(std::memory_order_acquire)) &&
           !companion_transport_parked.load(std::memory_order_acquire);
}

bool isAccessPointActive()
{
    // using_access_point is set before the radio is switched to AP mode so
    // transports are blocked during the setup window as well as the session.
    return using_access_point.load(std::memory_order_acquire);
}

static void requestWorkerStop() {
    stop_requested.store(true, std::memory_order_release);
    worker_exit_overdue.store(false, std::memory_order_release);
    sigurdosWebServerCancelMultipartClient();
}

static bool waitForWorkerExit(uint32_t timeoutMs) {
    const uint32_t startedAt = millis();
    while (!worker_exited.load(std::memory_order_acquire) &&
           static_cast<uint32_t>(millis() - startedAt) < timeoutMs) {
        sigurdosWebServerCancelMultipartClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return worker_exited.load(std::memory_order_acquire);
}

static void abortUploadAndCloseClient() {
    upload_state.failed = true;
    upload_state.started = false;
    if (Update.isRunning()) Update.abort();
    sigurdosWebServerCancelMultipartClient();
}

static bool startSession(const char* ssid, const char* password,
                         bool reuse_sta);

static void otaServerWorker(void*) {
    worker_tick_ms.store(millis(), std::memory_order_release);
    const bool started =
        !stop_requested.load(std::memory_order_acquire) &&
        startSession(startup_ssid, startup_password, startup_reuse_sta);
    sigurdos::comms::secureWipe(startup_password, sizeof(startup_password));

    if (!started || stop_requested.load(std::memory_order_acquire)) {
        if (!last_error[0]) {
            strncpy(last_error, "OTA startup cancelled", sizeof(last_error) - 1);
            last_error[sizeof(last_error) - 1] = '\0';
        }
        (void)cleanupServer(ServerCleanupContext::Worker);
        worker_owns_server.store(false, std::memory_order_release);
        active.store(false, std::memory_order_release);
        worker_exited.store(true, std::memory_order_release);
        start_status.store(StartStatus::Failed, std::memory_order_release);
        vTaskDelete(nullptr);
        return;
    }

    start_status.store(StartStatus::Ready, std::memory_order_release);
    while (!stop_requested.load(std::memory_order_acquire)) {
        worker_tick_ms.store(millis(), std::memory_order_release);
        if (otaSessionExpired(session_started_at, millis())) {
            SIG_LOGW("[ota] session expired");
            break;
        }
        server->handleClient();
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    (void)cleanupServer(ServerCleanupContext::Worker);
    worker_owns_server.store(false, std::memory_order_release);
    active.store(false, std::memory_order_release);
    worker_exited.store(true, std::memory_order_release);
    start_status.store(StartStatus::Idle, std::memory_order_release);
    vTaskDelete(nullptr);
}

static uint32_t workerStaleMs() {
    const uint32_t tick = worker_tick_ms.load(std::memory_order_acquire);
    return tick == 0 ? UINT32_MAX : static_cast<uint32_t>(millis() - tick);
}


static bool verifyPendingOtaImage() {
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part) return false;
    esp_image_metadata_t meta{};
    const esp_partition_pos_t pos = {.offset = part->address, .size = part->size};
    const esp_err_t err = esp_image_verify(ESP_IMAGE_VERIFY, &pos, &meta);
    if (err != ESP_OK) {
        SIG_LOGW("[ota] pending image verify failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// #1495: start the OTA softAP deterministically. The ESP32 WiFi driver keeps
// interface state across esp_wifi_stop(), and an AP that goes through a
// stop→start cycle can fail to re-transmit beacons while still reporting
// success (observed on hardware: session 1 beacons, every restart does not).
// Use a full driver deinit/init cycle for a guaranteed clean radio, configure
// and start the AP explicitly, then verify mode + config readback so a dead AP
// can never masquerade as a live session. cleanupServer() re-inits the driver
// afterwards so the Arduino WiFi library keeps working for scan/STA.
static bool startAccessPoint(const char* ssid, const char* password) {
    WiFi.disconnect(true, true);   // clear any STA association/profile
    WiFi.mode(WIFI_OFF);           // full driver stop
    delay(100);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(100);

    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&icfg) != ESP_OK) {
        strncpy(last_error, "AP driver init failed", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        SIG_LOGE("[ota] esp_wifi_init failed");
        return false;
    }
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
        strncpy(last_error, "AP mode failed", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        SIG_LOGE("[ota] esp_wifi_set_mode(AP) failed");
        return false;
    }

    wifi_config_t apcfg{};
    strlcpy(reinterpret_cast<char*>(apcfg.ap.ssid), ssid, sizeof(apcfg.ap.ssid));
    strlcpy(reinterpret_cast<char*>(apcfg.ap.password), password,
            sizeof(apcfg.ap.password));
    apcfg.ap.ssid_len = static_cast<uint8_t>(strlen(ssid));
    apcfg.ap.channel = 1;
    apcfg.ap.max_connection = 4;
    apcfg.ap.authmode = WIFI_AUTH_WPA2_PSK;

    if (esp_wifi_set_config(WIFI_IF_AP, &apcfg) != ESP_OK) {
        strncpy(last_error, "AP config failed", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        SIG_LOGE("[ota] esp_wifi_set_config(AP) failed");
        return false;
    }
    if (esp_wifi_start() != ESP_OK) {
        strncpy(last_error, "AP start failed", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        SIG_LOGE("[ota] esp_wifi_start failed");
        return false;
    }
    delay(250);  // let the AP settle

    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode != WIFI_MODE_AP) {
        strncpy(last_error, "AP verify: mode != AP", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        SIG_LOGE("[ota] AP verify: mode != AP (%d)", static_cast<int>(mode));
        return false;
    }
    wifi_config_t check{};
    if (esp_wifi_get_config(WIFI_IF_AP, &check) != ESP_OK ||
        strcmp(reinterpret_cast<const char*>(check.ap.ssid), ssid) != 0 ||
        strcmp(reinterpret_cast<const char*>(check.ap.password), password) != 0) {
        snprintf(last_error, sizeof(last_error), "AP verify: cfg mismatch (%s)",
                 reinterpret_cast<const char*>(check.ap.ssid));
        SIG_LOGE("[ota] AP verify: config mismatch (ssid=%s)",
                 reinterpret_cast<const char*>(check.ap.ssid));
        return false;
    }
    // The dialog displays the AP IP. WiFi.softAPIP() can read 0.0.0.0 after a
    // driver deinit/init cycle even though the AP's DHCP server is live, so
    // read the netif directly with the known AP-subnet fallback.
    server_ip[0] = '\0';
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip_info{};
    if (ap_netif && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK &&
        ip_info.ip.addr != 0) {
        snprintf(server_ip, sizeof(server_ip), IPSTR, IP2STR(&ip_info.ip));
    } else {
        strncpy(server_ip, "192.168.4.1", sizeof(server_ip) - 1);
    }
    SIG_LOGW("[ota] AP verified: %s @ %s", ssid, server_ip);
    return true;
}

static bool startSession(const char* ssid, const char* password,
                         bool reuse_sta) {
    // Reset PIN brute-force counter on each OTA session start (SEC-001)
    pin_fail_count = 0;

    IPAddress ip;
    if (reuse_sta) {
        // Already connected to a WiFi network — keep STA, bind on local IP
        ip = WiFi.localIP();
        snprintf(server_ip, sizeof(server_ip), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        SIG_LOGW("[ota] Using STA IP: %s", server_ip);
    } else {
        // Not connected — start AP mode. Uses the deterministic reset +
        // verification path (startAccessPoint) so a session can never appear
        // active while the beacon is absent (#1495).
        if (password && password[0]) {
            strncpy(ap_password, password, sizeof(ap_password) - 1);
            ap_password[sizeof(ap_password) - 1] = '\0';
        } else {
            static constexpr char alphabet[] =
                "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
            for (size_t i = 0; i < 12; ++i) {
                ap_password[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
            }
            ap_password[12] = '\0';
        }
        if (!startAccessPoint(ssid, ap_password)) {
            if (!last_error[0]) {
                strncpy(last_error, "WiFi AP startup failed", sizeof(last_error) - 1);
                last_error[sizeof(last_error) - 1] = '\0';
            }
            SIG_LOGE("[ota] WiFi AP startup failed");
            return false;
        }

        ip = WiFi.softAPIP();
        if (ip != IPAddress(0U, 0U, 0U, 0U)) {
            snprintf(server_ip, sizeof(server_ip), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        }
        SIG_LOGW("[ota] WiFi AP started: %s @ %s", ssid, server_ip);
    }

    // Set up web server
    void* server_object = nullptr;
    if (!hal::allocate_ota_object(OTA_SERVER_ALLOCATOR,
                                  hal::OtaAllocationKind::WebServer,
                                  &server_object)) {
        strncpy(last_error, "Out of memory for OTA server", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        SIG_LOGE("[ota] WebServer allocation failed; OTA aborted");
        return false;
    }
    server = static_cast<WebServer*>(server_object);

    // Root page — OTA upload form with CSRF token
    server->on("/", HTTP_GET, []() {
        server->sendHeader("Connection", "close");
        // Regenerate CSRF token per session (ESP32 hardware RNG)
        csrf_token = "";
        for (int i = 0; i < 16; i++) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02x", (uint8_t)esp_random());
            csrf_token += hex;
        }
        // Build page dynamically with embedded CSRF token
        String html = F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>SigurdOS OTA</title>"
            "<style>"
            "body{background:#0F0F0F;color:#00BFFF;font-family:monospace;text-align:center;padding:20px}"
            "h1{font-size:20px;margin-bottom:10px}"
            "input[type=file],input[type=password]{margin:20px 0;padding:10px;background:#1A1A2E;color:#00BFFF;border:2px solid #00BFFF;width:80%;max-width:300px;box-sizing:border-box}"
            "input[type=password]::placeholder{color:#4A4A6E}"
            "input[type=submit]{padding:10px 30px;background:#00BFFF;color:#0F0F0F;border:none;font-weight:bold;cursor:pointer}"
            "#progress{width:80%;height:20px;background:#1A1A2E;border:2px solid #00BFFF;margin:20px auto;display:none}"
            "#bar{width:0;height:100%;background:#00BFFF}"
            "#status{margin-top:10px;font-size:14px}"
            "#error{color:#FF4444;margin-top:10px;display:none}"
            "</style></head><body>"
            "<h1>SigurdOS Firmware Update</h1>"
            "<p>Enter device PIN and select firmware.bin.</p>"
            "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\" id=\"otaform\">");
        html += F("<input type=\"hidden\" name=\"csrf\" value=\"");
        html += csrf_token;
        html += F("\">"
            "<input type=\"password\" name=\"pin\" placeholder=\"Device PIN\" required><br>"
            "<input type=\"file\" name=\"firmware\" accept=\".bin\" required><br>"
            "<input type=\"submit\" value=\"Update\">"
            "</form>"
            "<div id=\"progress\"><div id=\"bar\"></div></div>"
            "<div id=\"error\"></div>"
            "<div id=\"status\"></div>"
            "<script>"
            "document.getElementById('otaform').addEventListener('submit',function(e){"
            "e.preventDefault();"
            "var pin=document.querySelector('input[name=pin]').value;"
            "var file=document.querySelector('input[type=file]').files[0];"
            "if(!file||!pin)return;"
            "var formData=new FormData();"
            "formData.append('pin',pin);"
            "formData.append('csrf',document.querySelector('input[name=csrf]').value);"
            "formData.append('firmware',file);"
            "var xhr=new XMLHttpRequest();"
            "xhr.open('POST','/update',true);"
            "xhr.onload=function(){"
            "if(xhr.status==200){"
            "document.getElementById('status').textContent='Update OK — rebooting...';"
            "}else{"
            "document.getElementById('error').textContent='Update FAILED: '+xhr.responseText;"
            "document.getElementById('error').style.display='block';"
            "}};"
            "xhr.onerror=function(){"
            "document.getElementById('error').textContent='Network error';"
            "document.getElementById('error').style.display='block';"
            "};"
            "xhr.send(formData);"
            "});"
            "</script></body></html>");
        server->send(200, "text/html", html);
    });

    // Firmware upload handler
    server->on("/update", HTTP_POST,
        // Completion handler
        []() {
            server->sendHeader("Connection", "close");
            if (!upload_state.completed || upload_state.failed || Update.hasError()) {
                const char* error = Update.hasError()
                    ? Update.errorString() : "Upload incomplete";
                server->send(500, "text/plain", error);
            } else {
                server->send(200, "text/plain", "OK");
                // The WebServer worker must not restart the MCU. Let it finish
                // this response and release all OTA resources; loop() owns
                // the durable checkpoint and reboot handoff.
                reboot_pending.store(true, std::memory_order_release);
                stop_requested.store(true, std::memory_order_release);
            }
        },
        // Upload handler (receives chunks)
        []() {
            HTTPUpload& upload = server->upload();
            if (stop_requested.load(std::memory_order_acquire)) {
                abortUploadAndCloseClient();
                return;
            }
            if (upload.status == UPLOAD_FILE_START) {
                upload_state = {};
                // Validate device PIN before accepting upload
                const NodePrefs& p = prefs_get();
                String pin_arg = server->arg("pin");
                // CSRF token must match the token served with the form
                String csrf_arg = server->arg("csrf");
                if (csrf_arg.length() == 0 || csrf_arg != csrf_token) {
                    SIG_LOGW("[ota] Upload rejected: invalid CSRF token");
                    abortUploadAndCloseClient();
                    return;
                }
                // PIN brute-force protection: lock out after N failures (SEC-001).
                // Counter resets when OTA is restarted (start() called again).
                if (pin_fail_count >= MAX_PIN_FAILURES) {
                    SIG_LOGW("[ota] Upload rejected: too many failed PIN attempts (%d) — restart OTA to retry",
                             pin_fail_count);
                    abortUploadAndCloseClient();
                    return;
                }
                // A PIN is mandatory: start() refuses to run without one, so
                // device_pin is always non-zero here. Treat a missing/zero PIN
                // as unauthenticated rather than silently accepting (#687).
                bool pin_valid = otaPinAccepts(p.device_pin, pin_arg.c_str());
                if (!pin_valid) {
                    pin_fail_count++;
                    SIG_LOGW("[ota] Upload rejected: invalid PIN (attempt %d/%d)",
                             pin_fail_count, MAX_PIN_FAILURES);
                    // Keep the upload session failed so WRITE/END callbacks
                    // cannot touch flash or reboot the device.
                    abortUploadAndCloseClient();
                    return;
                }
                upload_state.authenticated = true;
                SIG_LOGD("[ota] Update start: %s (%u bytes)",
                         upload.filename.c_str(), upload.totalSize);
                static_assert(OTA_UPDATE_SIZE_UNKNOWN == UPDATE_SIZE_UNKNOWN,
                              "Arduino Update unknown-size sentinel changed");
                const size_t update_size = otaUpdateBeginSize(upload.totalSize);
                const bool begin_ok = Update.begin(update_size);
                SIG_LOGD("[ota] begin(%u) ok=%d size=%u running=%d err=%s",
                         static_cast<unsigned>(update_size), begin_ok ? 1 : 0,
                         static_cast<unsigned>(Update.size()), Update.isRunning() ? 1 : 0,
                         Update.errorString());
                if (!begin_ok) {
                    abortUploadAndCloseClient();
                    SIG_LOGW("[ota] Update.begin failed: %s", Update.errorString());
                    Update.printError(Serial);
                } else {
                    upload_state.started = true;
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (!otaUploadAcceptsChunk(upload_state)) return;
                if (!upload_state.epoch_checked) {
                    uint32_t incoming_epoch = 0;
                    const hal::OtaEpochStatus epoch_status = hal::otaCheckSecurityEpoch(
                        upload.buf, upload.currentSize, currentSecurityEpoch(),
                        &incoming_epoch);
                    if (epoch_status != hal::OtaEpochStatus::Allowed) {
                        abortUploadAndCloseClient();
                        SIG_LOGW("[ota] Upload rejected: %s security epoch (%u)",
                                 epoch_status == hal::OtaEpochStatus::Downgrade
                                     ? "downgrade" : "malformed",
                                 static_cast<unsigned>(incoming_epoch));
                        return;
                    }
                    upload_state.epoch_checked = true;
                }
                const size_t written = Update.write(upload.buf, upload.currentSize);
                size_t next_received = upload_state.received;
                // #1495: the WebServer's HTTPUpload::totalSize is a per-chunk
                // accumulator — zero at START, growing by each delivered chunk —
                // never the advertised file size. It cannot bound individual
                // writes; the real full-size check runs at END via
                // otaUploadCanFinish(). Pass unknown (0) so the write path only
                // guards against short writes and counter overflow.
                const hal::OtaWriteResult write_result = hal::otaRecordExactWrite(
                    upload_state.received, upload.currentSize, written,
                    0, &next_received);
                if (!hal::otaWriteAccepted(write_result)) {
                    abortUploadAndCloseClient();
                    SIG_LOGW("[ota] Update.write failed: %s", Update.errorString());
                    Update.printError(Serial);
                } else {
                    upload_state.received = next_received;
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (!otaUploadCanFinish(upload_state, upload.totalSize)) {
                    upload_state.failed = true;
                    upload_state.started = false;
                    Update.abort();
                    SIG_LOGW("[ota] Upload incomplete: received=%u total=%u",
                             static_cast<unsigned>(upload_state.received),
                             static_cast<unsigned>(upload.totalSize));
                    return;
                }
                if (Update.end(true)) {
                    // Authenticate the written image as a valid ESP application
                    // (header, segments, checksum/hash). Epoch was already
                    // enforced on the first chunk.
                    if (!verifyPendingOtaImage()) {
                        upload_state.failed = true;
                        upload_state.started = false;
                        const esp_partition_t* running = esp_ota_get_running_partition();
                        if (running) {
                            (void)esp_ota_set_boot_partition(running);
                        }
                        SIG_LOGW("[ota] Upload rejected: image failed authenticity verify");
                    } else {
                        upload_state.started = false;
                        upload_state.completed = true;
                        SIG_LOGW("[ota] Update success: %u bytes", upload.totalSize);
                    }
                } else {
                    upload_state.failed = true;
                    upload_state.started = false;
                    SIG_LOGW("[ota] Update.end failed: %s", Update.errorString());
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_ABORTED) {
                abortUploadAndCloseClient();
                SIG_LOGW("[ota] Upload aborted after %u bytes",
                         static_cast<unsigned>(upload_state.received));
            }
        }
    );

    server->onNotFound([]() {
        server->send(404, "text/plain", "Not found");
    });

    server->begin();
    session_started_at = millis();
    return true;
}

bool start(const char* ssid, const char* password) {
    finalizePendingApRelease();
    if (ap_release_pending.load(std::memory_order_acquire)) {
        wifi::requestRelease(wifi::Owner::ApOta);
        strncpy(last_error, "WiFi cleanup is still in progress",
                sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        start_status.store(StartStatus::Failed, std::memory_order_release);
        return false;
    }

    if (active.load(std::memory_order_acquire)) {
        if (!stop_requested.load(std::memory_order_acquire) &&
            start_status.load(std::memory_order_acquire) != StartStatus::Failed) {
            SIG_LOGW("[ota] OTA session already starting or active — duplicate start ignored");
            return true;
        }
        strncpy(last_error, "OTA session is shutting down — retry in a few seconds",
                sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        start_status.store(StartStatus::Failed, std::memory_order_release);
        return false;
    }

    if (reboot_pending.load(std::memory_order_acquire)) {
        strncpy(last_error, "OTA reboot pending", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        start_status.store(StartStatus::Failed, std::memory_order_release);
        SIG_LOGW("[ota] REFUSED: reboot pending after completed upload");
        return false;
    }

    sigurdos::comms::secureWipe(ap_password, sizeof(ap_password));
    sigurdos::comms::secureWipe(startup_password, sizeof(startup_password));
    using_access_point.store(false, std::memory_order_release);
    server_ip[0] = '\0';
    last_error[0] = '\0';

    if (!otaAccessPointInputsValid(ssid, password)) {
        strncpy(last_error, "Invalid OTA WiFi name or password", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        start_status.store(StartStatus::Failed, std::memory_order_release);
        SIG_LOGW("[ota] REFUSED: invalid AP SSID or password length");
        return false;
    }

    if (sigurdos_is_under_launcher()) {
        strncpy(last_error, "Update through Launcher instead", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        start_status.store(StartStatus::Failed, std::memory_order_release);
        SIG_LOGW("[ota] REFUSED: OTA not available under bmorcelli/Launcher — update SigurdOS through Launcher instead");
        return false;
    }

    // The device PIN is the upload endpoint's only authentication.
    const uint32_t device_pin = prefs_get().device_pin;
    if (device_pin == 0) {
        strncpy(last_error, "Set a device PIN before local OTA", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        start_status.store(StartStatus::Failed, std::memory_order_release);
        SIG_LOGW("[ota] REFUSED: no device PIN set — set a PIN before using WiFi OTA");
        return false;
    }
    if (!otaDevicePinEligible(device_pin)) {
        strncpy(last_error, "Set a 6+ digit device PIN before local OTA",
                sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        start_status.store(StartStatus::Failed, std::memory_order_release);
        SIG_LOGW("[ota] REFUSED: device PIN too weak for OTA (need 6+ digits)");
        return false;
    }

    const bool reuse_sta = wifi_sta::isConnected();
    const wifi::RadioMode requested_mode =
        reuse_sta ? wifi::RadioMode::Sta : wifi::RadioMode::Ap;
    if (!wifi::reserveForWorker(wifi::Owner::ApOta, requested_mode)) {
        snprintf(last_error, sizeof(last_error), "WiFi busy: %s",
                 wifi::ownerName(wifi::currentOwner()));
        start_status.store(StartStatus::Failed, std::memory_order_release);
        SIG_LOGW("[ota] REFUSED: %s", last_error);
        return false;
    }

    strlcpy(startup_ssid, ssid, sizeof(startup_ssid));
    strlcpy(startup_password, password ? password : "",
            sizeof(startup_password));
    startup_reuse_sta = reuse_sta;
    using_access_point.store(!reuse_sta, std::memory_order_release);
    if (!reuse_sta) notifyCompanionTransportParked(true);

    stop_requested.store(false, std::memory_order_release);
    worker_exited.store(false, std::memory_order_release);
    worker_owns_server.store(true, std::memory_order_release);
    worker_exit_overdue.store(false, std::memory_order_release);
    worker_tick_ms.store(0, std::memory_order_release);
    active.store(true, std::memory_order_release);
    start_status.store(StartStatus::Starting, std::memory_order_release);
    if (xTaskCreatePinnedToCore(
            otaServerWorker, "wifi-ota", hal::OTA_WORKER_STACK_BYTES,
            nullptr, 1, nullptr, hal::OTA_WORKER_CORE) != pdPASS) {
        strncpy(last_error, "Unable to start OTA worker", sizeof(last_error) - 1);
        last_error[sizeof(last_error) - 1] = '\0';
        active.store(false, std::memory_order_release);
        worker_owns_server.store(false, std::memory_order_release);
        worker_exited.store(true, std::memory_order_release);
        start_status.store(StartStatus::Failed, std::memory_order_release);
        using_access_point.store(false, std::memory_order_release);
        notifyCompanionTransportParked(false);
        sigurdos::comms::secureWipe(startup_password,
                                   sizeof(startup_password));
        if (!wifi::release(wifi::Owner::ApOta) &&
            wifi::hasOwner(wifi::Owner::ApOta)) {
            wifi::requestRelease(wifi::Owner::ApOta);
        }
        return false;
    }
    return true;
}

void loop() {
    finalizePendingApRelease();

    // Expiry requests cancellation and wakes a blocked multipart read. The
    // supervisor never deletes the worker-owned server; it waits for the
    // worker's post-cleanup exit acknowledgement instead.
    if (active.load(std::memory_order_acquire) && session_started_at != 0 &&
        !reboot_pending.load(std::memory_order_acquire) &&
        otaSessionExpired(session_started_at, millis())) {
        if (!stop_requested.load(std::memory_order_acquire)) {
            SIG_LOGW("[ota] session expired — requesting worker exit");
            requestWorkerStop();
        } else if (workerStaleMs() > OTA_WORKER_STALE_LIMIT_MS &&
                   !worker_exit_overdue.exchange(true, std::memory_order_acq_rel)) {
            // Re-close the active socket, but never reclaim the worker's
            // WebServer. The worker acknowledges exit after owning cleanup.
            SIG_LOGW("[ota] worker exit overdue — waiting for owner cleanup");
            sigurdosWebServerCancelMultipartClient();
        }
    }

    // WebServer multipart parsing and Update writes are worker-owned so a slow
    // or body-less client cannot hold Arduino's loopTask. Once that worker has
    // released its resources, the loop task owns the normal reboot path.
    if (!hal::otaRebootMayFinalize(
            reboot_pending.load(std::memory_order_acquire),
            active.load(std::memory_order_acquire))) {
        return;
    }
    if (!reboot_pending.exchange(false, std::memory_order_acq_rel)) return;

    if (!sigurdos::mesh::saveState()) {
        SIG_LOGE("[ota] Reboot deferred: durable state checkpoint failed");
        reboot_pending.store(true, std::memory_order_release);
        if (++save_state_retries >= MAX_SAVE_STATE_RETRIES) {
            SIG_LOGE("[ota] saveState retry limit reached — rebooting without "
                     "checkpoint");
            SPIFFS.end();
            delay(500);
            ESP.restart();
        }
        return;
    }

    SPIFFS.end();
    delay(500);
    ESP.restart();
}

void stop() {
    if (!active.load(std::memory_order_acquire)) return;
    requestWorkerStop();
    if (!waitForWorkerExit(OTA_WORKER_JOIN_TIMEOUT_MS)) {
        // Fail safe: retain the object rather than deleting it from this task.
        // The parser's absolute deadline remains the final bounded escape.
        SIG_LOGW("[ota] worker did not acknowledge stop within %u ms",
                 static_cast<unsigned>(OTA_WORKER_JOIN_TIMEOUT_MS));
    }
}

bool isActive() {
    return active.load(std::memory_order_acquire);
}

StartStatus getStartStatus() {
    return start_status.load(std::memory_order_acquire);
}

bool isRebootPending() {
    return reboot_pending.load(std::memory_order_acquire);
}

const char* getLastError() {
    return last_error;
}

const char* getIP() {
    return active.load(std::memory_order_acquire) ? server_ip : "";
}

const char* getAPPassword() {
    const bool ota_active = active.load(std::memory_order_acquire);
    const bool ap_session = using_access_point.load(std::memory_order_acquire);
    return otaApPasswordVisible(ota_active, ap_session) ? ap_password : "";
}

}  // namespace ota

// ── WiFi Site Survey ─────────────────────────────────────
namespace wifi_scan {

namespace {

AsyncScanState scan_state;
bool scan_lease_held = false;
bool scan_driver_started = false;
uint32_t scan_acquired_at = 0;
int scan_driver_count = -1;
int scan_capacity = 0;

void releaseScan() {
    WiFi.scanDelete();
    if (scan_lease_held) {
        if (wifi::release(wifi::Owner::Scan)) {
            scan_lease_held = false;
        } else {
            // Keep the local lease marker until the coordinator has actually
            // restored the previous hardware mode. The loop-level service
            // path will retry a transient driver failure.
            wifi::requestRelease(wifi::Owner::Scan);
        }
    }
    scan_driver_started = false;
    scan_acquired_at = 0;
    scan_driver_count = -1;
    scan_capacity = 0;
}

}  // namespace

StartResult begin() {
    if (scan_lease_held) {
        if (wifi::hasOwner(wifi::Owner::Scan)) return {Status::Busy, 0};
        // A queued release succeeded on the main loop since the last scan
        // cleanup. Reconcile the local marker before acquiring a new lease.
        scan_lease_held = false;
    }
    if (!wifi::acquire(wifi::Owner::Scan, wifi::RadioMode::Sta)) {
        return {Status::Busy, 0};
    }
    scan_lease_held = true;
    WiFi.scanDelete();

    const uint32_t token = scan_state.start();
    scan_acquired_at = millis();
    return {Status::Running, token};
}

PollResult poll(uint32_t token, APInfo* out, int max_aps, uint32_t budget_ms) {
    Status status = scan_state.status(token);
    if (status != Status::Running) {
        return {status, scan_state.count(token)};
    }
    if (!out || max_aps <= 0) {
        scan_state.finish(token, Status::Error);
        releaseScan();
        return {Status::Error, 0};
    }

    // Preserve the ESP32-S3 radio settle interval without blocking LVGL.
    if (!scan_driver_started) {
        if (millis() - scan_acquired_at < SIGURDOS_WIFI_SCAN_SETTLE_MS) {
            return {Status::Running, 0};
        }
        scan_driver_started = true;
        const int started = WiFi.scanNetworks(true, false);
        if (started == WIFI_SCAN_FAILED) {
            scan_state.finish(token, Status::Error);
            releaseScan();
            return {Status::Error, 0};
        }
        if (started >= 0) {
            scan_driver_count = started;
            if (started == 0) {
                scan_state.resultsReady(token, 0, max_aps);
                releaseScan();
                return {Status::Complete, 0};
            }
        }
    }

    if (scan_capacity == 0) {
        if (scan_driver_count < 0) {
            scan_driver_count = WiFi.scanComplete();
            if (scan_driver_count == WIFI_SCAN_RUNNING) {
                return {Status::Running, 0};
            }
            if (scan_driver_count == WIFI_SCAN_FAILED) {
                scan_state.finish(token, Status::Error);
                releaseScan();
                return {Status::Error, 0};
            }
        }
        scan_capacity = max_aps;
        scan_state.resultsReady(token, scan_driver_count, max_aps);
        status = scan_state.status(token);
        if (status == Status::Complete) {
            releaseScan();
            return {status, 0};
        }
    }

    const uint32_t started_at = millis();
    int copied = 0;
    int index = 0;
    while (copied < SIGURDOS_WIFI_SCAN_RESULTS_PER_POLL &&
           scan_state.takeNext(token, index)) {
        strncpy(out[index].ssid, WiFi.SSID(index).c_str(),
                sizeof(out[index].ssid) - 1);
        out[index].ssid[sizeof(out[index].ssid) - 1] = '\0';
        out[index].rssi = WiFi.RSSI(index);
        out[index].channel = WiFi.channel(index);
        out[index].encrypted = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
        ++copied;
        if (budget_ms == 0 || millis() - started_at >= budget_ms) break;
    }

    status = scan_state.status(token);
    const int count = scan_state.count(token);
    if (status == Status::Complete) {
        sortByRssi(out, count);
        releaseScan();
    }
    return {status, count};
}

void cancel(uint32_t token) {
    if (!scan_state.finish(token, Status::Cancelled)) return;
    releaseScan();
}

}  // namespace wifi_scan

// ── WiFi STA Client ──────────────────────────────────────
namespace wifi_sta {

namespace {

static constexpr uint32_t AUTO_RECONNECT_INTERVAL_MS = 30000U;

bool s_connected = false;
bool s_lease_held = false;
int s_rssi = 0;
Status s_status = Status::Idle;
uint32_t s_conn_start = 0;
uint32_t s_reconnect_attempts = 0;
uint32_t s_last_attempt_ms = 0;
uint32_t s_next_reconnect_ms = 0;
char s_ssid[WIFI_STA_SSID_CAPACITY] = {};
char s_ip[WIFI_STA_IP_CAPACITY] = {};
char s_error[WIFI_STA_ERROR_CAPACITY] = {};

void copyBounded(char* destination, size_t capacity, const char* value)
{
    if (!destination || capacity == 0) return;
    if (!value) value = "";
    strncpy(destination, value, capacity - 1);
    destination[capacity - 1] = '\0';
}

void clearIp()
{
    s_ip[0] = '\0';
}

void clearError()
{
    s_error[0] = '\0';
}

void setError(const char* error)
{
    copyBounded(s_error, sizeof(s_error), error);
}

void captureSsid()
{
    copyBounded(s_ssid, sizeof(s_ssid), WiFi.SSID().c_str());
}

void captureIp()
{
    if (!s_connected) {
        clearIp();
        return;
    }
    const IPAddress ip = WiFi.localIP();
    snprintf(s_ip, sizeof(s_ip), "%u.%u.%u.%u",
             static_cast<unsigned>(ip[0]), static_cast<unsigned>(ip[1]),
             static_cast<unsigned>(ip[2]), static_cast<unsigned>(ip[3]));
}

bool deadlineReached(uint32_t now, uint32_t deadline)
{
    return deadline != 0U && static_cast<int32_t>(now - deadline) >= 0;
}

bool hardwareFailureDetected()
{
    const auto status = WiFi.status();
    return status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL;
}

const char* hardwareFailureText()
{
    switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL:
        return "Network not found";
    case WL_CONNECT_FAILED:
        return "Authentication failed";
    case WL_CONNECTION_LOST:
        return "Connection lost";
    case WL_DISCONNECTED:
        return "No response from access point";
    default:
        return "Connection timed out";
    }
}

void markConnected()
{
    s_connected = true;
    s_status = Status::Connected;
    s_rssi = WiFi.RSSI();
    captureSsid();
    captureIp();
    clearError();
    s_next_reconnect_ms = 0;
}

void releaseAfterFailure(const char* error)
{
    WiFi.disconnect();
    if (wifi::release(wifi::Owner::Sta)) {
        s_lease_held = false;
    } else {
        wifi::requestRelease(wifi::Owner::Sta);
    }
    s_status = Status::Failed;
    s_connected = false;
    s_rssi = 0;
    clearIp();
    setError(error);
    s_next_reconnect_ms = millis() + AUTO_RECONNECT_INTERVAL_MS;
}

}  // namespace

bool beginConnect(const char* ssid, const char* password) {
    if (s_lease_held && !wifi::hasOwner(wifi::Owner::Sta)) {
        s_lease_held = false;
    }
    if (!ssid || !ssid[0]) {
        s_status = Status::Failed;
        s_connected = false;
        s_rssi = 0;
        clearIp();
        setError("WiFi network name is empty");
        return false;
    }
    if (!wifi::acquire(wifi::Owner::Sta, wifi::RadioMode::Sta)) {
        char busy[WIFI_STA_ERROR_CAPACITY];
        snprintf(busy, sizeof(busy), "WiFi busy: %s",
                 wifi::ownerName(wifi::currentOwner()));
        setError(busy);
        SIG_LOGW("[wifi-sta] connect refused: WiFi busy with %s",
                 wifi::ownerName(wifi::currentOwner()));
        return false;
    }
    s_lease_held = true;
    delay(100);  // let MAC/BB/RF settle after potential mode switch (ESP32-S3 erratum)
    WiFi.begin(ssid, password);
    s_status = Status::Connecting;
    s_conn_start = millis();
    s_last_attempt_ms = s_conn_start;
    ++s_reconnect_attempts;
    s_next_reconnect_ms = s_conn_start + AUTO_RECONNECT_INTERVAL_MS;
    s_connected = false;
    s_rssi = 0;
    clearIp();
    copyBounded(s_ssid, sizeof(s_ssid), ssid);
    clearError();
    SIG_LOGD("[wifi-sta] connecting to %s...", ssid);
    return true;
}

Status getStatus() {
    return s_status;
}

StatusInfo getStatusInfo()
{
    StatusInfo info{};
    info.status = s_status;
    info.mode = ota::isAccessPointActive()
        ? LinkMode::AccessPoint
        : ((s_connected || s_status == Status::Connecting)
            ? LinkMode::Station : LinkMode::Off);
    info.connected = s_connected;
    info.rssi = s_rssi;
    info.reconnect_attempts = s_reconnect_attempts;
    info.last_attempt_ms = s_last_attempt_ms;
    info.next_reconnect_ms = s_next_reconnect_ms;
    copyBounded(info.ssid, sizeof(info.ssid), s_ssid);
    copyBounded(info.ip, sizeof(info.ip), s_ip);
    copyBounded(info.error, sizeof(info.error), s_error);
    return info;
}

bool reconnect()
{
    const NodePrefs& prefs = sigurdos::prefs_get();
    if (!prefs.wifi_ssid[0]) {
        s_status = Status::Failed;
        setError("No saved WiFi credentials");
        return false;
    }
    return beginConnect(prefs.wifi_ssid, prefs.wifi_password);
}

void disconnect() {
    if (s_lease_held && !wifi::hasOwner(wifi::Owner::Sta)) {
        s_lease_held = false;
    }
    if (!wifi::hasOwner(wifi::Owner::Sta)) return;
    if (s_connected || s_status == Status::Connecting) {
        WiFi.disconnect();
    }
    if (wifi::release(wifi::Owner::Sta)) {
        s_lease_held = false;
    } else {
        wifi::requestRelease(wifi::Owner::Sta);
    }
    s_connected = false;
    s_rssi = 0;
    s_status = Status::Idle;
    s_next_reconnect_ms = 0;
    clearIp();
    clearError();
}

bool isConnected() {
    // Always check hardware — never rely solely on internal state.
    // External code (github_ota, WiFi.begin from another path) can
    // change the radio state without going through our state machine.
    bool hw = (WiFi.status() == WL_CONNECTED);
    if (hw) {
        if (!s_connected) markConnected();
    } else if (s_connected || s_status == Status::Connected) {
        s_connected = false;
        s_rssi = 0;
        s_status = Status::Idle;
        clearIp();
        setError("Connection lost");
        s_next_reconnect_ms = millis() + AUTO_RECONNECT_INTERVAL_MS;
    }
    return s_connected;
}

int getRSSI() {
    if (isConnected()) {
        s_rssi = WiFi.RSSI();
    }
    return s_rssi;
}

void loop() {
    const wifi::Owner owner = wifi::currentOwner();
    if (owner != wifi::Owner::None && owner != wifi::Owner::Sta) return;

    // Always sync internal state with hardware (handles external
    // reconnections, e.g. github_ota reusing an existing STA link).
    bool hw = (WiFi.status() == WL_CONNECTED);

    if (s_status == Status::Connecting) {
        const Status next = advanceConnectingStatus(
            s_status, hw, static_cast<uint32_t>(millis() - s_conn_start));
        if (next == Status::Connected) {
            markConnected();
            SIG_LOGD("[wifi-sta] connected! (%d dBm)", s_rssi);
        } else if (next == Status::Failed || hardwareFailureDetected()) {
            const char* error = hardwareFailureText();
            releaseAfterFailure(error);
            SIG_LOGW("[wifi-sta] connection failed: %s", error);
        }
        hw = s_connected;
    }

    if (hw && !s_connected && s_status != Status::Connecting) {
        // Hardware is connected but we didn't know — external reconnect.
        markConnected();
        SIG_LOGD("[wifi-sta] reconnected externally (%d dBm)", s_rssi);
    } else if (!hw && (s_connected || s_status == Status::Connected)) {
        // Was connected, now not.
        s_connected = false;
        s_rssi = 0;
        s_status = Status::Idle;
        clearIp();
        setError("Connection lost");
        s_next_reconnect_ms = millis() + AUTO_RECONNECT_INTERVAL_MS;
        SIG_LOGW("[wifi-sta] disconnected; reconnect scheduled");
    }

    // ── Auto-reconnect ──────────────────────────────────────
    // If we have saved credentials and aren't connected/connecting,
    // periodically attempt to reconnect (every 30 seconds).
    if (!s_connected && s_status != Status::Connecting) {
        const uint32_t now = millis();
        if (s_next_reconnect_ms == 0U) {
            s_next_reconnect_ms = now + AUTO_RECONNECT_INTERVAL_MS;
        } else if (deadlineReached(now, s_next_reconnect_ms)) {
            const NodePrefs& p = sigurdos::prefs_get();
            s_next_reconnect_ms = now + AUTO_RECONNECT_INTERVAL_MS;
            if (p.wifi_ssid[0]) {
                SIG_LOGD("[wifi-sta] auto-reconnecting to %s...",
                         p.wifi_ssid);
                beginConnect(p.wifi_ssid, p.wifi_password);
            }
        }
    }
}

}  // namespace wifi_sta
}  // namespace sigurdos
