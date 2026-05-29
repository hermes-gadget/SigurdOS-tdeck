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


/**
 * Unit tests for mesh_wrapper API contract
 * Tests: function signatures, return value ranges, mock integration
 *
 * The actual MeshCore integration is tested on-hardware;
 * these tests validate the API surface and mock behavior.
 */
#include <gtest/gtest.h>
#include "Arduino.h"
#include <cstdint>

// Include our mesh wrapper header (uses mocks for MeshCore)
#include "mesh/mesh_wrapper.h"

namespace {

class MeshWrapperTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
    }
};

// ── API function signatures compile and link ────────────
TEST_F(MeshWrapperTest, InitFunctionExists) {
    // We can't call init() without hardware, but the function symbol exists.
    // Verify return type is bool (spiffs_ok parameter has default).
    using init_fn = bool (*)(bool);
    (void)static_cast<init_fn>(slopos::mesh::init);
    SUCCEED();
}

TEST_F(MeshWrapperTest, LoopFunctionExists) {
    using loop_fn = void (*)();
    (void)static_cast<loop_fn>(slopos::mesh::loop);
    SUCCEED();
}

TEST_F(MeshWrapperTest, SendDirectSignature) {
    using send_fn = bool (*)(const char*, const char*);
    (void)static_cast<send_fn>(slopos::mesh::sendMessage);
    SUCCEED();
}

TEST_F(MeshWrapperTest, SendChannelSignature) {
    using send_fn = bool (*)(const char*, const char*);
    (void)static_cast<send_fn>(slopos::mesh::sendChannelMessage);
    SUCCEED();
}

TEST_F(MeshWrapperTest, AddHashtagChannelSignature) {
    using add_fn = bool (*)(const char*);
    (void)static_cast<add_fn>(slopos::mesh::addHashtagChannel);
    SUCCEED();
}

TEST_F(MeshWrapperTest, RemoveChannelSignature) {
    using rm_fn = bool (*)(int);
    (void)static_cast<rm_fn>(slopos::mesh::removeChannel);
    SUCCEED();
}

TEST_F(MeshWrapperTest, GetNoiseFloorReturnsInt) {
    using fn = int (*)();
    (void)static_cast<fn>(slopos::mesh::getNoiseFloor);
    SUCCEED();
}

TEST_F(MeshWrapperTest, GetLastRSSIReturnsInt) {
    using fn = int (*)();
    (void)static_cast<fn>(slopos::mesh::getLastRSSI);
    SUCCEED();
}

TEST_F(MeshWrapperTest, GetLastSNRReturnsFloat) {
    using fn = float (*)();
    (void)static_cast<fn>(slopos::mesh::getLastSNR);
    SUCCEED();
}

TEST_F(MeshWrapperTest, GetUnreadCountReturnsInt) {
    using fn = int (*)();
    (void)static_cast<fn>(slopos::mesh::pendingMessageCount);
    SUCCEED();
}

// ── Initial unread count is zero ────────────────────────
TEST_F(MeshWrapperTest, UnreadCountStartsAtZero) {
    EXPECT_EQ(slopos::mesh::pendingMessageCount(), 0);
}

// ── Noise floor is within realistic range ───────────────
TEST_F(MeshWrapperTest, NoiseFloorInRealisticRange) {
    // Even with mocks, the return should be in dBm range
    int nf = slopos::mesh::getNoiseFloor();
    EXPECT_GE(nf, -150);
    EXPECT_LE(nf, 0);
}

// ── Signal values are within physical limits ─────────────
TEST_F(MeshWrapperTest, RSSIInRealisticRange) {
    int rssi = slopos::mesh::getLastRSSI();
    EXPECT_GE(rssi, -160);
    EXPECT_LE(rssi, 0);
}

TEST_F(MeshWrapperTest, SNRInRealisticRange) {
    float snr = slopos::mesh::getLastSNR();
    EXPECT_GE(snr, -20.0f);
    EXPECT_LE(snr, 20.0f);
}

// ── Recent nodes returns valid count ────────────────────
TEST_F(MeshWrapperTest, GetRecentNodesReturnsNonNegative) {
    char names[4][32];
    int count = slopos::mesh::exportContacts(names, 4);
    EXPECT_GE(count, 0);
    EXPECT_LE(count, 4);
}

// ── ContactInfo struct includes type field ──────────────
TEST_F(MeshWrapperTest, ContactInfoHasTypeField) {
    slopos::mesh::ContactInfo ci;
    // type should be a uint8_t; verify it compiles and has a known default
    // In native test mode (no mesh init), exportContactsFull returns 0,
    // but the struct layout is what we're testing.
    EXPECT_EQ(sizeof(ci.type), sizeof(uint8_t));
}

TEST_F(MeshWrapperTest, ContactInfoHasLocationFields) {
    slopos::mesh::ContactInfo ci{};
    ci.has_location = true;
    ci.latitude = 43.6532f;
    ci.longitude = -79.3832f;

    EXPECT_TRUE(ci.has_location);
    EXPECT_NEAR(ci.latitude, 43.6532f, 0.0001f);
    EXPECT_NEAR(ci.longitude, -79.3832f, 0.0001f);
}

// ── ADV_TYPE constants have expected values ────────────
TEST_F(MeshWrapperTest, AdvTypeConstants) {
    EXPECT_EQ(ADV_TYPE_NONE, 0);
    EXPECT_EQ(ADV_TYPE_CHAT, 1);
    EXPECT_EQ(ADV_TYPE_REPEATER, 2);
    EXPECT_EQ(ADV_TYPE_ROOM, 3);
    EXPECT_EQ(ADV_TYPE_SENSOR, 4);
}

// ── exportContactsFull returns valid ContactInfo data ───
TEST_F(MeshWrapperTest, ExportContactsFullReturnsNonNegative) {
    slopos::mesh::ContactInfo contacts[4];
    int count = slopos::mesh::exportContactsFull(contacts, 4);
    EXPECT_GE(count, 0);
}

// ── removeContact signature exists ──────────────────────
TEST_F(MeshWrapperTest, RemoveContactSignature) {
    using rm_fn = bool (*)(const char*);
    (void)static_cast<rm_fn>(slopos::mesh::removeContact);
    SUCCEED();
}

} // anonymous namespace
