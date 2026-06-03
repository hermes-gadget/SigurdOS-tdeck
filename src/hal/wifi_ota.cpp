// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// WiFi OTA implementation — WebServer-based firmware upload.

#include "wifi_ota.h"
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
input[type=file]{margin:20px 0;padding:10px;background:#1A1A2E;color:#00BFFF;border:2px solid #00BFFF}
input[type=submit]{padding:10px 30px;background:#00BFFF;color:#0F0F0F;border:none;font-weight:bold;cursor:pointer}
#progress{width:80%;height:20px;background:#1A1A2E;border:2px solid #00BFFF;margin:20px auto;display:none}
#bar{width:0;height:100%;background:#00BFFF}
#status{margin-top:10px;font-size:14px}
</style></head><body>
<h1>SigurdOS Firmware Update</h1>
<p>Select firmware.bin and click Update.</p>
<form method="POST" action="/update" enctype="multipart/form-data" id="otaform">
<input type="file" name="firmware" accept=".bin" required><br>
<input type="submit" value="Update">
</form>
<div id="progress"><div id="bar"></div></div>
<div id="status"></div>
<script>
document.getElementById('otaform').addEventListener('submit',function(e){
e.preventDefault();
var file=document.querySelector('input[type=file]').files[0];
if(!file)return;
var xhr=new XMLHttpRequest();
xhr.open('POST','/update',true);
xhr.upload.onprogress=function(e){
if(e.lengthComputable){
var pct=Math.round(e.loaded/e.total*100);
document.getElementById('progress').style.display='block';
document.getElementById('bar').style.width=pct+'%';
document.getElementById('status').textContent=pct+'%';
}
};
xhr.onload=function(){
if(xhr.status==200){
document.getElementById('status').textContent='Update OK — rebooting...';
}else{
document.getElementById('status').textContent='Update FAILED: '+xhr.responseText;
}
};
xhr.send(file);
});
</script></body></html>
)rawliteral";

bool start(const char* ssid, const char* password) {
    if (active) return true;

    IPAddress ip;
    if (wifi_sta::isConnected()) {
        // Already connected to a WiFi network — keep STA, bind on local IP
        ip = WiFi.localIP();
        snprintf(server_ip, sizeof(server_ip), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        Serial.printf("[ota] Using STA IP: %s\n", server_ip);
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
        Serial.printf("[ota] WiFi AP started: %s @ %s\n", ssid, server_ip);
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
                Serial.printf("[ota] Update start: %s (%u bytes)\n",
                              upload.filename.c_str(), upload.totalSize);
                if (!Update.begin(upload.totalSize)) {
                    Serial.printf("[ota] Update.begin failed: %s\n", Update.errorString());
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Serial.printf("[ota] Update.write failed: %s\n", Update.errorString());
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    Serial.printf("[ota] Update success: %u bytes\n", upload.totalSize);
                } else {
                    Serial.printf("[ota] Update.end failed: %s\n", Update.errorString());
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

    // Collect results, cap at buffer size
    if (n > max_aps) n = max_aps;
    for (int i = 0; i < n; i++) {
        strncpy(out[i].ssid, WiFi.SSID(i).c_str(), sizeof(out[i].ssid) - 1);
        out[i].ssid[sizeof(out[i].ssid) - 1] = '\0';
        out[i].rssi      = WiFi.RSSI(i);
        out[i].channel   = WiFi.channel(i);
        out[i].encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

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
    Serial.printf("[wifi-sta] connecting to %s...\n", ssid);
}

Status getStatus() {
    if (s_status != Status::Connecting) return s_status;

    if (WiFi.status() == WL_CONNECTED) {
        s_status = Status::Connected;
        s_connected = true;
        s_rssi = WiFi.RSSI();
        Serial.printf("[wifi-sta] connected! (%d dBm)\n", s_rssi);
        return Status::Connected;
    }

    // Timeout after 15 seconds
    if (millis() - s_conn_start > 15000) {
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
        s_status = Status::Failed;
        s_connected = false;
        s_rssi = 0;
        Serial.printf("[wifi-sta] connection timed out\n");
        return Status::Failed;
    }

    return Status::Connecting;
}

bool connect(const char* ssid, const char* password) {
    if (!ssid || !ssid[0]) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    // Wait up to 15 seconds for connection
    unsigned long start = millis();
    while (millis() - start < 15000) {
        if (WiFi.status() == WL_CONNECTED) {
            s_connected = true;
            s_rssi = WiFi.RSSI();
            Serial.printf("[wifi-sta] connected to %s (%d dBm)\n", ssid, s_rssi);
            return true;
        }
        delay(200);
    }

    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    s_connected = false;
    s_rssi = 0;
    Serial.printf("[wifi-sta] failed to connect to %s\n", ssid);
    return false;
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
        Serial.printf("[wifi-sta] reconnected externally (%d dBm)\n", s_rssi);
    } else if (!hw && (s_connected || s_status == Status::Connected)) {
        // Was connected, now not.
        s_connected = false;
        s_rssi = 0;
        s_status = Status::Idle;
        Serial.printf("[wifi-sta] disconnected\n");
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
                Serial.printf("[wifi-sta] auto-reconnecting to %s...\n",
                              p.wifi_ssid);
                beginConnect(p.wifi_ssid, p.wifi_password);
            }
        }
    }
}

}  // namespace wifi_sta
}  // namespace sigurdos
