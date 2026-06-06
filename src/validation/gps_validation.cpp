// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Minimal T-Deck GPS hardware validation firmware. This is intentionally
// separate from the release app so GPS UART bring-up can be proven over serial
// without display, mesh, SD, WiFi, or LVGL startup dependencies.

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SPIFFS.h>

#include "hal/gps.h"
#include "hal/tdeck_pins.h"
#if defined(SIGURDOS_GPS_VALIDATION_WIFI) && SIGURDOS_GPS_VALIDATION_WIFI
#include "validation/gps_validation_wifi.h"
#endif

static uint32_t last_status_ms = 0;
static uint32_t last_persist_ms = 0;
static bool spiffs_ready = false;
static bool fix_recorded = false;

static constexpr const char* GPS_LOG_PATH = "/gps_hw.txt";
static constexpr const char* GPS_NVS_NS = "gpsval";
static constexpr const char* GPS_NVS_MARKER = "gps-validation";

static void mark_boot()
{
    Preferences prefs;
    if (!prefs.begin(GPS_NVS_NS, false)) {
        Serial.println("[gps-validation] nvs=0");
        return;
    }

    uint32_t boot_count = prefs.getUInt("boot_count", 0);
    if (boot_count < UINT32_MAX) boot_count++;
    prefs.putUInt("boot_count", boot_count);
    prefs.putString("marker", GPS_NVS_MARKER);
    prefs.end();

    Serial.printf("[gps-validation] nvs=1 boot_count=%lu\n",
                  (unsigned long)boot_count);
}

static void append_log_line(const char* line)
{
    if (!spiffs_ready || line == nullptr) return;

    File f = SPIFFS.open(GPS_LOG_PATH, FILE_APPEND);
    if (!f) return;
    f.println(line);
    f.close();
}

static void build_status(char* out, size_t out_size)
{
    const bool has_fix = sigurdos_gps_has_fix();
    const bool has_location = has_fix
        && sigurdos_gps_latitude() != 0.0f
        && sigurdos_gps_longitude() != 0.0f;

    snprintf(out,
             out_size,
             "@gps_hw|ms=%lu|fix=%u|qual=%u|sv=%u|siv=%u|ft=%u|rmc=%c|snr=%u|snrc=%u|baud=%lu|chars=%lu|sent=%lu|valid=%lu|gga=%lu|rmc_s=%lu|gsv=%lu|gsa=%lu|csfail=%lu|sw=%lu|loc=%u",
             (unsigned long)millis(),
             has_fix ? 1u : 0u,
             (unsigned)sigurdos_gps_fix_quality(),
             (unsigned)sigurdos_gps_satellites(),
             (unsigned)sigurdos_gps_satellites_in_view(),
             (unsigned)sigurdos_gps_fix_type(),
             sigurdos_gps_rmc_status() ? sigurdos_gps_rmc_status() : '-',
             (unsigned)sigurdos_gps_gsv_snr_max(),
             (unsigned)sigurdos_gps_gsv_snr_count(),
             (unsigned long)sigurdos_gps_active_baud(),
             (unsigned long)sigurdos_gps_chars_processed(),
             (unsigned long)sigurdos_gps_sentences_received(),
             (unsigned long)sigurdos_gps_valid_sentences(),
             (unsigned long)sigurdos_gps_gga_sentences(),
             (unsigned long)sigurdos_gps_rmc_sentences(),
             (unsigned long)sigurdos_gps_gsv_sentences(),
             (unsigned long)sigurdos_gps_gsa_sentences(),
             (unsigned long)sigurdos_gps_checksum_failures(),
             (unsigned long)sigurdos_gps_baud_switches(),
             has_location ? 1u : 0u);

#if defined(SIGURDOS_GPS_VALIDATION_COORDS) && SIGURDOS_GPS_VALIDATION_COORDS
    if (has_fix && strlen(out) < out_size) {
        const size_t used = strlen(out);
        snprintf(out + used,
                 out_size - used,
                 "|lat=%.6f|lon=%.6f",
                 (double)sigurdos_gps_latitude(),
                 (double)sigurdos_gps_longitude());
    }
#endif
}

static void emit_status(bool persist)
{
    char line[256];
    build_status(line, sizeof(line));
    Serial.println(line);
    if (persist) {
        append_log_line(line);
#if defined(SIGURDOS_GPS_VALIDATION_WIFI) && SIGURDOS_GPS_VALIDATION_WIFI
        sigurdos_gps_validation_wifi_post_status(line);
#endif
    }
}

static void init_log()
{
    spiffs_ready = SPIFFS.begin(true);
    if (!spiffs_ready) {
        Serial.println("[gps-validation] spiffs=0");
        return;
    }

    SPIFFS.remove(GPS_LOG_PATH);
    append_log_line("[gps-validation] log-start");
    append_log_line("[gps-validation] coords=0");
    Serial.println("[gps-validation] spiffs=1 log=/gps_hw.txt");
}

void setup()
{
    delay(250);
    Serial.begin(115200);
    delay(500);

    Serial.println("[gps-validation] SigurdOS T-Deck GPS validation firmware");
    Serial.printf("[gps-validation] uart rx=%d tx=%d primary=%lu fallback=%lu\n",
                  PIN_GPS_RX,
                  PIN_GPS_TX,
                  (unsigned long)GPS_PRIMARY_BAUD_RATE,
                  (unsigned long)GPS_FALLBACK_BAUD_RATE);

    mark_boot();
    init_log();
    sigurdos_gps_init();
#if defined(SIGURDOS_GPS_VALIDATION_WIFI) && SIGURDOS_GPS_VALIDATION_WIFI
    sigurdos_gps_validation_wifi_init();
#endif
    emit_status(true);
}

void loop()
{
#if defined(SIGURDOS_GPS_VALIDATION_WIFI) && SIGURDOS_GPS_VALIDATION_WIFI
    sigurdos_gps_validation_wifi_service();
#endif
    sigurdos_gps_loop();

    const uint32_t now = millis();
    if ((uint32_t)(now - last_status_ms) >= 1000) {
        last_status_ms = now;
        const bool should_persist = (uint32_t)(now - last_persist_ms) >= 5000
            || (sigurdos_gps_has_fix() && !fix_recorded);
        if (should_persist) {
            last_persist_ms = now;
            if (sigurdos_gps_has_fix()) fix_recorded = true;
        }
        emit_status(should_persist);
    }
}
