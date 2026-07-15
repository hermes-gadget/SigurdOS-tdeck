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
#include <cstdio>

// Include our mesh wrapper header (uses mocks for MeshCore)
#include "mesh/mesh_wrapper.h"
#include "mesh/durable_mutation.h"

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
    (void)static_cast<init_fn>(sigurdos::mesh::init);
    SUCCEED();
}

TEST_F(MeshWrapperTest, LoopFunctionExists) {
    using loop_fn = void (*)();
    (void)static_cast<loop_fn>(sigurdos::mesh::loop);
    SUCCEED();
}

TEST_F(MeshWrapperTest, SendDirectSignature) {
    using send_fn = uint32_t (*)(const char*, const char*);
    (void)static_cast<send_fn>(sigurdos::mesh::sendMessage);
    SUCCEED();
}

TEST_F(MeshWrapperTest, SendChannelSignature) {
    using send_fn = bool (*)(const char*, const char*);
    (void)static_cast<send_fn>(sigurdos::mesh::sendChannelMessage);
    SUCCEED();
}

TEST_F(MeshWrapperTest, AddHashtagChannelSignature) {
    using add_fn = bool (*)(const char*);
    (void)static_cast<add_fn>(sigurdos::mesh::addHashtagChannel);
    SUCCEED();
}

TEST_F(MeshWrapperTest, RemoveChannelSignature) {
    using rm_fn = bool (*)(int);
    (void)static_cast<rm_fn>(sigurdos::mesh::removeChannel);
    SUCCEED();
}

TEST_F(MeshWrapperTest, GetNoiseFloorReturnsInt) {
    using fn = int (*)();
    (void)static_cast<fn>(sigurdos::mesh::getNoiseFloor);
    SUCCEED();
}

TEST_F(MeshWrapperTest, GetLastRSSIReturnsInt) {
    using fn = int (*)();
    (void)static_cast<fn>(sigurdos::mesh::getLastRSSI);
    SUCCEED();
}

TEST_F(MeshWrapperTest, GetLastSNRReturnsFloat) {
    using fn = float (*)();
    (void)static_cast<fn>(sigurdos::mesh::getLastSNR);
    SUCCEED();
}

TEST_F(MeshWrapperTest, GetUnreadCountReturnsInt) {
    using fn = int (*)();
    (void)static_cast<fn>(sigurdos::mesh::pendingMessageCount);
    SUCCEED();
}

TEST_F(MeshWrapperTest, PacketLogGenerationReturnsMonotonicCounterType) {
    using fn = uint32_t (*)();
    (void)static_cast<fn>(sigurdos::mesh::getPacketLogGeneration);
    (void)sigurdos::mesh::getPacketLogGeneration();
    SUCCEED();
}

TEST_F(MeshWrapperTest, ApplyRadioParamsAcceptsRxGainFlag) {
    using fn = bool (*)(float, float, int, int, int, bool);
    (void)static_cast<fn>(sigurdos::mesh::applyRadioParams);
    SUCCEED();
}

TEST_F(MeshWrapperTest, PersistenceApisReportCommitStatus) {
    using save_fn = bool (*)();
    using favourite_fn = bool (*)(const char*, bool);
    (void)static_cast<save_fn>(sigurdos::mesh::saveState);
    (void)static_cast<save_fn>(sigurdos::mesh::saveChannels);
    (void)static_cast<save_fn>(sigurdos::mesh::saveContacts);
    (void)static_cast<favourite_fn>(sigurdos::mesh::setContactFavourite);
    SUCCEED();
}

TEST_F(MeshWrapperTest, FailedDurableCommitRollsBackRuntimeMutation) {
    int visible_value = 10;
    bool rolled_back = false;

    bool ok = sigurdos::mesh::detail::applyAndCommit(
        [&]() { visible_value = 20; return true; },
        []() { return false; },
        [&]() { visible_value = 10; rolled_back = true; });

    EXPECT_FALSE(ok);
    EXPECT_TRUE(rolled_back);
    EXPECT_EQ(visible_value, 10);
}

TEST_F(MeshWrapperTest, SuccessfulDurableCommitKeepsRuntimeMutation) {
    int visible_value = 10;
    bool rolled_back = false;

    bool ok = sigurdos::mesh::detail::applyAndCommit(
        [&]() { visible_value = 20; return true; },
        []() { return true; },
        [&]() { rolled_back = true; });

    EXPECT_TRUE(ok);
    EXPECT_FALSE(rolled_back);
    EXPECT_EQ(visible_value, 20);
}

