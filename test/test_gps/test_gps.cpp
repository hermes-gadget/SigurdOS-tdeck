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


/**
 * Unit tests for GPS NMEA parser
 * Tests: GGA sentence parsing, RMC parsing, fix quality,
 *        invalid sentence handling, coordinate conversion, satellite count
 */
#include <gtest/gtest.h>
#include "Arduino.h"
#include "hal/gps.h"
#include "hal/gps_demand.h"
#include "hal/tdeck_pins.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>

namespace {

TEST(GPSDemandTest, BackgroundCadenceIsNeverEveryLoop) {
    EXPECT_EQ(sigurdos_gps_normalize_interval(0), 5);
    EXPECT_EQ(sigurdos_gps_normalize_interval(1), 5);
    EXPECT_EQ(sigurdos_gps_effective_poll_ms(true, 0, false, false), 5000u);
    EXPECT_EQ(sigurdos_gps_effective_poll_ms(false, 5, false, false), 0u);
}

TEST(GPSDemandTest, ForegroundDemandOverridesBackgroundSetting) {
    EXPECT_EQ(sigurdos_gps_effective_poll_ms(false, 60, true, false), 200u);
    EXPECT_EQ(sigurdos_gps_effective_poll_ms(false, 60, false, true), 200u);
    EXPECT_TRUE(sigurdos_gps_poll_due(100, 0, 200));
    EXPECT_FALSE(sigurdos_gps_poll_due(299, 100, 200));
    EXPECT_TRUE(sigurdos_gps_poll_due(300, 100, 200));
}

TEST(GPSDemandTest, OneShotSyncTimesOutWithoutEnablingBackgroundGps) {
    arduino_mock::reset();
    arduino_mock::current_millis = 100;
    sigurdos_gps_init();
    sigurdos_gps_start_time_sync(1000);
    EXPECT_EQ(sigurdos_gps_time_sync_status(), SigurdOSGpsSyncStatus::Waiting);
    arduino_mock::current_millis = 1100;
    sigurdos_gps_service(false, 5);
    EXPECT_EQ(sigurdos_gps_time_sync_status(), SigurdOSGpsSyncStatus::TimedOut);
}

// ── GPS fix data ──────────────────────────────────────────
struct GPSFix {
    float latitude;    // decimal degrees
    float longitude;
    float altitude_m;
    float speed_kn;
    float heading;
    uint8_t satellites;
    uint8_t fix_quality; // 0=none, 1=GPS, 2=DGPS, 4=RTK
    uint8_t hour, minute, second;
    bool valid;

