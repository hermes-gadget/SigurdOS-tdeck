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
#include "gps_demand.h"
#include "tdeck_pins.h"
#include <Arduino.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>

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
    uint32_t epoch;
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
static bool nmea_discarding = false;
static uint8_t gps_baud_index = 0;
static bool gps_map_high_rate = false;
static SigurdOSGpsSyncStatus gps_sync_status = SigurdOSGpsSyncStatus::Idle;
static uint32_t gps_sync_started_ms = 0;
static uint32_t gps_sync_timeout_ms = 0;
static uint32_t gps_last_service_ms = 0;

static constexpr uint32_t GPS_BAUD_PROBE_INTERVAL_MS = 3000;
static constexpr uint32_t GPS_PROBE_TIMEOUT_MS = 60000;  // stop cycling after 60s with no valid sentences
static uint32_t gps_probe_start_ms = 0;
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
    nmea_discarding = false;
    if (count_switch) gps.baud_switches++;
}

static void gps_maybe_cycle_baud()
{
    if (GPS_BAUD_CANDIDATE_COUNT < 2) return;
    if (gps.valid_sentences > 0) return;

    uint32_t now = millis();

    // Latch probe start on first invocation
    if (gps_probe_start_ms == 0) gps_probe_start_ms = now;

    // Stop cycling after the probe timeout expires
    if ((uint32_t)(now - gps_probe_start_ms) > GPS_PROBE_TIMEOUT_MS) return;

    if ((uint32_t)(now - gps.last_baud_switch_ms) < GPS_BAUD_PROBE_INTERVAL_MS) return;

    gps_begin_uart((uint8_t)(gps_baud_index + 1), true);
}

// ── Helpers ───────────────────────────────────────────────
static bool nmea_digits(const char* value, size_t count) {
    if (!value) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!isdigit((unsigned char)value[i])) return false;
    }
    return true;
}

static bool nmea_unsigned(const char* value, unsigned max_value, unsigned* out) {
    if (!value || value[0] == '\0' || !out) return false;
    const size_t len = strlen(value);
    if (!nmea_digits(value, len)) return false;

    char* end = nullptr;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (!end || *end != '\0' || parsed > max_value) return false;
    *out = (unsigned)parsed;
    return true;
}

static bool nmea_float(const char* value, bool allow_negative, float* out) {
    if (!value || value[0] == '\0' || !out) return false;

    const char* p = value;
    if (*p == '-') {
        if (!allow_negative) return false;
        ++p;
    }
    const char* integer_start = p;
    while (isdigit((unsigned char)*p)) ++p;
    if (p == integer_start) return false;
    if (*p == '.') {
        ++p;
        const char* fraction_start = p;
        while (isdigit((unsigned char)*p)) ++p;
        if (p == fraction_start) return false;
    }
    if (*p != '\0') return false;

    char* end = nullptr;
    const float parsed = strtof(value, &end);
    if (!end || *end != '\0' || !std::isfinite(parsed)) return false;
    *out = parsed;
    return true;
}

static bool nmea_time(const char* value, uint8_t* hour, uint8_t* minute, uint8_t* second) {
    if (!value || !hour || !minute || !second || strlen(value) < 6) return false;
    if (!nmea_digits(value, 6)) return false;

    const char* suffix = value + 6;
    if (*suffix != '\0') {
        if (*suffix++ != '.' || *suffix == '\0') return false;
        const size_t fraction_len = strlen(suffix);
        if (!nmea_digits(suffix, fraction_len)) return false;
    }

    const unsigned parsed_hour = (unsigned)(value[0] - '0') * 10u + (unsigned)(value[1] - '0');
    const unsigned parsed_minute = (unsigned)(value[2] - '0') * 10u + (unsigned)(value[3] - '0');
    const unsigned parsed_second = (unsigned)(value[4] - '0') * 10u + (unsigned)(value[5] - '0');
    if (parsed_hour > 23 || parsed_minute > 59 || parsed_second > 59) return false;

    *hour = (uint8_t)parsed_hour;
    *minute = (uint8_t)parsed_minute;
    *second = (uint8_t)parsed_second;
    return true;
}

static bool gps_leap_year(uint16_t year) {
    return (year % 4u == 0u) && ((year % 100u != 0u) || (year % 400u == 0u));
}

