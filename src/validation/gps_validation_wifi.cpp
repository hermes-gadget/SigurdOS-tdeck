// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "validation/gps_validation_wifi.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <cstdio>
#include <cstring>

#include "hal/gps.h"

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

namespace {

using sigurdos::gps_validation::ConnectPollResult;
using sigurdos::gps_validation::UploadQueue;
using sigurdos::gps_validation::pollConnect;
using sigurdos::gps_validation::reconnectDue;

constexpr uint32_t HTTP_CONNECT_BUDGET_MS = 25;
constexpr uint32_t HTTP_RESPONSE_TIMEOUT_MS = 2000;
constexpr size_t HTTP_WRITE_BUDGET_BYTES = 64;
constexpr size_t HTTP_READ_BUDGET_BYTES = 32;

enum class WifiState : uint8_t {
    Disabled,
    Connecting,
    Disconnected,
    Connected,
};

enum class UploadPhase : uint8_t {
    Idle,
    Header,
    Body,
    Response,
};

bool wifi_config_valid = false;
WifiState wifi_state = WifiState::Disabled;
uint32_t wifi_connect_started_ms = 0;
uint32_t last_wifi_attempt_ms = 0;

UploadQueue upload_queue;
WiFiClient upload_client;
UploadPhase upload_phase = UploadPhase::Idle;
char http_header[256] = {};
size_t http_header_len = 0;
size_t http_header_offset = 0;
size_t http_body_offset = 0;
char http_status_line[64] = {};
size_t http_status_len = 0;
uint32_t http_response_started_ms = 0;
uint32_t uploads_sent = 0;
uint32_t uploads_failed = 0;

bool wifi_has_config()
{
    return strlen(SIGURDOS_GPS_VALIDATION_WIFI_SSID) > 0
        && strlen(SIGURDOS_GPS_VALIDATION_WIFI_HOST) > 0;
}

void print_upload_outcome(const char* outcome, int http_status)
{
    Serial.printf(
        "[gps-validation] upload=%s http=%d queued=%u dropped=%lu sent=%lu failed=%lu\n",
        outcome,
        http_status,
        (unsigned)upload_queue.size(),
        (unsigned long)upload_queue.dropped(),
        (unsigned long)uploads_sent,
        (unsigned long)uploads_failed);
}

void reset_upload_transport(bool discard_record)
{
    upload_client.stop();
    if (discard_record) upload_queue.pop();
    upload_phase = UploadPhase::Idle;
    http_header_len = 0;
    http_header_offset = 0;
    http_body_offset = 0;
    http_status_len = 0;
    http_status_line[0] = '\0';
}

void finish_upload(const char* outcome, int http_status, bool success)
{
    if (success) {
        uploads_sent++;
    } else {
        uploads_failed++;
    }
    reset_upload_transport(true);
    print_upload_outcome(outcome, http_status);
}

void start_wifi_connect()
{
    if (!wifi_config_valid) return;

    last_wifi_attempt_ms = millis();
    wifi_connect_started_ms = last_wifi_attempt_ms;
    wifi_state = WifiState::Connecting;
    WiFi.begin(SIGURDOS_GPS_VALIDATION_WIFI_SSID,
               SIGURDOS_GPS_VALIDATION_WIFI_PASS);
    Serial.println("[gps-validation] wifi=0 state=connecting");
}

void service_wifi_state()
{
    const uint32_t now = millis();
    const bool connected = WiFi.status() == WL_CONNECTED;

    if (wifi_state == WifiState::Connected && !connected) {
        // Keep the current queue head for a retry after reconnecting.
        reset_upload_transport(false);
        wifi_state = WifiState::Disconnected;
        last_wifi_attempt_ms = now;
        Serial.println("[gps-validation] wifi=0 state=lost");
        return;
    }

    if (wifi_state == WifiState::Connecting) {
        const ConnectPollResult result = pollConnect(
            connected,
            static_cast<uint32_t>(now - wifi_connect_started_ms));
        if (result == ConnectPollResult::Connected) {
            wifi_state = WifiState::Connected;
            Serial.printf("[gps-validation] wifi=1 state=connected ip=%s\n",
                          WiFi.localIP().toString().c_str());
        } else if (result == ConnectPollResult::TimedOut) {
            WiFi.disconnect(false, false);
            wifi_state = WifiState::Disconnected;
            last_wifi_attempt_ms = now;
            Serial.println("[gps-validation] wifi=0 state=timeout");
        }
        return;
    }

    if (wifi_state == WifiState::Disconnected
        && reconnectDue(now, last_wifi_attempt_ms)) {
        start_wifi_connect();
    }
}

void begin_upload()
{
    const char* body = upload_queue.front();
    if (body == nullptr) return;

    // A numeric host avoids the ESP32 core's long DNS timeout. Even when a
    // hostname is configured, GPS is drained immediately before and after the
    // tightly bounded TCP connection attempt.
    sigurdos_gps_loop();
    const bool connected = upload_client.connect(
        SIGURDOS_GPS_VALIDATION_WIFI_HOST,
        SIGURDOS_GPS_VALIDATION_WIFI_PORT,
        HTTP_CONNECT_BUDGET_MS);
    sigurdos_gps_loop();
    if (!connected) {
        finish_upload("connect-failed", 0, false);
        return;
    }

    const size_t body_len = strlen(body);
    const int header_len = snprintf(
        http_header,
        sizeof(http_header),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n"
        "Content-Length: %u\r\n\r\n",
        SIGURDOS_GPS_VALIDATION_WIFI_PATH,
        SIGURDOS_GPS_VALIDATION_WIFI_HOST,
        (unsigned)SIGURDOS_GPS_VALIDATION_WIFI_PORT,
        (unsigned)body_len);
    if (header_len <= 0 || static_cast<size_t>(header_len) >= sizeof(http_header)) {
        finish_upload("request-too-large", 0, false);
        return;
    }

    http_header_len = static_cast<size_t>(header_len);
    http_header_offset = 0;
    http_body_offset = 0;
    upload_phase = UploadPhase::Header;
}

bool write_upload_chunk(const char* data, size_t len, size_t& offset)
{
    const size_t remaining = len - offset;
    const size_t chunk = remaining < HTTP_WRITE_BUDGET_BYTES
        ? remaining
        : HTTP_WRITE_BUDGET_BYTES;
    sigurdos_gps_loop();
    const size_t written = upload_client.write(
        reinterpret_cast<const uint8_t*>(data + offset), chunk);
    sigurdos_gps_loop();
    if (written == 0) return false;
    offset += written;
    return true;
}

void service_upload()
{
    if (upload_queue.size() == 0) return;

    if (upload_phase == UploadPhase::Idle) {
        begin_upload();
        return;
    }

    if (upload_phase == UploadPhase::Header) {
        if (!write_upload_chunk(http_header, http_header_len, http_header_offset)) {
            finish_upload("write-failed", 0, false);
            return;
        }
        if (http_header_offset == http_header_len) {
            upload_phase = UploadPhase::Body;
        }
        return;
    }

    const char* body = upload_queue.front();
    if (body == nullptr) {
        reset_upload_transport(false);
        return;
    }

    if (upload_phase == UploadPhase::Body) {
        const size_t body_len = strlen(body);
        if (!write_upload_chunk(body, body_len, http_body_offset)) {
            finish_upload("write-failed", 0, false);
            return;
        }
        if (http_body_offset == body_len) {
            upload_phase = UploadPhase::Response;
            http_response_started_ms = millis();
        }
        return;
    }

    size_t read_count = 0;
    while (upload_client.available() > 0
           && read_count < HTTP_READ_BUDGET_BYTES) {
        const int value = upload_client.read();
        if (value < 0) break;
        read_count++;

        const char c = static_cast<char>(value);
        if (c == '\n') {
            http_status_line[http_status_len] = '\0';
            int http_status = 0;
            if (sscanf(http_status_line, "HTTP/%*u.%*u %d", &http_status) != 1) {
                finish_upload("bad-response", 0, false);
            } else if (http_status >= 200 && http_status < 300) {
                finish_upload("ok", http_status, true);
            } else {
                finish_upload("http-failed", http_status, false);
            }
            return;
        }
        if (c == '\r') continue;
        if (http_status_len + 1 >= sizeof(http_status_line)) {
            finish_upload("bad-response", 0, false);
            return;
        }
        http_status_line[http_status_len++] = c;
    }

    const uint32_t now = millis();
    if ((!upload_client.connected() && upload_client.available() == 0)
        || static_cast<uint32_t>(now - http_response_started_ms)
            > HTTP_RESPONSE_TIMEOUT_MS) {
        finish_upload("response-timeout", 0, false);
    }
}

} // namespace

void sigurdos_gps_validation_wifi_init()
{
    wifi_config_valid = wifi_has_config();
    if (!wifi_config_valid) {
        wifi_state = WifiState::Disabled;
        Serial.println("[gps-validation] wifi=0 cfg=missing");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    start_wifi_connect();
}

void sigurdos_gps_validation_wifi_service()
{
    // This service is deliberately cooperative: every invocation drains the
    // UART, polls one WiFi state transition, performs at most one small HTTP
    // step, then drains the UART again.
    sigurdos_gps_loop();
    if (!wifi_config_valid) return;

    service_wifi_state();
    if (wifi_state == WifiState::Connected) service_upload();
    sigurdos_gps_loop();
}

void sigurdos_gps_validation_wifi_post_status(const char* line)
{
    if (!wifi_config_valid || line == nullptr) return;

    const uint32_t dropped_before = upload_queue.dropped();
    if (!upload_queue.push(line)
        && upload_queue.dropped() != dropped_before) {
        Serial.printf(
            "[gps-validation] upload=dropped queued=%u dropped=%lu\n",
            (unsigned)upload_queue.size(),
            (unsigned long)upload_queue.dropped());
    }
}