    void clear() {
        latitude = 0; longitude = 0; altitude_m = 0;
        speed_kn = 0; heading = 0; satellites = 0;
        fix_quality = 0; hour = minute = second = 0;
        valid = false;
    }
};

// ── NMEA coordinate conversion ───────────────────────────
// NMEA format: ddmm.mmmm (GGA) or ddmm.mmmm (RMC)
// Convert to decimal degrees
float nmea_to_decimal(const char* coord, char dir) {
    if (!coord || coord[0] == '\0') return 0.0f;

    float val = strtof(coord, nullptr);
    int degrees = (int)(val / 100.0f);
    float minutes = val - (degrees * 100.0f);
    float decimal = degrees + minutes / 60.0f;

    if (dir == 'S' || dir == 'W') decimal = -decimal;
    return decimal;
}

// ── GGA parse ────────────────────────────────────────────
// $GPGGA,hhmmss.ss,ddmm.mmmm,N,dddmm.mmmm,W,q,sat,hdop,alt,M,...
bool parse_gga(const char* sentence, GPSFix* fix) {
    if (!sentence || !fix) return false;
    if (strncmp(sentence, "$GPGGA,", 7) != 0) return false;

    fix->clear();

    // Tokenize: skip header, parse fields
    char buf[128];
    strncpy(buf, sentence, sizeof(buf) - 1);

    char* token = strtok(buf, ",");
    int field = 0;

    // Field 1: time
    token = strtok(nullptr, ","); field++;
    if (token && strlen(token) >= 6) {
        char h[3] = {token[0], token[1], 0};
        char m[3] = {token[2], token[3], 0};
        char s[3] = {token[4], token[5], 0};
        fix->hour   = atoi(h);
        fix->minute = atoi(m);
        fix->second = atoi(s);
    }

    // Field 2: latitude
    token = strtok(nullptr, ","); field++;
    // Field 3: N/S
    char* ns = strtok(nullptr, ","); field++;
    if (token && ns) {
        fix->latitude = nmea_to_decimal(token, ns[0]);
    }

    // Field 4: longitude
    token = strtok(nullptr, ","); field++;
    // Field 5: E/W
    char* ew = strtok(nullptr, ","); field++;
    if (token && ew) {
        fix->longitude = nmea_to_decimal(token, ew[0]);
    }

    // Field 6: fix quality
    token = strtok(nullptr, ","); field++;
    if (token) fix->fix_quality = atoi(token);

    // Field 7: satellites
    token = strtok(nullptr, ","); field++;
    if (token) fix->satellites = atoi(token);

    // Field 9: altitude
    token = strtok(nullptr, ","); field++; // skip HDOP
    token = strtok(nullptr, ","); field++;
    if (token) fix->altitude_m = strtof(token, nullptr);

    fix->valid = (fix->fix_quality > 0);
    return true;
}

// ── RMC parse (time, date, speed, heading) ───────────────
bool parse_rmc(const char* sentence, GPSFix* fix) {
    if (!sentence || !fix) return false;
    if (strncmp(sentence, "$GPRMC,", 7) != 0) return false;

    char buf[128];
    strncpy(buf, sentence, sizeof(buf) - 1);
    char* token = strtok(buf, ",");

    // Skip to field 7 (speed)
    for (int i = 0; i < 7; i++) token = strtok(nullptr, ",");
    if (token) fix->speed_kn = strtof(token, nullptr);

    // Field 8: heading
    token = strtok(nullptr, ",");
    if (token) fix->heading = strtof(token, nullptr);

    return true;
}

// ════════════════════════════════════════════════════════
// TESTS
// ════════════════════════════════════════════════════════
class GPSTest : public ::testing::Test {
protected:
    GPSFix fix;

