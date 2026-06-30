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


// Arduino mock state implementation
#include "Arduino.h"

namespace arduino_mock {
    unsigned long current_millis = 0;
    int pin_states[64] = {0};
    int pin_mode_calls[64] = {0};
    int forced_read_value[64] = {0};
    int forced_read_count[64] = {0};
    int analog_values[16] = {0};

    void reset() {
        current_millis = 0;
        for (int i = 0; i < 64; i++) {
            pin_states[i] = 0;
            pin_mode_calls[i] = 0;
            forced_read_value[i] = 0;
            forced_read_count[i] = 0;
        }
        for (int i = 0; i < 16; i++) analog_values[i] = 0;
    }
}

// Global objects
HardwareSerial Serial;
HardwareSerial Serial1;
TwoWire Wire;
SPIClass SPI;
