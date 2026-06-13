// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// WiFi OTA implementation — WebServer-based firmware upload.

#include "wifi_ota.h"
#include "../diagnostics/log.h"
#include "launcher_env.h"
#include "prefs.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

namespace sigurdos {
namespace ota {

static WebServer* server = nullptr;
static bool active = false;
static char server_ip[16] = "";

static const char OTA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SigurdOS OTA</title>
<style>
body{background:#0F0F0F;color:#00BFFF;font-family:monospace;text-align:center;padding:20px}
h1{font-size:20px;margin-bottom:10px}
input[type=file],input[type=password]{margin:20px 0;padding:10px;background:#1A1A2E;color:#00BFFF;border:2px solid #00BFFF;width:80%;max-width:300px;box-sizing:border-box}
input[type=password]::placeholder{color:#4A4A6E}
input[type=submit]{padding:10px 30px;background:#00BFFF;color:#0F0F0F;border:none;font-weight:bold;cursor:pointer}
#progress{width:80%;height:20px;background:#1A1A2E;border:2px solid #00BFFF;margin:20px auto;display:none}
#bar{width:0;height:100%;background:#00BFFF}
#status{margin-top:10px;font-size:14px}
#error{color:#FF4444;margin-top:10px;display:none}
</style></head><body>
<h1>SigurdOS Firmware Update</h1>
<p>Enter device PIN and select firmware.bin.</p>
<form method="POST" action="/update" enctype="multipart/form-data" id="otaform">
<input type="password" name="pin" placeholder="Device PIN" required><br>
<input type="file" name="firmware" accept=".bin" required><br>
<input type="submit" value="Update">
</form>
<div id="progress"><div id="bar"></div></div>
<div id="error"></div>
<div id="status"></div>
<script>
document.getElementById('otaform').addEventListener('submit',function(e){
e.preventDefault();
var pin=document.querySelector('input[name=pin]').value;
var file=document.querySelector('input[type=file]').files[0];
if(!file||!pin)return;
var formData=new FormData();
formData.append('pin',pin);
formData.append('firmware',file);
var xhr=new XMLHttpRequest();
xhr.open('POST','/update',true);
xhr.onload=function(){
if(xhr.status==200){
document.getElementById('status').textContent='Update OK — rebooting...';
}else{
document.getElementById('error').textContent='Update FAILED: '+xhr.responseText;
document.getElementById('error').style.display='block';
}
};
xhr.onerror=function(){
document.getElementById('error').textContent='Network error';
document.getElementById('error').style.display='block';
};
xhr.send(formData);
});
</script></body></html>
)rawliteral";

bool start(const char* ssid, const char* password) {
    if (active) return true;

    if (sigurdos_is_under_launcher()) {
        SIG_LOGW("[ota] REFUSED: OTA not available under bmorcelli/Launcher — update SigurdOS through Launcher instead");
        return false;
    }

    IPAddress ip;
    if (wifi_sta::isConnected()) {
        // Already connected to a WiFi network — keep STA, bind on local IP
        ip = WiFi.localIP();
        snprintf(server_ip, sizeof(server_ip), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        SIG_LOGW("[ota] Using STA IP: %s", server_ip);
    } else {
        // Not connected — start AP mode
        WiFi.mode(WIFI_AP);
        if (password && password[0]) {
            WiFi.softAP(ssid, password);
        } else {
            WiFi.softAP(ssid);
        }

        ip = WiFi.softAPIP();
        snprintf(server_ip, sizeof(server_ip), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        SIG_LOGW("[ota] WiFi AP started: %s @ %s", ssid, server_ip);
    }

    // Set up web server
    server = new WebServer(80);

    // Root page — OTA upload form
    server->on("/", HTTP_GET, []() {
        server->sendHeader("Connection", "close");
        server->send(200, "text/html", OTA_HTML);
    });

    // Firmware upload handler
    server->on("/update", HTTP_POST,
        // Completion handler
        []() {
            server->sendHeader("Connection", "close");
            if (Update.hasError()) {
                server->send(500, "text/plain", Update.errorString());
            } else {
                server->send(200, "text/plain", "OK");
                delay(2000);
                ESP.restart();
            }
        },
        // Upload handler (receives chunks)
        []() {
            HTTPUpload& upload = server->upload();
            if (upload.status == UPLOAD_FILE_START) {
                // Validate device PIN before accepting upload
                const NodePrefs& p = prefs_get();
                String pin_arg = server->arg("pin");
                bool pin_valid = false;
                if (p.device_pin != 0 && pin_arg.length() > 0) {
                    pin_valid = ((uint32_t)pin_arg.toInt() == p.device_pin);
                } else if (p.device_pin == 0) {
                    // No PIN configured — accept upload (PIN field is ignored)
                    pin_valid = true;
                }
                if (!pin_valid) {
                    SIG_LOGW("[ota] Upload rejected: invalid PIN");
                    // Reject by setting a zero-length update so the write/end
                    // callbacks become no-ops. The completion handler will
                    // report the error.
                    Update.abort();
                    return;
                }
                SIG_LOGD("[ota] Update start: %s (%u bytes)",
                         upload.filename.c_str(), upload.totalSize);
                if (!Update.begin(upload.totalSize)) {
                    SIG_LOGW("[ota] Update.begin failed: %s", Update.errorString());
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    SIG_LOGW("[ota] Update.write failed: %s", Update.errorString());
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    SIG_LOGW("[ota] Update success: %u bytes", upload.totalSize);
                } else {
                    SIG_LOGW("[ota] Update.end failed: %s", Update.errorString());
                    Update.printError(Serial);
                }
            }
        }
    );

    server->onNotFound([]() {
        server->send(404, "text/plain", "Not found");
    });

    server->begin();
    active = true;
    return true;
}

void loop() {
    if (active && server) {
        server->handleClient();
    }
}

void stop() {
    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    active = false;
    server_ip[0] = '\0';
}

bool isActive() {
    return active;
}

const char* getIP() {
    return server_ip;
}

}  // namespace ota

// ── WiFi Site Survey ─────────────────────────────────────
namespace wifi_scan {

int scan(APInfo* out, int max_aps) {
    if (!out || max_aps <= 0) return 0;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);  // let radio settle

    int n = WiFi.scanNetworks(false, false);  // async=false, show_hidden=false
    if (n <= 0) {
        WiFi.mode(WIFI_OFF);
        return 0;
    }

    // Collect results, cap at documented and caller-provided limits.
    n = limitScanCount(n, max_aps);
    for (int i = 0; i < n; i++) {
        strncpy(out[i].ssid, WiFi.SSID(i).c_str(), sizeof(out[i].ssid) - 1);
        out[i].ssid[sizeof(out[i].ssid) - 1] = '\0';
        out[i].rssi      = WiFi.RSSI(i);
        out[i].channel   = WiFi.channel(i);
        out[i].encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    sortByRssi(out, n);

    WiFi.scanDelete();
    // Keep WiFi in STA mode so beginConnect() doesn't have to
    // re-initialize the MAC/BB/RF from cold (WIFI_OFF→STA has
    // a known ESP32-S3 power-up erratum that can cause silent
    // WiFi.begin() failure without sufficient settling time).
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    return n;
}

}  // namespace wifi_scan

// ── WiFi STA Client ──────────────────────────────────────
namespace wifi_sta {

static bool     s_connected   = false;
static int      s_rssi        = 0;
static Status   s_status      = Status::Idle;
static unsigned long s_conn_start = 0;

void beginConnect(const char* ssid, const char* password) {
    if (!ssid || !ssid[0]) {
        s_status = Status::Failed;
        return;
    }
    // WiFi should already be in STA mode from the scan, but ensure it.
    WiFi.mode(WIFI_STA);
    delay(100);  // let MAC/BB/RF settle after potential mode switch (ESP32-S3 erratum)
    WiFi.begin(ssid, password);
    s_status = Status::Connecting;
    s_conn_start = millis();
    s_connected = false;
    s_rssi = 0;
    SIG_LOGD("[wifi-sta] connecting to %s...", ssid);
}

Status getStatus() {
    if (s_status != Status::Connecting) return s_status;

    if (WiFi.status() == WL_CONNECTED) {
        s_status = Status::Connected;
        s_connected = true;
        s_rssi = WiFi.RSSI();
        SIG_LOGD("[wifi-sta] connected! (%d dBm)", s_rssi);
        return Status::Connected;
    }

    // Timeout after 15 seconds
    if (millis() - s_conn_start > 15000) {
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
        s_status = Status::Failed;
        s_connected = false;
        s_rssi = 0;
        SIG_LOGW("[wifi-sta] connection timed out");
        return Status::Failed;
    }

    return Status::Connecting;
}

void disconnect() {
    if (s_connected || s_status == Status::Connecting) {
        WiFi.disconnect();
    }
    WiFi.mode(WIFI_OFF);
    s_connected = false;
    s_rssi = 0;
    s_status = Status::Idle;
}

bool isConnected() {
    // Always check hardware — never rely solely on internal state.
    // External code (github_ota, WiFi.begin from another path) can
    // change the radio state without going through our state machine.
    bool hw = (WiFi.status() == WL_CONNECTED);
    if (hw) {
        s_connected = true;
        s_status = Status::Connected;
    } else if (s_connected || s_status == Status::Connected) {
        s_connected = false;
        s_rssi = 0;
        s_status = Status::Idle;
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
    // Always sync internal state with hardware (handles external
    // reconnections, e.g. github_ota reusing an existing STA link).
    bool hw = (WiFi.status() == WL_CONNECTED);

    if (hw && !s_connected && s_status != Status::Connecting) {
        // Hardware is connected but we didn't know — external reconnect.
        s_connected = true;
        s_rssi = WiFi.RSSI();
        s_status = Status::Connected;
        SIG_LOGD("[wifi-sta] reconnected externally (%d dBm)", s_rssi);
    } else if (!hw && (s_connected || s_status == Status::Connected)) {
        // Was connected, now not.
        s_connected = false;
        s_rssi = 0;
        s_status = Status::Idle;
        SIG_LOGW("[wifi-sta] disconnected");
    }

    // ── Auto-reconnect ──────────────────────────────────────
    // If we have saved credentials and aren't connected/connecting,
    // periodically attempt to reconnect (every 30 seconds).
    if (!s_connected && s_status != Status::Connecting) {
        static unsigned long last_reconnect = 0;
        if (millis() - last_reconnect > 30000) {
            last_reconnect = millis();
            const NodePrefs& p = sigurdos::prefs_get();
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