    void SetUp() override { fix.clear(); }
};

// ── NMEA coordinate conversion ───────────────────────────
TEST_F(GPSTest, NorthLatitudeConvertsPositive) {
    float lat = nmea_to_decimal("5149.1234", 'N');
    // 51° 49.1234' = 51 + 49.1234/60 ≈ 51.8187
    EXPECT_NEAR(lat, 51.8187f, 0.01f);
}

TEST_F(GPSTest, SouthLatitudeConvertsNegative) {
    float lat = nmea_to_decimal("3345.0000", 'S');
    // 33° 45.0000' S = -(33 + 45/60) = -33.75
    EXPECT_NEAR(lat, -33.75f, 0.01f);
}

TEST_F(GPSTest, EastLongitudeConvertsPositive) {
    float lon = nmea_to_decimal("00010.5000", 'E');
    // 0° 10.5000' E = 0 + 10.5/60 = 0.175
    EXPECT_NEAR(lon, 0.175f, 0.01f);
}

TEST_F(GPSTest, WestLongitudeConvertsNegative) {
    float lon = nmea_to_decimal("11815.0000", 'W');
    // 118° 15.0000' W = -(118 + 15/60) = -118.25
    EXPECT_NEAR(lon, -118.25f, 0.01f);
}

TEST_F(GPSTest, ZeroCoordinate) {
    float val = nmea_to_decimal("0000.0000", 'N');
    EXPECT_NEAR(val, 0.0f, 0.01f);
}

// ── GGA parsing ──────────────────────────────────────────
TEST_F(GPSTest, ValidGGASentenceParsesCorrectly) {
    // $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,...
    // 48° 07.038' N = 48 + 7.038/60 ≈ 48.1173
    // 11° 31.000' E = 11 + 31/60 ≈ 11.5167
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    bool ok = parse_gga(gga, &fix);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(fix.valid);
    EXPECT_NEAR(fix.latitude, 48.1173f, 0.01f);
    EXPECT_NEAR(fix.longitude, 11.5167f, 0.01f);
    EXPECT_EQ(fix.fix_quality, 1);
    EXPECT_EQ(fix.satellites, 8);
    EXPECT_NEAR(fix.altitude_m, 545.4f, 0.1f);
    EXPECT_EQ(fix.hour, 12);
    EXPECT_EQ(fix.minute, 35);
    EXPECT_EQ(fix.second, 19);
}

TEST_F(GPSTest, NoFixGGASentenceHasInvalidFlag) {
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,0,00,0.9,545.4,M,46.9,M,,*47";
    bool ok = parse_gga(gga, &fix);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(fix.valid);   // fix quality = 0
    EXPECT_EQ(fix.satellites, 0);
}

TEST_F(GPSTest, EmptyFieldsDefaultToZero) {
    const char* gga = "$GPGGA,,,,,,,,,,,,,";
    bool ok = parse_gga(gga, &fix);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(fix.valid);
    EXPECT_EQ(fix.fix_quality, 0);
}

TEST_F(GPSTest, NonGGASentenceIgnored) {
    const char* gsv = "$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,*75";
    bool ok = parse_gga(gsv, &fix);
    EXPECT_FALSE(ok);
}

TEST_F(GPSTest, RTKFixedQuality) {
    const char* gga = "$GPGGA,140000,5000.000,N,01000.000,E,4,20,0.5,100.0,M,50.0,M,,*00";
    bool ok = parse_gga(gga, &fix);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(fix.valid);
    EXPECT_EQ(fix.fix_quality, 4); // RTK fixed
}

// ── RMC parsing ──────────────────────────────────────────
TEST_F(GPSTest, RMCExtractsSpeedAndHeading) {
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    bool ok = parse_rmc(rmc, &fix);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(fix.speed_kn, 22.4f, 0.1f);
    EXPECT_NEAR(fix.heading, 84.4f, 0.1f);
}

TEST_F(GPSTest, NonRMCSentenceIgnoredByRMC) {
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,,*47";
    bool ok = parse_rmc(gga, &fix);
    EXPECT_FALSE(ok);
}

// ── NMEA checksum validation ────────────────────────────
// NMEA checksum: XOR of all bytes between '$' and '*' (exclusive),
// then '*' followed by 2 hex digits representing the XOR result.

static uint8_t compute_nmea_checksum(const char* sentence) {
    if (!sentence || sentence[0] != '$') return 0;

    uint8_t cs = 0;
    for (const char* p = sentence + 1; *p && *p != '*' && *p != '\0'; p++) {
        cs ^= (uint8_t)(*p);
    }
    return cs;
}

// Returns the expected checksum value parsed from *XX suffix, or -1 if absent.
static int parse_expected_checksum(const char* sentence) {
    if (!sentence) return -1;
    const char* star = strchr(sentence, '*');
    if (!star || strlen(star) < 3) return -1;
    if (!isxdigit((unsigned char)star[1]) || !isxdigit((unsigned char)star[2])) return -1;
    char hex[3] = {star[1], star[2], 0};
    return (int)strtol(hex, nullptr, 16);
}

static bool nmea_checksum_valid(const char* sentence) {
    if (!sentence || sentence[0] != '$') return false;
    const char* star = strchr(sentence, '*');
    if (!star) return false;
    if (strlen(star) < 3) return false;
    if (!isxdigit((unsigned char)star[1]) || !isxdigit((unsigned char)star[2])) return false;
    char hex[3] = {star[1], star[2], 0};
    uint8_t expected = (uint8_t)strtol(hex, nullptr, 16);
    return (int)compute_nmea_checksum(sentence) == (int)expected;
}

// ── NMEA checksum tests ──────────────────────────────────
TEST(NMEAChecksumTest, ValidChecksumPasses) {
    // Standard GGA sentence with correct *47
    EXPECT_TRUE(nmea_checksum_valid("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
}

TEST(NMEAChecksumTest, ValidRMCChecksumPasses) {
    // Standard RMC sentence with correct *6A
    EXPECT_TRUE(nmea_checksum_valid("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));
}

TEST(NMEAChecksumTest, CorruptedChecksumFails) {
    // Same sentence but torched checksum: *47 -> *FF
    EXPECT_FALSE(nmea_checksum_valid("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*FF"));
}

TEST(NMEAChecksumTest, CorruptedDataFailsValidation) {
    // Same sentence with corrupted lat (4807.038 -> 9999.999) but original *47 checksum
    EXPECT_FALSE(nmea_checksum_valid("$GPGGA,123519,9999.999,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
}

TEST(NMEAChecksumTest, NoChecksumRejected) {
    // GGA sentence without trailing checksum — now rejected as malformed
    EXPECT_FALSE(nmea_checksum_valid("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
}

TEST(NMEAChecksumTest, EmptyAsteriskRejected) {
    // Sentence ending with bare * — now rejected as malformed
    EXPECT_FALSE(nmea_checksum_valid("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,*"));
}

TEST(NMEAChecksumTest, NullInput) {
    EXPECT_FALSE(nmea_checksum_valid(nullptr));
}

TEST(NMEAChecksumTest, EmptyInput) {
    EXPECT_FALSE(nmea_checksum_valid(""));
}

TEST(NMEAChecksumTest, NonNMEAInput) {
    // Not a $ sentence — rejected as malformed
    EXPECT_FALSE(nmea_checksum_valid("garbage data"));
}

TEST(NMEAChecksumTest, BoldCharacterCorruption) {
    // GSA sentence: $GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39
    const char* gsa_ok = "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39";
    EXPECT_TRUE(nmea_checksum_valid(gsa_ok));
    // Corrupted: torched checksum *39 -> *AA
    EXPECT_FALSE(nmea_checksum_valid("$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*AA"));
}

TEST(NMEAChecksumTest, GNSSVariants) {
    // GNGGA (multi-constellation) with valid checksum
    EXPECT_TRUE(nmea_checksum_valid("$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*59"));
    // GNRMC with GN prefix
    EXPECT_TRUE(nmea_checksum_valid("$GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*74"));
}

TEST(NMEAChecksumTest, ChecksumHexCaseInsensitive) {
    // Lowercase checksum hex should also work
    const char* rmc_upper = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    const char* rmc_lower = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6a";
    EXPECT_TRUE(nmea_checksum_valid(rmc_upper));
    EXPECT_TRUE(nmea_checksum_valid(rmc_lower));
}

// ── Actual GPS parser integration ───────────────────────
class GPSIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
        Serial1.mock_reset();
        sigurdos_gps_init();
    }

    void feed(const char* sentence) {
        Serial1.mock_clear_rx();
        Serial1.mock_queue_rx(sentence);
        sigurdos_gps_loop();
    }

    void feed_body(const char* body) {
        uint8_t checksum = 0;
        for (const char* p = body; p && *p; p++) {
            checksum ^= (uint8_t)(*p);
        }

        char sentence[160];
        snprintf(sentence, sizeof(sentence), "$%s*%02X\n", body, checksum);
        feed(sentence);
    }
};

TEST_F(GPSIntegrationTest, ValidGGAReportsFixWithCoordinateDirections) {
    feed_body("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");

    EXPECT_TRUE(sigurdos_gps_has_fix());
    EXPECT_EQ(sigurdos_gps_fix_quality(), 1);
    EXPECT_NEAR(sigurdos_gps_latitude(), 48.1173f, 0.01f);
    EXPECT_NEAR(sigurdos_gps_longitude(), 11.5167f, 0.01f);
}

TEST_F(GPSIntegrationTest, InitUsesLilyGoGpsShieldUartContract) {
    EXPECT_TRUE(Serial1.mock_was_begun());
    EXPECT_EQ(Serial1.mock_last_baud(), GPS_PRIMARY_BAUD_RATE);
    EXPECT_EQ(Serial1.mock_last_config(), SERIAL_8N1);
    EXPECT_EQ(Serial1.mock_last_rx_pin(), PIN_GPS_RX);
    EXPECT_EQ(Serial1.mock_last_tx_pin(), PIN_GPS_TX);
    EXPECT_EQ(PIN_GPS_RX, 44);
    EXPECT_EQ(PIN_GPS_TX, 43);
    EXPECT_EQ(sigurdos_gps_active_baud(), GPS_PRIMARY_BAUD_RATE);
}

TEST_F(GPSIntegrationTest, FallsBackToAlternateBaudWhenNoValidNmeaArrives) {
    delay(3001);
    sigurdos_gps_loop();

    EXPECT_EQ(Serial1.mock_last_baud(), GPS_FALLBACK_BAUD_RATE);
    EXPECT_EQ(Serial1.mock_last_rx_pin(), PIN_GPS_RX);
    EXPECT_EQ(Serial1.mock_last_tx_pin(), PIN_GPS_TX);
    EXPECT_EQ(sigurdos_gps_active_baud(), GPS_FALLBACK_BAUD_RATE);
    EXPECT_EQ(sigurdos_gps_baud_switches(), 1U);
}

TEST_F(GPSIntegrationTest, ChecksumValidNmeaLocksDetectedBaud) {
    feed_body("GPGGA,123519,,,,,0,08,0.9,545.4,M,46.9,M,,");

    delay(3001);
    sigurdos_gps_loop();

    EXPECT_EQ(sigurdos_gps_sentences_received(), 1U);
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 1U);
    EXPECT_EQ(sigurdos_gps_checksum_failures(), 0U);
    EXPECT_EQ(sigurdos_gps_baud_switches(), 0U);
    EXPECT_EQ(sigurdos_gps_active_baud(), GPS_PRIMARY_BAUD_RATE);
    EXPECT_EQ(sigurdos_gps_gga_sentences(), 1U);
}

TEST_F(GPSIntegrationTest, ChecksumFailuresAreCountedForHardwareDebug) {
    feed("$GPGGA,123519,,,,,1,08,0.9,545.4,M,46.9,M,,*00\n");

    EXPECT_EQ(sigurdos_gps_sentences_received(), 1U);
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 0U);
    EXPECT_EQ(sigurdos_gps_checksum_failures(), 1U);
}

TEST_F(GPSIntegrationTest, GGAWithFixAndEmptyCoordinatesRejectsWholeUpdate) {
    feed_body("GPGGA,010203,4807.038,N,01131.000,E,1,04,0.9,12.5,M,46.9,M,,");
    feed_body("GPGGA,123519,,,,,1,08,0.9,545.4,M,46.9,M,,");

    EXPECT_EQ(sigurdos_gps_hour(), 1);
    EXPECT_EQ(sigurdos_gps_minute(), 2);
    EXPECT_EQ(sigurdos_gps_second(), 3);
    EXPECT_NEAR(sigurdos_gps_latitude(), 48.1173f, 0.01f);
    EXPECT_NEAR(sigurdos_gps_longitude(), 11.5167f, 0.01f);
    EXPECT_EQ(sigurdos_gps_fix_quality(), 1);
    EXPECT_EQ(sigurdos_gps_satellites(), 4);
    EXPECT_NEAR(sigurdos_gps_altitude_m(), 12.5f, 0.1f);
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 1U);
}

TEST_F(GPSIntegrationTest, GGAWithoutCoordinateDirectionsDoesNotReportFix) {
    feed_body("GPGGA,123520,4807.038,,01131.000,,1,08,0.9,545.4,M,46.9,M,,");

    EXPECT_FALSE(sigurdos_gps_has_fix());
    EXPECT_EQ(sigurdos_gps_fix_quality(), 0);
    EXPECT_FLOAT_EQ(sigurdos_gps_latitude(), 0.0f);
    EXPECT_FLOAT_EQ(sigurdos_gps_longitude(), 0.0f);
}

TEST_F(GPSIntegrationTest, GGAWithMalformedCoordinateDirectionsDoesNotReportFix) {
    feed_body("GPGGA,123519,4807.038,X,01131.000,Q,1,08,0.9,545.4,M,46.9,M,,");

    EXPECT_FALSE(sigurdos_gps_has_fix());
    EXPECT_EQ(sigurdos_gps_fix_quality(), 0);
    EXPECT_FLOAT_EQ(sigurdos_gps_latitude(), 0.0f);
    EXPECT_FLOAT_EQ(sigurdos_gps_longitude(), 0.0f);
}

TEST_F(GPSIntegrationTest, GGAWithMalformedCoordinateNumbersDoesNotReportFix) {
    feed_body("GPGGA,123519,4807.x,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");

    EXPECT_FALSE(sigurdos_gps_has_fix());
    EXPECT_EQ(sigurdos_gps_fix_quality(), 0);
    EXPECT_FLOAT_EQ(sigurdos_gps_latitude(), 0.0f);
    EXPECT_FLOAT_EQ(sigurdos_gps_longitude(), 0.0f);
}

TEST_F(GPSIntegrationTest, CoordinateBoundariesAreAccepted) {
    feed_body("GPGGA,235959.9,9000.000,N,18000.000,E,1,08,0.9,0.0,M,0.0,M,,");

    EXPECT_TRUE(sigurdos_gps_has_fix());
    EXPECT_FLOAT_EQ(sigurdos_gps_latitude(), 90.0f);
    EXPECT_FLOAT_EQ(sigurdos_gps_longitude(), 180.0f);
    EXPECT_EQ(sigurdos_gps_hour(), 23);
    EXPECT_EQ(sigurdos_gps_minute(), 59);
    EXPECT_EQ(sigurdos_gps_second(), 59);

    feed_body("GPGGA,000000,9000.000,S,18000.000,W,1,08,0.9,0.0,M,0.0,M,,");
    EXPECT_FLOAT_EQ(sigurdos_gps_latitude(), -90.0f);
    EXPECT_FLOAT_EQ(sigurdos_gps_longitude(), -180.0f);
}

TEST_F(GPSIntegrationTest, CoordinatesBeyondDegreesOrMinuteBoundariesAreRejected) {
    feed_body("GPGGA,120000,9000.001,N,01131.000,E,1,08,0.9,0.0,M,0.0,M,,");
    feed_body("GPGGA,120000,4807.038,N,18000.001,E,1,08,0.9,0.0,M,0.0,M,,");
    feed_body("GPGGA,120000,4860.000,N,01131.000,E,1,08,0.9,0.0,M,0.0,M,,");

    EXPECT_FALSE(sigurdos_gps_has_fix());
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 0U);
    EXPECT_EQ(sigurdos_gps_gga_sentences(), 0U);
}

TEST_F(GPSIntegrationTest, NonFiniteAndNonNmeaCoordinateSyntaxAreRejected) {
    feed_body("GPGGA,120000,nan,N,01131.000,E,1,08,0.9,0.0,M,0.0,M,,");
    feed_body("GPGGA,120000,4807.038,N,inf,E,1,08,0.9,0.0,M,0.0,M,,");
    feed_body("GPGGA,120000,+4807.038,N,01131.000,E,1,08,0.9,0.0,M,0.0,M,,");

    EXPECT_FALSE(sigurdos_gps_has_fix());
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 0U);
}

TEST_F(GPSIntegrationTest, InvalidTimeBoundariesRejectWholeGgaUpdate) {
    feed_body("GPGGA,240000,4807.038,N,01131.000,E,1,08,0.9,0.0,M,0.0,M,,");
    feed_body("GPGGA,126000,4807.038,N,01131.000,E,1,08,0.9,0.0,M,0.0,M,,");
    feed_body("GPGGA,125960,4807.038,N,01131.000,E,1,08,0.9,0.0,M,0.0,M,,");

    EXPECT_FALSE(sigurdos_gps_has_fix());
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 0U);
    EXPECT_EQ(sigurdos_gps_hour(), 0);
    EXPECT_EQ(sigurdos_gps_minute(), 0);
    EXPECT_EQ(sigurdos_gps_second(), 0);
}

TEST_F(GPSIntegrationTest, GregorianDateBoundariesAreEnforced) {
    feed_body("GPRMC,235959,A,4807.038,N,01131.000,E,0.0,359.9,290224,,,A");
    EXPECT_EQ(sigurdos_gps_rmc_sentences(), 1U);
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 1U);

