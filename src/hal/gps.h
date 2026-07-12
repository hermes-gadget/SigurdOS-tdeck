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
struct SigurdOSGpsUtcTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

uint8_t sigurdos_gps_hour();
uint8_t sigurdos_gps_minute();
uint8_t sigurdos_gps_second();
bool    sigurdos_gps_time_synced();
uint32_t sigurdos_gps_epoch();

enum class SigurdOSGpsSyncStatus : uint8_t { Idle, Waiting, Success, TimedOut };

void sigurdos_gps_set_map_high_rate(bool enabled);
void sigurdos_gps_start_time_sync(uint32_t timeout_ms = 60000);
void sigurdos_gps_cancel_time_sync();
SigurdOSGpsSyncStatus sigurdos_gps_time_sync_status();
uint32_t sigurdos_gps_time_sync_remaining_ms();
void sigurdos_gps_service(bool background_enabled, uint16_t background_interval_s);
// Returns one valid, not-yet-applied GPS UTC value. The caller must route it
// through the system clock setter and mark it synced only after that succeeds.
bool    sigurdos_gps_get_pending_time(SigurdOSGpsUtcTime* out);
void    sigurdos_gps_mark_time_synced();