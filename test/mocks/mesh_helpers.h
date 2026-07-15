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


// Placeholder stubs for MeshCore helper headers needed during compilation
#pragma once
// mesh::AutoDiscoverRTCClock stub
#include "MeshCore.h"
namespace mesh {
    class ESP32RTCClock : public RTCClock {
    public:
        void begin() {}
        uint32_t getCurrentTime() override { return 0; }
        void setCurrentTime(uint32_t) override {}
    };
}
#define StdRNG int
