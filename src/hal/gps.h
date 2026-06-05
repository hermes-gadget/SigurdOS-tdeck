#pragma once

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

#include <cstdint>

// Initialize GPS module on Serial1 with primary/fallback baud probing.
void sigurdos_gps_init();

// Call each frame to read and parse incoming NMEA data
void sigurdos_gps_loop();

// Current GPS fix data
float    sigurdos_gps_latitude();
float    sigurdos_gps_longitude();
float    sigurdos_gps_altitude_m();
float    sigurdos_gps_speed_kn();
float    sigurdos_gps_heading();
uint8_t  sigurdos_gps_satellites();
uint8_t  sigurdos_gps_fix_quality(); // 0=none, 1=GPS, 2=DGPS, 4=RTK
bool     sigurdos_gps_has_fix();

// Time from GPS (UTC)
uint8_t sigurdos_gps_hour();
uint8_t sigurdos_gps_minute();
uint8_t sigurdos_gps_second();
bool    sigurdos_gps_time_synced();

// UART/parser diagnostics for hardware validation and telemetry.
uint32_t sigurdos_gps_active_baud();
uint32_t sigurdos_gps_chars_processed();
uint32_t sigurdos_gps_sentences_received();
uint32_t sigurdos_gps_valid_sentences();
uint32_t sigurdos_gps_checksum_failures();
uint32_t sigurdos_gps_baud_switches();
uint32_t sigurdos_gps_gga_sentences();
uint32_t sigurdos_gps_rmc_sentences();
uint32_t sigurdos_gps_gsv_sentences();
uint32_t sigurdos_gps_gsa_sentences();
uint8_t  sigurdos_gps_satellites_in_view();
uint8_t  sigurdos_gps_fix_type(); // GSA fix type: 1=none, 2=2D, 3=3D
uint8_t  sigurdos_gps_gsv_snr_max(); // Max non-zero GSV SNR/CN0 in latest GSV set
uint8_t  sigurdos_gps_gsv_snr_count(); // Satellites with non-zero GSV SNR/CN0 in latest GSV set
char     sigurdos_gps_rmc_status(); // 'A'=active, 'V'=void, 0=unknown
