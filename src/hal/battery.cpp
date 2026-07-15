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


#include "battery.h"
#include "tdeck_pins.h"
#include <Arduino.h>

void sigurdos_battery_init()
{
    // ADC pin already configured by TDeckBoard::begin()
    // (analogReadResolution(12), adcAttachPin, pinMode)
}

uint16_t sigurdos_battery_mv()
{
    // Use analogReadMilliVolts for calibrated ADC readings.
    // ESP32-S3 applies efuse-based per-chip calibration, while analogRead()
    // returns uncalibrated raw counts with up to 10-24% error.
    const int samples = 8;
    uint32_t total_mv = 0;
    for (int i = 0; i < samples; i++) {
        total_mv += analogReadMilliVolts(PIN_BAT_ADC);
    }
    total_mv /= samples;
    // analogReadMilliVolts returns mV at the ADC pin.
    // T-Deck uses a 2:1 voltage divider (two 100k resistors),
    // so battery voltage = ADC pin voltage * 2.
    return (uint16_t)(total_mv * 2u);
}

uint8_t sigurdos_battery_pct()
{
    return sigurdos_battery_pct_from_mv(sigurdos_battery_mv());
}

bool sigurdos_battery_charging()
{
    // T-Deck doesn't have a dedicated charge-detect pin
    return false;
}
