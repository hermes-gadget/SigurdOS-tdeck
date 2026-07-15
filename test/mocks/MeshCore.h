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

// Mock ESP32-specific RadioLib and mesh headers for native testing

// MeshCore constants required by upstream headers
#define MAX_HASH_SIZE        8
#define PUB_KEY_SIZE        32
#define PRV_KEY_SIZE        64
#define SEED_SIZE           32
#define SIGNATURE_SIZE      64
#define MAX_ADVERT_DATA_SIZE  32
#define CIPHER_KEY_SIZE     16
#define CIPHER_BLOCK_SIZE   16
#define CIPHER_MAC_SIZE      2
#define PATH_HASH_SIZE       1
#define MAX_PACKET_PAYLOAD  184
#define MAX_GROUP_DATA_LENGTH  (MAX_PACKET_PAYLOAD - CIPHER_BLOCK_SIZE - 3)
#define MAX_PATH_SIZE        64
#define MAX_TRANS_UNIT      255
#define MAX_REGION_ENTRIES   32

// Stub for FILESYSTEM (fs::FS on ESP32)
class FS { public: bool mkdir(const char*) { return true; } };
typedef FS FILESYSTEM;

// RNG is defined by upstream Utils.h (included via RegionMap/Identity chain)
#include "Arduino.h"

// MeshCore base class mock
namespace mesh {
    class MainBoard {
    public:
        virtual ~MainBoard() {}
        virtual uint16_t getBattMilliVolts() = 0;
        virtual float getMCUTemperature() { return 45.0f; }
        virtual const char* getManufacturerName() const = 0;
        virtual uint8_t getStartupReason() const = 0;
        virtual void reboot() = 0;
        virtual void sleep(uint32_t) {}
    };

    class RTCClock {
    protected:
        uint32_t last_unique;
    public:
        RTCClock() : last_unique(0) {}
        virtual uint32_t getCurrentTime() = 0;
        virtual void setCurrentTime(uint32_t) = 0;
        virtual void tick() {}
    };

    class Radio {
    public:
        virtual ~Radio() {}
        virtual int recvRaw(uint8_t*, int) = 0;
        virtual uint32_t getEstAirtimeFor(int) = 0;
        virtual float packetScore(float, int) = 0;
        virtual bool startSendRaw(const uint8_t*, int) = 0;
        virtual bool isSendComplete() = 0;
        virtual void onSendFinished() = 0;
        virtual void loop() {}
        virtual int getNoiseFloor() const { return -110; }
        virtual bool isInRecvMode() const = 0;
    };
}

#define BD_STARTUP_NORMAL    0
#define BD_STARTUP_RX_PACKET 1
