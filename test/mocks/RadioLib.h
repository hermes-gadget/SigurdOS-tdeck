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

// Mock RadioLib.h for native PlatformIO testing

#include <cstdint>

class Module {
public:
    Module(int, int, int, int) {}
    Module(int, int, int, int, class SPIClass&) {}
};

class SX1262 : public Module {
public:
    using Module::Module;
    bool std_init(SPIClass* spi = nullptr) { return true; }
    int getRSSI() { return -80; }
    float getSNR() { return 5.0f; }
    int getNoiseFloor() { return -110; }
};

typedef SX1262 CustomSX1262;

class CustomSX1262Wrapper {
public:
    CustomSX1262Wrapper(SX1262& radio, class mesh::MainBoard& board) {}
    void loop() {}
    int getNoiseFloor() const { return -110; }
    float getLastRSSI() const { return -80.0f; }
    float getLastSNR() const { return 5.0f; }
    uint32_t getRngSeed() { return 0xDEADBEEF; }
};
