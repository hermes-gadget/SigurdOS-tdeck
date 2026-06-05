// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.


#include "gps.h"
#include "tdeck_pins.h"
#include <Arduino.h>
#include <sys/time.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>

// ── GPS state ────────────────────────────────────────────
static struct GPSData {
    float    latitude;
    float    longitude;
    float    altitude_m;
    float    speed_kn;
    float    heading;
    uint8_t  satellites;
    uint8_t  fix_quality;
    uint8_t  hour, minute, second;
    uint16_t year;
    uint8_t  month, day;
    bool     has_fix;
    bool     initialized;
    bool     time_synced;
    uint32_t active_baud;
    uint32_t chars_processed;
    uint32_t sentences_received;
    uint32_t valid_sentences;
    uint32_t checksum_failures;
    uint32_t baud_switches;
    uint32_t last_baud_switch_ms;
    uint32_t gga_sentences;
    uint32_t rmc_sentences;
    uint32_t gsv_sentences;
    uint32_t gsa_sentences;
    uint8_t  satellites_in_view;
    uint8_t  fix_type;
    uint8_t  gsv_snr_max;
    uint8_t  gsv_snr_count;
    uint8_t  gsv_cycle_snr_max;
    uint8_t  gsv_cycle_snr_count;
    char     rmc_status;
} gps;

static char nmea_buf[128];
static int  nmea_pos = 0;
static uint8_t gps_baud_index = 0;

static constexpr uint32_t GPS_BAUD_PROBE_INTERVAL_MS = 3000;
static constexpr uint8_t GPS_BAUD_CANDIDATE_COUNT =
    (GPS_PRIMARY_BAUD_RATE == GPS_FALLBACK_BAUD_RATE) ? 1 : 2;

static uint32_t gps_baud_for_index(uint8_t index)
{
    return (index % GPS_BAUD_CANDIDATE_COUNT) == 0
        ? GPS_PRIMARY_BAUD_RATE
        : GPS_FALLBACK_BAUD_RATE;
}