    sigurdos_gps_init();
    feed_body("GPRMC,120000,A,4807.038,N,01131.000,E,0.0,0.0,290223,,,A");
    feed_body("GPRMC,120000,A,4807.038,N,01131.000,E,0.0,0.0,310424,,,A");
    feed_body("GPRMC,120000,A,4807.038,N,01131.000,E,0.0,0.0,011324,,,A");

    EXPECT_EQ(sigurdos_gps_rmc_sentences(), 0U);
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 0U);
}

TEST_F(GPSIntegrationTest, ChecksumMustTerminateSentenceExactly) {
    feed("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47garbage\n");

    EXPECT_FALSE(sigurdos_gps_has_fix());
    EXPECT_EQ(sigurdos_gps_checksum_failures(), 1U);
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 0U);
}

TEST_F(GPSIntegrationTest, OverlengthSentenceIsDiscardedUntilNewline) {
    std::string sentence = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    sentence.append(160, 'X');
    sentence.push_back('\n');
    feed(sentence.c_str());

    EXPECT_EQ(sigurdos_gps_sentences_received(), 1U);
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 0U);
    EXPECT_FALSE(sigurdos_gps_has_fix());

    feed_body("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    EXPECT_TRUE(sigurdos_gps_has_fix());
    EXPECT_EQ(sigurdos_gps_valid_sentences(), 1U);
}

