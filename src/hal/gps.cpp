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
    bool     has_fix;
    bool     initialized;
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

static void parse_gga(const char* sentence) {
    // $GPGGA,time,lat,N,lon,E,q,sat,hdop,alt,M,...
    char buf[128];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';  // guarantee null termination
    char* token = strtok(buf, ",");

    // Time (field 1) — skip header (field 0) and get time
    token = strtok(nullptr, ",");
    if (token && strlen(token) >= 6) {
        char h[3] = {token[0], token[1], 0};
        char m[3] = {token[2], token[3], 0};
        char s[3] = {token[4], token[5], 0};
        gps.hour   = atoi(h);
        gps.minute = atoi(m);
        gps.second = atoi(s);
    }

    // Latitude + N/S (fields 2-3)
    char* lat_str = strtok(nullptr, ",");
    char* ns = strtok(nullptr, ",");
    if (lat_str && ns) gps.latitude = nmea_to_decimal(lat_str, ns[0]);

    // Longitude + E/W (fields 4-5)
    char* lon_str = strtok(nullptr, ",");
    char* ew = strtok(nullptr, ",");
    if (lon_str && ew) gps.longitude = nmea_to_decimal(lon_str, ew[0]);

    // Fix quality (field 6)
    token = strtok(nullptr, ",");
    if (token) gps.fix_quality = atoi(token);

    // Satellites (field 7)
    token = strtok(nullptr, ",");
    if (token) gps.satellites = atoi(token);

    // Skip HDOP (field 8)
    token = strtok(nullptr, ",");

    // Altitude (field 9)
    token = strtok(nullptr, ",");
    if (token) gps.altitude_m = strtof(token, nullptr);

    gps.has_fix = (gps.fix_quality > 0);
}

static void parse_rmc(const char* sentence) {
    // $GPRMC,time,status,lat,NS,lon,EW,speed,heading,...
    char buf[128];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';  // guarantee null termination
    char* token = strtok(buf, ",");

    // Fields 1-6: skip (time, status, lat, NS, lon, EW)
    // Field 7: speed (knots) — 7 skips from header (field 0)
    for (int i = 0; i < 7; i++) token = strtok(nullptr, ",");

    if (token) gps.speed_kn = strtof(token, nullptr);

    // Field 8: heading (degrees)
    token = strtok(nullptr, ",");
    if (token) gps.heading = strtof(token, nullptr);
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