static void gps_begin_uart(uint8_t index, bool count_switch)
{
    gps_baud_index = (uint8_t)(index % GPS_BAUD_CANDIDATE_COUNT);
    gps.active_baud = gps_baud_for_index(gps_baud_index);
    Serial1.begin(gps.active_baud, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    gps.last_baud_switch_ms = millis();
    nmea_pos = 0;
    if (count_switch) gps.baud_switches++;
}

static void gps_maybe_cycle_baud()
{
    if (GPS_BAUD_CANDIDATE_COUNT < 2) return;
    if (gps.valid_sentences > 0) return;

    uint32_t now = millis();
    if ((uint32_t)(now - gps.last_baud_switch_ms) < GPS_BAUD_PROBE_INTERVAL_MS) return;

    gps_begin_uart((uint8_t)(gps_baud_index + 1), true);
}

// ── Helpers ───────────────────────────────────────────────
static float nmea_to_decimal(const char* coord, char dir) {
    if (!coord || coord[0] == '\0') return 0.0f;
    float val = strtof(coord, nullptr);
    int degrees = (int)(val / 100.0f);
    float minutes = val - (degrees * 100.0f);
    float decimal = degrees + minutes / 60.0f;
    if (dir == 'S' || dir == 'W') decimal = -decimal;
    return decimal;
}

static bool nmea_field(const char* sentence, int index, char* out, size_t out_size) {
    if (!sentence || !out || out_size == 0 || index < 0) return false;

    int field = 0;
    const char* start = sentence;
    for (const char* p = sentence;; p++) {
        if (*p == ',' || *p == '*' || *p == '\0') {
            if (field == index) {
                size_t len = (size_t)(p - start);
                if (len >= out_size) len = out_size - 1;
                memcpy(out, start, len);
                out[len] = '\0';
                return true;
            }
            if (*p == '*' || *p == '\0') break;
            field++;
            start = p + 1;
        }
    }

    out[0] = '\0';
    return false;
}

static void parse_gga(const char* sentence) {
    // $GPGGA,time,lat,N,lon,E,q,sat,hdop,alt,M,...
    char field[20];

    // Time (field 1)
    if (nmea_field(sentence, 1, field, sizeof(field)) && strlen(field) >= 6) {
        char h[3] = {field[0], field[1], 0};
        char m[3] = {field[2], field[3], 0};
        char s[3] = {field[4], field[5], 0};
        gps.hour   = atoi(h);
        gps.minute = atoi(m);
        gps.second = atoi(s);
    }

    // Latitude + N/S (fields 2-3)
    char lat_str[20];
    char ns[4];
    if (nmea_field(sentence, 2, lat_str, sizeof(lat_str)) &&
        nmea_field(sentence, 3, ns, sizeof(ns))) {
        gps.latitude = nmea_to_decimal(lat_str, ns[0]);
    }

    // Longitude + E/W (fields 4-5)
    char lon_str[20];
    char ew[4];
    if (nmea_field(sentence, 4, lon_str, sizeof(lon_str)) &&
        nmea_field(sentence, 5, ew, sizeof(ew))) {
        gps.longitude = nmea_to_decimal(lon_str, ew[0]);
    }

    // Fix quality (field 6)
    if (nmea_field(sentence, 6, field, sizeof(field))) gps.fix_quality = atoi(field);

    // Satellites (field 7)
    if (nmea_field(sentence, 7, field, sizeof(field))) gps.satellites = atoi(field);

    // Altitude (field 9)
    if (nmea_field(sentence, 9, field, sizeof(field))) gps.altitude_m = strtof(field, nullptr);

    gps.has_fix = (gps.fix_quality > 0);
}

static void parse_rmc(const char* sentence) {
    // $GPRMC,time,status,lat,NS,lon,EW,speed,heading,date,...
    char field[20];

    // Field 2: status — 'A' = active (valid), 'V' = void (invalid)
    if (nmea_field(sentence, 2, field, sizeof(field))) {
        gps.rmc_status = field[0];
        if (field[0] != 'A') {
            // Status is void ('V') or unknown — skip updating speed, heading, date
            return;
        }
    } else {
        // No status field — skip
        return;
    }

    // Field 7: speed (knots)
    if (nmea_field(sentence, 7, field, sizeof(field))) gps.speed_kn = strtof(field, nullptr);

    // Field 8: heading (degrees)
    if (nmea_field(sentence, 8, field, sizeof(field))) gps.heading = strtof(field, nullptr);

    // Field 9: date (DDMMYY)
    if (nmea_field(sentence, 9, field, sizeof(field)) && strlen(field) >= 6) {
        char d[3] = {field[0], field[1], 0};
        char m[3] = {field[2], field[3], 0};
        char y[3] = {field[4], field[5], 0};
        gps.day   = atoi(d);
        gps.month = atoi(m);
        gps.year  = 2000 + atoi(y);
    }
}

// Acquisition diagnostics used by the validation harness.
static void parse_gsv(const char* sentence) {
    // $GPGSV,total_msgs,msg_num,satellites_in_view,sv,elev,az,snr,...
    char field[20];
    uint8_t msg_num = 0;

    if (nmea_field(sentence, 2, field, sizeof(field))) {
        msg_num = (uint8_t)atoi(field);
    }

    if (msg_num <= 1) {
        gps.gsv_cycle_snr_max = 0;
        gps.gsv_cycle_snr_count = 0;
    }

    if (nmea_field(sentence, 3, field, sizeof(field))) {
        gps.satellites_in_view = (uint8_t)atoi(field);
    }

    static constexpr int kSnrFields[] = {7, 11, 15, 19};
    for (int snr_field : kSnrFields) {
        if (!nmea_field(sentence, snr_field, field, sizeof(field))) continue;
        int snr = atoi(field);
        if (snr <= 0) continue;
        if (snr > 255) snr = 255;
        if ((uint8_t)snr > gps.gsv_cycle_snr_max) {
            gps.gsv_cycle_snr_max = (uint8_t)snr;
        }
        if (gps.gsv_cycle_snr_count < UINT8_MAX) {
            gps.gsv_cycle_snr_count++;
        }
    }

    gps.gsv_snr_max = gps.gsv_cycle_snr_max;
    gps.gsv_snr_count = gps.gsv_cycle_snr_count;
}

static void parse_gsa(const char* sentence) {
    // $GPGSA,mode,fix_type,... where fix_type is 1 none, 2 2D, 3 3D.
    char field[20];
    if (nmea_field(sentence, 2, field, sizeof(field))) {
        gps.fix_type = (uint8_t)atoi(field);
    }
}

// ── NMEA checksum validation ───────────────────────────────
// Returns false if the sentence has no '*' or too few hex chars after '*'.
// Only returns true when the checksum is explicitly present AND valid.
static bool nmea_checksum_valid(const char* sentence) {
    if (!sentence || sentence[0] != '$') return false;

    // Find the '*'
    const char* star = strchr(sentence, '*');
    if (!star) return false; // no '*' → malformed

    // Must have at least 2 hex chars after '*'
    if (strlen(star) < 3) return false;
    char hex[3] = {star[1], star[2], 0};
    if (!isxdigit((unsigned char)hex[0]) || !isxdigit((unsigned char)hex[1]))
        return false; // malformed checksum

    uint8_t expected = (uint8_t)strtol(hex, nullptr, 16);

    // Compute XOR of all bytes between '$' and '*'
    uint8_t computed = 0;
    for (const char* p = sentence + 1; p < star; p++) {
        computed ^= (uint8_t)(*p);
    }

    return computed == expected;
}

static void process_nmea(const char* sentence) {
    // Validate checksum — reject corrupted sentences
    if (!nmea_checksum_valid(sentence)) {
        gps.checksum_failures++;
        return;
    }

    gps.valid_sentences++;
    // Support both $GP (GPS-only) and $GN (multi-constellation) prefixes
    // L76K GNSS module on T-Deck outputs $GN by default
    if (strncmp(sentence, "$GPGGA,", 7) == 0 || strncmp(sentence, "$GNGGA,", 7) == 0) {
        gps.gga_sentences++;
        parse_gga(sentence);
    } else if (strncmp(sentence, "$GPRMC,", 7) == 0 || strncmp(sentence, "$GNRMC,", 7) == 0) {
        gps.rmc_sentences++;
        parse_rmc(sentence);
    } else if (strncmp(sentence, "$GPGSV,", 7) == 0 || strncmp(sentence, "$GNGSV,", 7) == 0) {
        gps.gsv_sentences++;
        parse_gsv(sentence);
    } else if (strncmp(sentence, "$GPGSA,", 7) == 0 || strncmp(sentence, "$GNGSA,", 7) == 0) {
        gps.gsa_sentences++;
        parse_gsa(sentence);
    }
}

// ════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════

void sigurdos_gps_init() {
    memset(&gps, 0, sizeof(gps));
    nmea_pos = 0;
    gps_baud_index = 0;
    gps_begin_uart(gps_baud_index, false);
    gps.initialized = true;
}

void sigurdos_gps_loop() {
    if (!gps.initialized) return;

    while (Serial1.available()) {
        int raw = Serial1.read();
        if (raw < 0) break;
        char c = (char)raw;
        gps.chars_processed++;

        if (c == '\n') {
            // End of NMEA sentence
            if (nmea_pos > 0) {
                nmea_buf[nmea_pos] = '\0';
                gps.sentences_received++;
                process_nmea(nmea_buf);
                nmea_pos = 0;

                // Auto-sync RTC from GPS on first valid fix with date
                if (!gps.time_synced && gps.has_fix && gps.year >= 2020) {
                    gps.time_synced = true;
                    // Compute Unix epoch from GPS date/time
                    int y = gps.year;
                    int m = gps.month;
                    int d = gps.day;
                    if (m <= 2) { y--; m += 12; }
                    // Days since 1970-01-01 (Gregorian calendar)
                    uint32_t days = (uint32_t)(365LL * y + y / 4 - y / 100 + y / 400
                                             - (365LL * 1970 + 1970 / 4 - 1970 / 100 + 1970 / 400)
                                             + (uint32_t)(30.6001 * (m + 1)) + d - 719469);
                    uint32_t epoch = days * 86400UL + gps.hour * 3600UL
                                   + gps.minute * 60UL + gps.second;
                    // Set system RTC where settimeofday is available.
                    // Native Windows builds do not expose it, and the tests only
                    // need to validate that the GPS parser reaches synced state.
#if !defined(_WIN32)
                    struct timeval tv = { static_cast<time_t>(epoch), 0 };
                    settimeofday(&tv, nullptr);
#endif
                }
            }
        } else if (c == '\r') {
            // Ignore carriage return
        } else if (c == '$' && nmea_pos > 0) {
            // New sentence starting before previous ended — flush
            nmea_pos = 0;
            nmea_buf[nmea_pos++] = c;
        } else if (nmea_pos < (int)(sizeof(nmea_buf) - 1)) {
            nmea_buf[nmea_pos++] = c;
        }
    }

    gps_maybe_cycle_baud();
}

float    sigurdos_gps_latitude()     { return gps.latitude; }
float    sigurdos_gps_longitude()    { return gps.longitude; }
float    sigurdos_gps_altitude_m()   { return gps.altitude_m; }
float    sigurdos_gps_speed_kn()     { return gps.speed_kn; }
float    sigurdos_gps_heading()      { return gps.heading; }
uint8_t  sigurdos_gps_satellites()   { return gps.satellites; }
uint8_t  sigurdos_gps_fix_quality()  { return gps.fix_quality; }
bool     sigurdos_gps_has_fix()      { return gps.has_fix; }
uint8_t  sigurdos_gps_hour()         { return gps.hour; }
uint8_t  sigurdos_gps_minute()       { return gps.minute; }
uint8_t  sigurdos_gps_second()       { return gps.second; }
bool     sigurdos_gps_time_synced()  { return gps.time_synced; }
uint32_t sigurdos_gps_active_baud()  { return gps.active_baud; }
uint32_t sigurdos_gps_chars_processed() { return gps.chars_processed; }
uint32_t sigurdos_gps_sentences_received() { return gps.sentences_received; }
uint32_t sigurdos_gps_valid_sentences() { return gps.valid_sentences; }
uint32_t sigurdos_gps_checksum_failures() { return gps.checksum_failures; }
uint32_t sigurdos_gps_baud_switches() { return gps.baud_switches; }
uint32_t sigurdos_gps_gga_sentences() { return gps.gga_sentences; }
uint32_t sigurdos_gps_rmc_sentences() { return gps.rmc_sentences; }
uint32_t sigurdos_gps_gsv_sentences() { return gps.gsv_sentences; }
uint32_t sigurdos_gps_gsa_sentences() { return gps.gsa_sentences; }
uint8_t  sigurdos_gps_satellites_in_view() { return gps.satellites_in_view; }
uint8_t  sigurdos_gps_fix_type() { return gps.fix_type; }
uint8_t  sigurdos_gps_gsv_snr_max() { return gps.gsv_snr_max; }
uint8_t  sigurdos_gps_gsv_snr_count() { return gps.gsv_snr_count; }
char     sigurdos_gps_rmc_status() { return gps.rmc_status; }
