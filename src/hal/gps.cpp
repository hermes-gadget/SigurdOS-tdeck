// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SlopOS-TDeck.
//
// SlopOS-TDeck is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SlopOS-TDeck is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SlopOS-TDeck.  If not, see <https://www.gnu.org/licenses/>.


#include "gps.h"
#include "tdeck_pins.h"
#include <Arduino.h>
#include <sys/time.h>
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
} gps;

static char nmea_buf[128];
static int  nmea_pos = 0;

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

// ── NMEA checksum validation ───────────────────────────────
// Returns true if the sentence has no checksum (backward compatible)
// or the checksum after '*' matches the XOR of bytes between '$' and '*'.
static bool nmea_checksum_valid(const char* sentence) {
    if (!sentence || sentence[0] != '$') return true;

    // Find the '*'
    const char* star = strchr(sentence, '*');
    if (!star || strlen(star) < 3) return true; // no checksum, backward compatible

    // Extract expected checksum (2 hex digits after *)
    char hex[3] = {star[1], star[2], 0};
    if (!isxdigit((unsigned char)hex[0]) || !isxdigit((unsigned char)hex[1]))
        return true; // malformed checksum, backward compatible
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
    if (!nmea_checksum_valid(sentence)) return;
    // Support both $GP (GPS-only) and $GN (multi-constellation) prefixes
    // L76K GNSS module on T-Deck outputs $GN by default
    if (strncmp(sentence, "$GPGGA,", 7) == 0 || strncmp(sentence, "$GNGGA,", 7) == 0) {
        parse_gga(sentence);
    } else if (strncmp(sentence, "$GPRMC,", 7) == 0 || strncmp(sentence, "$GNRMC,", 7) == 0) {
        parse_rmc(sentence);
    }
}

// ════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════

void slopos_gps_init() {
    Serial1.begin(GPS_BAUD_RATE, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    memset(&gps, 0, sizeof(gps));
    nmea_pos = 0;
    gps.initialized = true;
}

void slopos_gps_loop() {
    if (!gps.initialized) return;

    while (Serial1.available()) {
        char c = Serial1.read();

        if (c == '\n') {
            // End of NMEA sentence
            if (nmea_pos > 0) {
                nmea_buf[nmea_pos] = '\0';
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
                                             + (uint32_t)(30.6 * (m + 1)) + d - 719469);
                    uint32_t epoch = days * 86400UL + gps.hour * 3600UL
                                   + gps.minute * 60UL + gps.second;
                    // Set system RTC (available on both ESP32 and native builds)
                    struct timeval tv = { (time_t)epoch, 0 };
                    settimeofday(&tv, nullptr);
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
}

float    slopos_gps_latitude()     { return gps.latitude; }
float    slopos_gps_longitude()    { return gps.longitude; }
float    slopos_gps_altitude_m()   { return gps.altitude_m; }
float    slopos_gps_speed_kn()     { return gps.speed_kn; }
float    slopos_gps_heading()      { return gps.heading; }
uint8_t  slopos_gps_satellites()   { return gps.satellites; }
uint8_t  slopos_gps_fix_quality()  { return gps.fix_quality; }
bool     slopos_gps_has_fix()      { return gps.has_fix; }
uint8_t  slopos_gps_hour()         { return gps.hour; }
uint8_t  slopos_gps_minute()       { return gps.minute; }
uint8_t  slopos_gps_second()       { return gps.second; }
bool     slopos_gps_time_synced()  { return gps.time_synced; }