TEST_F(MeshWrapperTest, IdentityActivationRequiresSuccessfulCommit) {
    bool activated = false;
    EXPECT_FALSE(sigurdos::mesh::detail::commitBeforeActivate(
        []() { return false; }, [&]() { activated = true; }));
    EXPECT_FALSE(activated);

    EXPECT_TRUE(sigurdos::mesh::detail::commitBeforeActivate(
        []() { return true; }, [&]() { activated = true; }));
    EXPECT_TRUE(activated);
}

// ── Initial unread count is zero ────────────────────────
TEST_F(MeshWrapperTest, UnreadCountStartsAtZero) {
    EXPECT_EQ(sigurdos::mesh::pendingMessageCount(), 0);
}

// ── Noise floor is within realistic range ───────────────
TEST_F(MeshWrapperTest, NoiseFloorInRealisticRange) {
    // Even with mocks, the return should be in dBm range
    int nf = sigurdos::mesh::getNoiseFloor();
    EXPECT_GE(nf, -150);
    EXPECT_LE(nf, 0);
}

// ── Signal values are within physical limits ─────────────
TEST_F(MeshWrapperTest, RSSIInRealisticRange) {
    int rssi = sigurdos::mesh::getLastRSSI();
    EXPECT_GE(rssi, -160);
    EXPECT_LE(rssi, 0);
}

TEST_F(MeshWrapperTest, SNRInRealisticRange) {
    float snr = sigurdos::mesh::getLastSNR();
    EXPECT_GE(snr, -20.0f);
    EXPECT_LE(snr, 20.0f);
}

// ── Recent nodes returns valid count ────────────────────
TEST_F(MeshWrapperTest, GetRecentNodesReturnsNonNegative) {
    char names[4][32];
    int count = sigurdos::mesh::exportContacts(names, 4);
    EXPECT_GE(count, 0);
    EXPECT_LE(count, 4);
}

// ── ContactInfo struct includes type field ──────────────
TEST_F(MeshWrapperTest, ContactInfoHasTypeField) {
    sigurdos::mesh::ContactInfo ci;
    // type should be a uint8_t; verify it compiles and has a known default
    // In native test mode (no mesh init), exportContactsFull returns 0,
    // but the struct layout is what we're testing.
    EXPECT_EQ(sizeof(ci.type), sizeof(uint8_t));
}

TEST_F(MeshWrapperTest, ContactInfoHasLocationFields) {
    sigurdos::mesh::ContactInfo ci{};
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
    sigurdos::mesh::ContactInfo contacts[4];
    int count = sigurdos::mesh::exportContactsFull(contacts, 4);
    EXPECT_GE(count, 0);
}

// ── removeContact signature exists ──────────────────────
TEST_F(MeshWrapperTest, RemoveContactSignature) {
    using rm_fn = bool (*)(const char*);
    (void)static_cast<rm_fn>(sigurdos::mesh::removeContact);
    SUCCEED();
}

// ── resetPathTo signature exists ────────────────────────
TEST_F(MeshWrapperTest, ResetPathToSignature) {
    using fn = bool (*)(const char*);
    (void)static_cast<fn>(sigurdos::mesh::resetPathTo);
    SUCCEED();
}

TEST_F(MeshWrapperTest, FactoryResetSignature) {
    using fn = void (*)();
    (void)static_cast<fn>(sigurdos::mesh::factoryReset);
    SUCCEED();
}

// ── Node Stats API surface ──

TEST_F(MeshWrapperTest, NodeStatsCounterSignatures) {
    // All getter return types compile-time-checked
    using getU32 = uint32_t (*)();
    using getUL  = unsigned long (*)();
    using getInt = int (*)();

    (void)static_cast<getU32>(sigurdos::mesh::getNumSentFlood);
    (void)static_cast<getU32>(sigurdos::mesh::getNumSentDirect);
    (void)static_cast<getU32>(sigurdos::mesh::getNumRecvFlood);
    (void)static_cast<getU32>(sigurdos::mesh::getNumRecvDirect);
    (void)static_cast<getUL>(sigurdos::mesh::getTotalTxAirtimeMs);
    (void)static_cast<getUL>(sigurdos::mesh::getTotalRxAirtimeMs);
    (void)static_cast<getUL>(sigurdos::mesh::getRemainingTxBudget);
    (void)static_cast<getInt>(sigurdos::mesh::getAckCounter);
    (void)static_cast<getInt>(sigurdos::mesh::getDeliveryCounter);
    (void)static_cast<getU32>(sigurdos::mesh::getPendingAckDropCount);
    (void)static_cast<getU32>(sigurdos::mesh::getPendingAckExpiredCount);
    (void)static_cast<void (*)()>(sigurdos::mesh::resetPacketStats);

    SUCCEED();
}

