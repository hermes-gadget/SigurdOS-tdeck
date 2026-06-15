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

#include "tdeck_pins.h"
#include <cstdint>

inline uint16_t sigurdos_battery_mv_from_adc_raw(uint16_t raw)
{
    return (uint16_t)((BAT_ADC_MULT * (float)raw) / 4096.0f);
}

inline uint8_t sigurdos_battery_pct_from_mv(uint16_t mv)
{
    // Li-ion discharge curve lookup table (approximated)
    // Maps millivolts to realistic percentage based on non-linear discharge
    static const uint16_t mv_table[] = {
        4200, 4150, 4110, 4080, 4050, 4020, 3990, 3960,
        3930, 3900, 3870, 3840, 3810, 3780, 3750, 3710,
        3680, 3650, 3610, 3570, 3530, 3480, 3430, 3370, 3300
    };
    static const uint8_t pct_table[] = {
        100, 95, 90, 85, 80, 75, 70, 65,
        60, 55, 50, 45, 40, 35, 30, 25,
        20, 15, 10, 7, 5, 3, 2, 1, 0
    };
    static constexpr int table_size = sizeof(mv_table) / sizeof(mv_table[0]);

    if (mv >= mv_table[0]) return 100;
    if (mv <= mv_table[table_size - 1]) return 0;

    for (int i = 0; i < table_size - 1; i++) {
        if (mv >= mv_table[i + 1]) {
            uint16_t range_mv = mv_table[i] - mv_table[i + 1];
            uint8_t range_pct = pct_table[i] - pct_table[i + 1];
            uint16_t offset_mv = mv_table[i] - mv;
            return pct_table[i] - (uint8_t)((uint32_t)offset_mv * range_pct / range_mv);
        }
    }
    return 0;
}

void sigurdos_battery_init();
uint16_t sigurdos_battery_mv();        // millivolts
uint8_t  sigurdos_battery_pct();       // 0-100
bool     sigurdos_battery_charging();