static bool nmea_date(const char* value, uint16_t* year, uint8_t* month, uint8_t* day) {
    if (!value || !year || !month || !day || strlen(value) != 6 || !nmea_digits(value, 6)) {
        return false;
    }

    const unsigned parsed_day = (unsigned)(value[0] - '0') * 10u + (unsigned)(value[1] - '0');
    const unsigned parsed_month = (unsigned)(value[2] - '0') * 10u + (unsigned)(value[3] - '0');
    const uint16_t parsed_year = (uint16_t)(2000u +
        (unsigned)(value[4] - '0') * 10u + (unsigned)(value[5] - '0'));
    if (parsed_month < 1 || parsed_month > 12) return false;

    static constexpr uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    uint8_t max_day = days_per_month[parsed_month - 1];
    if (parsed_month == 2 && gps_leap_year(parsed_year)) max_day = 29;
    if (parsed_day < 1 || parsed_day > max_day) return false;

    *year = parsed_year;
    *month = (uint8_t)parsed_month;
    *day = (uint8_t)parsed_day;
    return true;
}

static bool nmea_to_decimal(const char* coord, char dir, bool latitude, float* out) {
    if (!coord || coord[0] == '\0' || !out) return false;
    if (latitude ? (dir != 'N' && dir != 'S') : (dir != 'E' && dir != 'W')) return false;

    const size_t degree_digits = latitude ? 2u : 3u;
    const size_t whole_digits = degree_digits + 2u;
    const char* dot = strchr(coord, '.');
    const size_t integer_len = dot ? (size_t)(dot - coord) : strlen(coord);
    if (integer_len != whole_digits || !nmea_digits(coord, integer_len)) return false;
    if (dot && (dot[1] == '\0' || !nmea_digits(dot + 1, strlen(dot + 1)))) return false;

    char* end = nullptr;
    const double val = strtod(coord, &end);
    if (!end || *end != '\0' || !std::isfinite(val)) return false;

    unsigned degrees = 0;
    for (size_t i = 0; i < degree_digits; ++i) {
        degrees = degrees * 10u + (unsigned)(coord[i] - '0');
    }
    const double minutes = val - (double)(degrees * 100u);
    const unsigned max_degrees = latitude ? 90u : 180u;
    if (minutes < 0.0f || minutes >= 60.0f || degrees > max_degrees ||
        (degrees == max_degrees && minutes != 0.0f)) {
        return false;
    }

    float decimal = (float)((double)degrees + minutes / 60.0);
    if (dir == 'S' || dir == 'W') decimal = -decimal;
    *out = decimal;
    return true;
}