TEST_F(GPSIntegrationTest, RMCWithEmptySpeedKeepsHeadingInFieldEight) {
    feed("$GPRMC,123519,A,4807.038,N,01131.000,E,,084.4,230394,003.1,W*40\n");

    EXPECT_FLOAT_EQ(sigurdos_gps_speed_kn(), 0.0f);
    EXPECT_NEAR(sigurdos_gps_heading(), 84.4f, 0.1f);
    EXPECT_EQ(sigurdos_gps_rmc_sentences(), 1U);
    EXPECT_EQ(sigurdos_gps_rmc_status(), 'A');
}

TEST_F(GPSIntegrationTest, ValidFixExposesUtcUntilClockAcceptsIt) {
    sigurdos_gps_start_time_sync();
    feed_body("GPRMC,123519,A,4807.038,N,01131.000,E,0.0,0.0,290224,,,A");
    feed_body("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");

    SigurdOSGpsUtcTime utc{};
    ASSERT_TRUE(sigurdos_gps_get_pending_time(&utc));
    EXPECT_EQ(utc.year, 2024);
    EXPECT_EQ(utc.month, 2);
    EXPECT_EQ(utc.day, 29);
    EXPECT_EQ(utc.hour, 12);
    EXPECT_EQ(utc.minute, 35);
    EXPECT_EQ(utc.second, 19);
    EXPECT_EQ(sigurdos_gps_epoch(), 1709210119u);
    EXPECT_FALSE(sigurdos_gps_time_synced());
    EXPECT_EQ(sigurdos_gps_time_sync_status(), SigurdOSGpsSyncStatus::Waiting);

    SigurdOSGpsUtcTime retry{};
    EXPECT_TRUE(sigurdos_gps_get_pending_time(&retry));
    EXPECT_EQ(retry.second, utc.second);

    sigurdos_gps_mark_time_synced();

    EXPECT_TRUE(sigurdos_gps_time_synced());
    EXPECT_EQ(sigurdos_gps_time_sync_status(), SigurdOSGpsSyncStatus::Success);
    EXPECT_FALSE(sigurdos_gps_get_pending_time(&utc));
}

