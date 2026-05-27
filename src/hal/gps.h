#pragma once

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

#include <cstdint>

// Initialize GPS module on Serial1 at 38400 baud
void slopos_gps_init();

// Call each frame to read and parse incoming NMEA data
void slopos_gps_loop();

// Current GPS fix data
float    slopos_gps_latitude();
float    slopos_gps_longitude();
float    slopos_gps_altitude_m();
float    slopos_gps_speed_kn();
float    slopos_gps_heading();
uint8_t  slopos_gps_satellites();
uint8_t  slopos_gps_fix_quality(); // 0=none, 1=GPS, 2=DGPS, 4=RTK
bool     slopos_gps_has_fix();

// Time from GPS (UTC)
uint8_t slopos_gps_hour();
uint8_t slopos_gps_minute();
uint8_t slopos_gps_second();
bool    slopos_gps_time_synced();
