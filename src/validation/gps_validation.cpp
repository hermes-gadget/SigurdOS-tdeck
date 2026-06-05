// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Minimal T-Deck GPS hardware validation firmware. This is intentionally
// separate from the release app so GPS UART bring-up can be proven over serial
// without display, mesh, SD, WiFi, or LVGL startup dependencies.

#include <Arduino.h>

#include "hal/gps.h"
#include "hal/tdeck_pins.h"

static uint32_t last_status_ms = 0;

static void emit_status()
{
    const bool has_fix = sigurdos_gps_has_fix();
    const bool has_location = has_fix
        && sigurdos_gps_latitude() != 0.0f
        && sigurdos_gps_longitude() != 0.0f;

    Serial.printf("@gps_hw|fix=%u|qual=%u|sv=%u|baud=%lu|chars=%lu|sent=%lu|valid=%lu|csfail=%lu|sw=%lu|loc=%u",
                  has_fix ? 1u : 0u,
                  (unsigned)sigurdos_gps_fix_quality(),
                  (unsigned)sigurdos_gps_satellites(),
                  (unsigned long)sigurdos_gps_active_baud(),
                  (unsigned long)sigurdos_gps_chars_processed(),
                  (unsigned long)sigurdos_gps_sentences_received(),
                  (unsigned long)sigurdos_gps_valid_sentences(),
                  (unsigned long)sigurdos_gps_checksum_failures(),
                  (unsigned long)sigurdos_gps_baud_switches(),
                  has_location ? 1u : 0u);

#if defined(SIGURDOS_GPS_VALIDATION_COORDS) && SIGURDOS_GPS_VALIDATION_COORDS
    if (has_fix) {
        Serial.printf("|lat=%.6f|lon=%.6f",
                      (double)sigurdos_gps_latitude(),
                      (double)sigurdos_gps_longitude());
    }
#endif

    Serial.println();
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

    sigurdos_gps_init();
    emit_status();
}

void loop()
{
    sigurdos_gps_loop();

    const uint32_t now = millis();
    if ((uint32_t)(now - last_status_ms) >= 1000) {
        last_status_ms = now;
        emit_status();
    }
}
