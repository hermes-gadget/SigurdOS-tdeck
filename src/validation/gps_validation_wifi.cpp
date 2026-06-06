// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "validation/gps_validation_wifi.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <cstring>

#ifndef SIGURDOS_GPS_VALIDATION_WIFI_SSID
#define SIGURDOS_GPS_VALIDATION_WIFI_SSID ""
#endif
#ifndef SIGURDOS_GPS_VALIDATION_WIFI_PASS
#define SIGURDOS_GPS_VALIDATION_WIFI_PASS ""
#endif
#ifndef SIGURDOS_GPS_VALIDATION_WIFI_HOST
#define SIGURDOS_GPS_VALIDATION_WIFI_HOST ""
#endif
#ifndef SIGURDOS_GPS_VALIDATION_WIFI_PORT
#define SIGURDOS_GPS_VALIDATION_WIFI_PORT 8765
#endif
#ifndef SIGURDOS_GPS_VALIDATION_WIFI_PATH
#define SIGURDOS_GPS_VALIDATION_WIFI_PATH "/gps"
#endif

static uint32_t last_wifi_attempt_ms = 0;
static bool wifi_config_valid = false;

static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 15000;

static bool wifi_has_config()
{
    return strlen(SIGURDOS_GPS_VALIDATION_WIFI_SSID) > 0
        && strlen(SIGURDOS_GPS_VALIDATION_WIFI_HOST) > 0;
}

static void wifi_connect_once()
{
    if (!wifi_config_valid) return;
    last_wifi_attempt_ms = millis();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(SIGURDOS_GPS_VALIDATION_WIFI_SSID,
               SIGURDOS_GPS_VALIDATION_WIFI_PASS);

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED
           && (uint32_t)(millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[gps-validation] wifi=1 ip=%s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[gps-validation] wifi=0 state=disconnected");
    }
}

void sigurdos_gps_validation_wifi_init()
{
    wifi_config_valid = wifi_has_config();
    if (!wifi_config_valid) {
        Serial.println("[gps-validation] wifi=0 cfg=missing");
        return;
    }
    wifi_connect_once();
}

void sigurdos_gps_validation_wifi_service()
{
    if (!wifi_config_valid) return;
    if (WiFi.status() == WL_CONNECTED) return;

    const uint32_t now = millis();
    if ((uint32_t)(now - last_wifi_attempt_ms) < WIFI_RECONNECT_INTERVAL_MS) {
        return;
    }
    wifi_connect_once();
}

void sigurdos_gps_validation_wifi_post_status(const char* line)
{
    if (!wifi_config_valid || line == nullptr || WiFi.status() != WL_CONNECTED) {
        return;
    }

    WiFiClient client;
    client.setTimeout(2000);
    if (!client.connect(SIGURDOS_GPS_VALIDATION_WIFI_HOST,
                        SIGURDOS_GPS_VALIDATION_WIFI_PORT)) {
        return;
    }

    const size_t len = strlen(line);
    client.printf("POST %s HTTP/1.1\r\n", SIGURDOS_GPS_VALIDATION_WIFI_PATH);
    client.printf("Host: %s:%u\r\n",
                  SIGURDOS_GPS_VALIDATION_WIFI_HOST,
                  (unsigned)SIGURDOS_GPS_VALIDATION_WIFI_PORT);
    client.print("Content-Type: text/plain\r\n");
    client.print("Connection: close\r\n");
    client.printf("Content-Length: %u\r\n\r\n", (unsigned)len);
    client.write((const uint8_t*)line, len);
    client.flush();
    client.stop();
}