// ACK tracking bridge

TEST_F(MeshWrapperTest, AckCounterIncrementsOnRegister) {
    int before = sigurdos::mesh::getAckCounter();

    sigurdos::mesh::registerAckedMessage("AckNodeCounter353", 353001);

    EXPECT_EQ(sigurdos::mesh::getAckCounter(), before + 1);
}

TEST_F(MeshWrapperTest, AckMatchingRequiresExactDestinationAndTimestamp) {
    sigurdos::mesh::registerAckedMessage("AckNodeExact353", 353010);

    EXPECT_TRUE(sigurdos::mesh::isMessageAcked("AckNodeExact353", 353010));
    EXPECT_FALSE(sigurdos::mesh::isMessageAcked("AckNodeExact353", 353011));
    EXPECT_FALSE(sigurdos::mesh::isMessageAcked("AckNodeOther353", 353010));
    EXPECT_FALSE(sigurdos::mesh::isMessageAcked(nullptr, 353010));
}

TEST_F(MeshWrapperTest, NullAckDestinationDoesNotIncrementCounter) {
    int before = sigurdos::mesh::getAckCounter();

    sigurdos::mesh::registerAckedMessage(nullptr, 353020);

    EXPECT_EQ(sigurdos::mesh::getAckCounter(), before);
    EXPECT_FALSE(sigurdos::mesh::isMessageAcked(nullptr, 353020));
}

TEST_F(MeshWrapperTest, AckRingEvictsOldestEntry) {
    static constexpr int kAckCapacity = 32;
    static constexpr uint32_t kBaseTimestamp = 353100;

    for (int i = 0; i <= kAckCapacity; i++) {
        char dest[32];
        snprintf(dest, sizeof(dest), "AckRing353_%02d", i);
        sigurdos::mesh::registerAckedMessage(dest, kBaseTimestamp + i);
    }

    EXPECT_FALSE(sigurdos::mesh::isMessageAcked("AckRing353_00", kBaseTimestamp));
    EXPECT_TRUE(sigurdos::mesh::isMessageAcked("AckRing353_01", kBaseTimestamp + 1));
    EXPECT_TRUE(sigurdos::mesh::isMessageAcked("AckRing353_32", kBaseTimestamp + 32));
}

TEST_F(MeshWrapperTest, ConfirmationLossMatchesExactMessageAndRefreshesDelivery) {
    int before = sigurdos::mesh::getDeliveryCounter();
    sigurdos::mesh::registerConfirmationLost("LostNode353", 353200);

    EXPECT_EQ(sigurdos::mesh::getDeliveryCounter(), before + 1);
    EXPECT_TRUE(sigurdos::mesh::isMessageConfirmationLost("LostNode353", 353200));
    EXPECT_FALSE(sigurdos::mesh::isMessageConfirmationLost("LostNode353", 353201));
    EXPECT_FALSE(sigurdos::mesh::isMessageConfirmationLost("OtherNode353", 353200));
}

TEST_F(MeshWrapperTest, AckAlsoRefreshesDeliveryCounter) {
    int before = sigurdos::mesh::getDeliveryCounter();
    sigurdos::mesh::registerAckedMessage("DeliveryNode353", 353210);
    EXPECT_EQ(sigurdos::mesh::getDeliveryCounter(), before + 1);
}

// Identity backup API surface

TEST_F(MeshWrapperTest, ExportIdentitySignature) {
    using export_fn = bool (*)(char*, size_t);
    (void)static_cast<export_fn>(sigurdos::mesh::exportIdentity);
    SUCCEED();
}

TEST_F(MeshWrapperTest, ImportIdentitySignature) {
    using import_fn = bool (*)(const char*);
    (void)static_cast<import_fn>(sigurdos::mesh::importIdentity);
    SUCCEED();
}

} // anonymous namespace
