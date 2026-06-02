// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// WiFi OTA implementation — WebServer-based firmware upload.

#include "wifi_ota.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

namespace sigurdos {
namespace ota {

static WebServer* server = nullptr;
static bool active = false;
static char ap_ip[16] = "";

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

    // Start WiFi in AP mode
    WiFi.mode(WIFI_AP);
    if (password && password[0]) {
        WiFi.softAP(ssid, password);
    } else {
        WiFi.softAP(ssid);
    }

    IPAddress ip = WiFi.softAPIP();
    snprintf(ap_ip, sizeof(ap_ip), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

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
                delay(500);
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
    Serial.printf("[ota] WiFi AP started: %s @ %s\n", ssid, ap_ip);
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
    ap_ip[0] = '\0';
}

bool isActive() {
    return active;
}

const char* getIP() {
    return ap_ip;
}

}  // namespace ota
}  // namespace sigurdos