static bool nmea_field(const char* sentence, int index, char* out, size_t out_size) {
    if (!sentence || !out || out_size == 0 || index < 0) return false;

    int field = 0;
    const char* start = sentence;
    for (const char* p = sentence;; p++) {
        if (*p == ',' || *p == '*' || *p == '\0') {
            if (field == index) {
                size_t len = (size_t)(p - start);
                if (len >= out_size) {
                    out[0] = '\0';
                    return false;
                }
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

static bool parse_gga(const char* sentence) {
    // $GPGGA,time,lat,N,lon,E,q,sat,hdop,alt,M,...
    char field[20];

    uint8_t parsed_hour = 0;
    uint8_t parsed_minute = 0;
    uint8_t parsed_second = 0;
    if (!nmea_field(sentence, 1, field, sizeof(field)) ||
        !nmea_time(field, &parsed_hour, &parsed_minute, &parsed_second)) return false;

    // Latitude + N/S (fields 2-3)
    char lat_str[20] = {};
    char ns[4] = {};
    float parsed_lat = 0.0f;

    // Longitude + E/W (fields 4-5)
    char lon_str[20] = {};
    char ew[4] = {};
    float parsed_lon = 0.0f;
    if (!nmea_field(sentence, 2, lat_str, sizeof(lat_str)) ||
        !nmea_field(sentence, 3, ns, sizeof(ns)) ||
        !nmea_field(sentence, 4, lon_str, sizeof(lon_str)) ||
        !nmea_field(sentence, 5, ew, sizeof(ew))) return false;

    const bool coordinates_empty = lat_str[0] == '\0' && ns[0] == '\0' &&
                                   lon_str[0] == '\0' && ew[0] == '\0';
    const bool coordinates_present = lat_str[0] != '\0' && ns[0] != '\0' && ns[1] == '\0' &&
                                     lon_str[0] != '\0' && ew[0] != '\0' && ew[1] == '\0';
    if (!coordinates_empty && (!coordinates_present ||
        !nmea_to_decimal(lat_str, ns[0], true, &parsed_lat) ||
        !nmea_to_decimal(lon_str, ew[0], false, &parsed_lon))) return false;

    // Fix quality (field 6)
    unsigned parsed_fix_quality = 0;
    if (!nmea_field(sentence, 6, field, sizeof(field)) ||
        !nmea_unsigned(field, 8, &parsed_fix_quality)) return false;
    if (parsed_fix_quality > 0 && coordinates_empty) return false;

    // Satellites (field 7)
    unsigned parsed_satellites = 0;
    if (!nmea_field(sentence, 7, field, sizeof(field)) ||
        !nmea_unsigned(field, 99, &parsed_satellites)) return false;

    // Altitude (field 9)
    float parsed_altitude = 0.0f;
    if (!nmea_field(sentence, 9, field, sizeof(field))) return false;
    if (field[0] != '\0' && !nmea_float(field, true, &parsed_altitude)) return false;

    gps.hour = parsed_hour;
    gps.minute = parsed_minute;
    gps.second = parsed_second;
    gps.latitude = parsed_lat;
    gps.longitude = parsed_lon;
    gps.fix_quality = (uint8_t)parsed_fix_quality;
    gps.satellites = (uint8_t)parsed_satellites;
    gps.altitude_m = parsed_altitude;
    gps.has_fix = parsed_fix_quality > 0;
    return true;
}

static bool parse_rmc(const char* sentence) {
    // $GPRMC,time,status,lat,NS,lon,EW,speed,heading,date,...
    char field[20];

    uint8_t parsed_hour = 0;
    uint8_t parsed_minute = 0;
    uint8_t parsed_second = 0;
    if (!nmea_field(sentence, 1, field, sizeof(field)) ||
        !nmea_time(field, &parsed_hour, &parsed_minute, &parsed_second)) return false;

    // Field 2: status — 'A' = active (valid), 'V' = void (invalid)
    if (!nmea_field(sentence, 2, field, sizeof(field)) || field[1] != '\0' ||
        (field[0] != 'A' && field[0] != 'V')) return false;
    const char parsed_status = field[0];

    char lat_str[20] = {};
    char ns[4] = {};
    char lon_str[20] = {};
    char ew[4] = {};
    if (!nmea_field(sentence, 3, lat_str, sizeof(lat_str)) ||
        !nmea_field(sentence, 4, ns, sizeof(ns)) ||
        !nmea_field(sentence, 5, lon_str, sizeof(lon_str)) ||
        !nmea_field(sentence, 6, ew, sizeof(ew))) return false;
    const bool coordinates_empty = lat_str[0] == '\0' && ns[0] == '\0' &&
                                   lon_str[0] == '\0' && ew[0] == '\0';
    float ignored_coordinate = 0.0f;
    if (!coordinates_empty && (ns[1] != '\0' || ew[1] != '\0' ||
        !nmea_to_decimal(lat_str, ns[0], true, &ignored_coordinate) ||
        !nmea_to_decimal(lon_str, ew[0], false, &ignored_coordinate))) return false;
    if (parsed_status == 'A' && coordinates_empty) return false;

    // Field 7: speed (knots)
    float parsed_speed = 0.0f;
    if (!nmea_field(sentence, 7, field, sizeof(field))) return false;
    if (field[0] != '\0' && !nmea_float(field, false, &parsed_speed)) return false;

    // Field 8: heading (degrees)
    float parsed_heading = 0.0f;
    if (!nmea_field(sentence, 8, field, sizeof(field))) return false;
    if (field[0] != '\0' &&
        (!nmea_float(field, false, &parsed_heading) || parsed_heading >= 360.0f)) return false;

    // Field 9: date (DDMMYY)
    uint16_t parsed_year = 0;
    uint8_t parsed_month = 0;
    uint8_t parsed_day = 0;
    if (!nmea_field(sentence, 9, field, sizeof(field)) ||
        !nmea_date(field, &parsed_year, &parsed_month, &parsed_day)) return false;

    gps.rmc_status = parsed_status;
    if (parsed_status == 'V') return true;

    gps.hour = parsed_hour;
    gps.minute = parsed_minute;
    gps.second = parsed_second;
    gps.speed_kn = parsed_speed;
    gps.heading = parsed_heading;
    gps.day = parsed_day;
    gps.month = parsed_month;
    gps.year = parsed_year;
    return true;
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

    // The two checksum digits must terminate the sentence exactly.
    if (strlen(star) != 3) return false;
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

    bool accepted = false;
    // Support both $GP (GPS-only) and $GN (multi-constellation) prefixes
    // L76K GNSS module on T-Deck outputs $GN by default
    if (strncmp(sentence, "$GPGGA,", 7) == 0 || strncmp(sentence, "$GNGGA,", 7) == 0) {
        accepted = parse_gga(sentence);
        if (accepted) gps.gga_sentences++;
    } else if (strncmp(sentence, "$GPRMC,", 7) == 0 || strncmp(sentence, "$GNRMC,", 7) == 0) {
        accepted = parse_rmc(sentence);
        if (accepted) gps.rmc_sentences++;
    } else if (strncmp(sentence, "$GPGSV,", 7) == 0 || strncmp(sentence, "$GNGSV,", 7) == 0) {
        gps.gsv_sentences++;
        parse_gsv(sentence);
        accepted = true;
    } else if (strncmp(sentence, "$GPGSA,", 7) == 0 || strncmp(sentence, "$GNGSA,", 7) == 0) {
        gps.gsa_sentences++;
        parse_gsa(sentence);
        accepted = true;
    }
    if (accepted) gps.valid_sentences++;
}

// ════════════════════════════════════════════════════════
// PUBLIC API
// ════════════════════════════════════════════════════════

void sigurdos_gps_init() {
    memset(&gps, 0, sizeof(gps));
    nmea_pos = 0;
    nmea_discarding = false;
    gps_baud_index = 0;
    gps_probe_start_ms = 0;
    gps_sync_status = SigurdOSGpsSyncStatus::Idle;
    gps_sync_started_ms = 0;
    gps_sync_timeout_ms = 0;
    gps_last_service_ms = 0;
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
            if (nmea_discarding) {
                nmea_discarding = false;
                nmea_pos = 0;
                gps.sentences_received++;
                continue;
            }
            // End of NMEA sentence
            if (nmea_pos > 0) {
                nmea_buf[nmea_pos] = '\0';
                gps.sentences_received++;
                process_nmea(nmea_buf);
                nmea_pos = 0;

                // Publish a pending GPS time for the clock owner. Parsing must
                // not consume it: main.cpp marks it synced only after the mesh
                // clock accepts the update.
                if (!gps.time_synced && gps.has_fix && gps.year >= 2020) {
                    // Compute Unix epoch from GPS date/time using Howard
                    // Hinnant's date algorithm — shared with the onboarding
                    // and manual time-set paths (mesh_wrapper.cpp makeEpoch)
                    // to prevent formula duplication and divergence.
                    int y = gps.year;
                    unsigned m = (unsigned)gps.month;
                    if (m < 3) { m += 12; y -= 1; }
                    int era = (y >= 0 ? y : y - 399) / 400;
                    unsigned yoe = (unsigned)(y - era * 400);
                    unsigned doy = (153u * (m - 3u) + 2u) / 5u + (unsigned)(gps.day - 1);
                    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
                    int days = (int)(era * 146097) + (int)doe - 719468;
                    uint32_t epoch = (uint32_t)days * 86400UL + gps.hour * 3600UL
                                   + gps.minute * 60UL + gps.second;
                    gps.epoch = epoch;
                }
            }
        } else if (nmea_discarding || c == '\r') {
            // Ignore carriage return
        } else if (c == '$' && nmea_pos > 0) {
            // New sentence starting before previous ended — flush
            nmea_pos = 0;
            nmea_buf[nmea_pos++] = c;
        } else if (nmea_pos < (int)(sizeof(nmea_buf) - 1)) {
            nmea_buf[nmea_pos++] = c;
        } else {
            // Never parse a truncated prefix as a complete NMEA sentence.
            nmea_pos = 0;
            nmea_discarding = true;
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
uint32_t sigurdos_gps_active_baud()       { return gps.active_baud; }
uint32_t sigurdos_gps_chars_processed()   { return gps.chars_processed; }
uint32_t sigurdos_gps_sentences_received(){ return gps.sentences_received; }
uint32_t sigurdos_gps_valid_sentences()   { return gps.valid_sentences; }
uint32_t sigurdos_gps_checksum_failures() { return gps.checksum_failures; }
uint32_t sigurdos_gps_baud_switches()     { return gps.baud_switches; }
uint32_t sigurdos_gps_gga_sentences()     { return gps.gga_sentences; }
uint32_t sigurdos_gps_rmc_sentences()     { return gps.rmc_sentences; }
uint32_t sigurdos_gps_gsv_sentences()     { return gps.gsv_sentences; }
uint32_t sigurdos_gps_gsa_sentences()     { return gps.gsa_sentences; }
uint8_t  sigurdos_gps_satellites_in_view(){ return gps.satellites_in_view; }
uint8_t  sigurdos_gps_fix_type()          { return gps.fix_type; }
uint8_t  sigurdos_gps_gsv_snr_max()       { return gps.gsv_snr_max; }
uint8_t  sigurdos_gps_gsv_snr_count()     { return gps.gsv_snr_count; }
char     sigurdos_gps_rmc_status()        { return gps.rmc_status; }
bool     sigurdos_gps_has_fix()      { return gps.has_fix; }
uint8_t  sigurdos_gps_hour()         { return gps.hour; }
uint8_t  sigurdos_gps_minute()       { return gps.minute; }
uint8_t  sigurdos_gps_second()       { return gps.second; }
bool     sigurdos_gps_time_synced()  { return gps.time_synced; }
uint32_t sigurdos_gps_epoch()        { return gps.epoch; }

void sigurdos_gps_set_map_high_rate(bool enabled) {
    gps_map_high_rate = enabled;
    if (enabled && !gps.initialized) sigurdos_gps_init();
}

void sigurdos_gps_start_time_sync(uint32_t timeout_ms) {
    if (timeout_ms == 0) timeout_ms = 60000;
    if (!gps.initialized) sigurdos_gps_init();
    gps.time_synced = false;
    gps.epoch = 0;
    gps_sync_started_ms = millis();
    gps_sync_timeout_ms = timeout_ms;
    gps_sync_status = SigurdOSGpsSyncStatus::Waiting;
}

void sigurdos_gps_cancel_time_sync() {
    if (gps_sync_status == SigurdOSGpsSyncStatus::Waiting) {
        gps_sync_status = SigurdOSGpsSyncStatus::Idle;
    }
}

SigurdOSGpsSyncStatus sigurdos_gps_time_sync_status() { return gps_sync_status; }

uint32_t sigurdos_gps_time_sync_remaining_ms() {
    if (gps_sync_status != SigurdOSGpsSyncStatus::Waiting) return 0;
    const uint32_t elapsed = (uint32_t)(millis() - gps_sync_started_ms);
    return elapsed >= gps_sync_timeout_ms ? 0 : gps_sync_timeout_ms - elapsed;
}

void sigurdos_gps_service(bool background_enabled, uint32_t background_interval_s) {
    const uint32_t now = millis();
    if (gps_sync_status == SigurdOSGpsSyncStatus::Waiting &&
        (uint32_t)(now - gps_sync_started_ms) >= gps_sync_timeout_ms) {
        gps_sync_status = SigurdOSGpsSyncStatus::TimedOut;
    }
    const uint32_t interval = sigurdos_gps_effective_poll_ms(
        background_enabled, background_interval_s, gps_map_high_rate,
        gps_sync_status == SigurdOSGpsSyncStatus::Waiting);
    if (interval == 0) return;
    if (!gps.initialized) sigurdos_gps_init();
    if (sigurdos_gps_poll_due(now, gps_last_service_ms, interval)) {
        gps_last_service_ms = now;
        sigurdos_gps_loop();
    }
}

bool sigurdos_gps_get_pending_time(SigurdOSGpsUtcTime* out) {
    if (!out || gps.time_synced || !gps.has_fix || gps.year < 2020 ||
        gps.month < 1 || gps.month > 12 || gps.day < 1 || gps.day > 31) {
        return false;
    }
    *out = {gps.year, gps.month, gps.day,
            gps.hour, gps.minute, gps.second};
    return true;
}
void sigurdos_gps_mark_time_synced() {
    gps.time_synced = true;
    if (gps_sync_status == SigurdOSGpsSyncStatus::Waiting) {
        gps_sync_status = SigurdOSGpsSyncStatus::Success;
    }
}