TEST_F(GPSIntegrationTest, FixWithoutValidDateDoesNotOfferClockSync) {
    feed_body("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");

    SigurdOSGpsUtcTime utc{};
    EXPECT_FALSE(sigurdos_gps_get_pending_time(&utc));
    EXPECT_FALSE(sigurdos_gps_time_synced());
}

TEST_F(GPSIntegrationTest, GsvAndGsaDiagnosticsTrackSkyViewBeforeFix) {
    feed("$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74\n");
    feed("$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39\n");

    EXPECT_EQ(sigurdos_gps_valid_sentences(), 2U);
    EXPECT_EQ(sigurdos_gps_gsv_sentences(), 1U);
    EXPECT_EQ(sigurdos_gps_gsa_sentences(), 1U);
    EXPECT_EQ(sigurdos_gps_satellites_in_view(), 11);
    EXPECT_EQ(sigurdos_gps_fix_type(), 3);
    EXPECT_EQ(sigurdos_gps_gsv_snr_max(), 0);
    EXPECT_EQ(sigurdos_gps_gsv_snr_count(), 0);
    EXPECT_FALSE(sigurdos_gps_has_fix());
}

TEST_F(GPSIntegrationTest, GsvDiagnosticsAggregateNonZeroSnrAcrossMessageSet) {
    feed("$GPGSV,2,1,08,03,03,111,22,04,15,270,00,06,01,010,17,13,06,292,35*7D\n");
    feed("$GPGSV,2,2,08,16,45,123,42,19,10,050,,23,20,180,09,24,60,200,00*7C\n");

    EXPECT_EQ(sigurdos_gps_gsv_sentences(), 2U);
    EXPECT_EQ(sigurdos_gps_satellites_in_view(), 8);
    EXPECT_EQ(sigurdos_gps_gsv_snr_max(), 42);
    EXPECT_EQ(sigurdos_gps_gsv_snr_count(), 5);

    feed("$GPGSV,1,1,04,01,02,003,00,02,04,120,00,03,07,250,00,05,09,210,00*74\n");

    EXPECT_EQ(sigurdos_gps_gsv_sentences(), 3U);
    EXPECT_EQ(sigurdos_gps_satellites_in_view(), 4);
    EXPECT_EQ(sigurdos_gps_gsv_snr_max(), 0);
    EXPECT_EQ(sigurdos_gps_gsv_snr_count(), 0);
}

TEST_F(GPSIntegrationTest, GNVariantsUpdateAcquisitionDiagnostics) {
    feed("$GNGSV,1,1,03,01,02,003,00,02,04,120,00,03,07,250,00*52\n");
    feed("$GNGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99*2E\n");

    EXPECT_EQ(sigurdos_gps_gsv_sentences(), 1U);
    EXPECT_EQ(sigurdos_gps_gsa_sentences(), 1U);
    EXPECT_EQ(sigurdos_gps_satellites_in_view(), 3);
    EXPECT_EQ(sigurdos_gps_fix_type(), 1);
    EXPECT_EQ(sigurdos_gps_gsv_snr_max(), 0);
    EXPECT_EQ(sigurdos_gps_gsv_snr_count(), 0);
}

} // anonymous namespace
