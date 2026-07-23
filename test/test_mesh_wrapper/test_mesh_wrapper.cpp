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
#include "mesh/radio_config_policy.h"
#include "mocks/mock_mesh_state.h"
#include "mesh/state_checkpoint.h"

namespace {

class MeshWrapperTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
        sigurdos::mesh::mock_reset_all();
    }
};

TEST_F(MeshWrapperTest, ResetRestoresEveryPublishedMockMetric) {
    sigurdos::mesh::mock_set_noise(-42);
    sigurdos::mesh::mock_set_rssi(-12);
    sigurdos::mesh::mock_set_snr(9.5f);
    sigurdos::mesh::mock_set_drop_count(7);
    sigurdos::mesh::setOwnName("changed");

    sigurdos::mesh::mock_reset_all();

    EXPECT_EQ(sigurdos::mesh::getNoiseFloor(), -120);
    EXPECT_EQ(sigurdos::mesh::getLastRSSI(), -80);
    EXPECT_FLOAT_EQ(sigurdos::mesh::getLastSNR(), 5.0f);
    EXPECT_EQ(sigurdos::mesh::getQueueDropCount(), 0U);
    EXPECT_STREQ(sigurdos::mesh::getOwnName(), "MockNode");
    EXPECT_EQ(sigurdos::mesh::getPacketLogCount(), 0);
    EXPECT_EQ(sigurdos::mesh::getAckCounter(), 0);
    EXPECT_EQ(sigurdos::mesh::getDeliveryCounter(), 0);
}

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

TEST(RadioConfigPolicy, AcceptsExactSx1262HardwareBoundaries) {
    using sigurdos::mesh::RadioConfig;
    using sigurdos::mesh::sx1262RadioConfigSupported;

    EXPECT_TRUE(sx1262RadioConfigSupported(
        RadioConfig(150.0f, 7.8f, 5, 5, -9, false)));
    EXPECT_TRUE(sx1262RadioConfigSupported(
        RadioConfig(960.0f, 500.0f, 12, 8, 22, true)));
}

TEST(RadioConfigPolicy, RejectsUnsupportedFrequencyAndDiscreteBandwidth) {
    using sigurdos::mesh::RadioConfig;
    using sigurdos::mesh::sx1262RadioConfigSupported;

    EXPECT_FALSE(sx1262RadioConfigSupported(
        RadioConfig(149.999f, 62.5f, 8, 5, 22, false)));
    EXPECT_FALSE(sx1262RadioConfigSupported(
        RadioConfig(960.001f, 62.5f, 8, 5, 22, false)));
    EXPECT_FALSE(sx1262RadioConfigSupported(
        RadioConfig(869.618f, 100.0f, 8, 5, 22, false)));
    EXPECT_FALSE(sigurdos::mesh::sx1262BandwidthSupportedHz(62501));
}

TEST(RadioConfigPolicy, EveryDriverSetterFailureIsPropagatedImmediately) {
    using sigurdos::mesh::RadioConfig;
    using sigurdos::mesh::RadioConfigField;
    const RadioConfig config(869.618f, 62.5f, 8, 5, 22, true);

    for (int failing_index = 0; failing_index < 6; ++failing_index) {
        int calls = 0;
        const int16_t injected_error = static_cast<int16_t>(-700 - failing_index);
        const sigurdos::mesh::RadioApplyResult result =
            sigurdos::mesh::applyRadioConfigFields(
                config,
                [&](RadioConfigField, const RadioConfig&) -> int16_t {
                    const int current = calls++;
                    return current == failing_index ? injected_error : 0;
                });

        EXPECT_FALSE(result.ok);
        EXPECT_EQ(injected_error, result.driver_error);
        EXPECT_EQ(failing_index + 1, calls);
        EXPECT_EQ(failing_index,
                  static_cast<int>(result.failed_field));
    }
}

TEST(RadioConfigPolicy, PartialFailureRestoresTheFullPreviousConfig) {
    using sigurdos::mesh::RadioApplyResult;
    using sigurdos::mesh::RadioConfig;
    using sigurdos::mesh::RadioConfigField;
    const RadioConfig previous(869.618f, 62.5f, 8, 5, 22, false);
    const RadioConfig requested(915.0f, 250.0f, 10, 6, 17, true);
    RadioConfig applied[2];
    int apply_count = 0;

    const sigurdos::mesh::RadioTransactionResult result =
        sigurdos::mesh::applyRadioConfigTransaction(
            requested, previous,
            [&](const RadioConfig& config) {
                applied[apply_count] = config;
                ++apply_count;
                if (apply_count == 1) {
                    return RadioApplyResult(
                        false, RadioConfigField::CodingRate, -706);
                }
                return RadioApplyResult(true);
            });

    ASSERT_EQ(2, apply_count);
    EXPECT_FALSE(result.applied);
    EXPECT_TRUE(result.rollback_attempted);
    EXPECT_TRUE(result.rollback_succeeded);
    EXPECT_FLOAT_EQ(requested.frequency_mhz, applied[0].frequency_mhz);
    EXPECT_FLOAT_EQ(previous.frequency_mhz, applied[1].frequency_mhz);
    EXPECT_FLOAT_EQ(previous.bandwidth_khz, applied[1].bandwidth_khz);
    EXPECT_EQ(previous.spreading_factor, applied[1].spreading_factor);
    EXPECT_EQ(previous.coding_rate, applied[1].coding_rate);
    EXPECT_EQ(previous.tx_power_dbm, applied[1].tx_power_dbm);
    EXPECT_EQ(previous.rx_boosted_gain, applied[1].rx_boosted_gain);
}

TEST(RadioConfigPolicy, RollbackFailureRemainsVisibleToCaller) {
    using sigurdos::mesh::RadioApplyResult;
    using sigurdos::mesh::RadioConfig;
    using sigurdos::mesh::RadioConfigField;
    int calls = 0;
    const auto result = sigurdos::mesh::applyRadioConfigTransaction(
        RadioConfig(915.0f, 125.0f, 9, 5, 20, false),
        RadioConfig(869.618f, 62.5f, 8, 5, 22, false),
        [&](const RadioConfig&) {
            ++calls;
            return RadioApplyResult(
                false, RadioConfigField::Frequency,
                static_cast<int16_t>(calls == 1 ? -707 : -706));
        });

    EXPECT_EQ(2, calls);
    EXPECT_FALSE(result.applied);
    EXPECT_TRUE(result.rollback_attempted);
    EXPECT_FALSE(result.rollback_succeeded);
    EXPECT_EQ(-706, result.rollback_result.driver_error);
}

TEST(RadioConfigPolicy, FailedHardwareApplyIsNeverPersisted) {
    using sigurdos::mesh::RadioConfig;
    bool commit_called = false;

    const auto result = sigurdos::mesh::applyAndCommitRadioConfig(
        RadioConfig(915.0f, 125.0f, 9, 5, 20, false),
        RadioConfig(869.618f, 62.5f, 8, 5, 22, false),
        [](const RadioConfig&) { return false; },
        [&]() { commit_called = true; return true; });

    EXPECT_FALSE(result.applied);
    EXPECT_FALSE(result.commit_attempted);
    EXPECT_FALSE(result.committed);
    EXPECT_FALSE(result.restore_attempted);
    EXPECT_FALSE(commit_called);
}

TEST(RadioConfigPolicy, FailedDurableCommitRestoresPreviousHardwareConfig) {
    using sigurdos::mesh::RadioConfig;
    const RadioConfig requested(915.0f, 250.0f, 10, 6, 17, true);
    const RadioConfig previous(869.618f, 62.5f, 8, 5, 22, false);
    RadioConfig applied[2];
    int apply_count = 0;

    const auto result = sigurdos::mesh::applyAndCommitRadioConfig(
        requested, previous,
        [&](const RadioConfig& config) {
            applied[apply_count++] = config;
            return true;
        },
        []() { return false; });

    ASSERT_EQ(2, apply_count);
    EXPECT_TRUE(result.applied);
    EXPECT_TRUE(result.commit_attempted);
    EXPECT_FALSE(result.committed);
    EXPECT_TRUE(result.restore_attempted);
    EXPECT_TRUE(result.restore_succeeded);
    EXPECT_FLOAT_EQ(requested.frequency_mhz, applied[0].frequency_mhz);
    EXPECT_FLOAT_EQ(previous.frequency_mhz, applied[1].frequency_mhz);
    EXPECT_FLOAT_EQ(previous.bandwidth_khz, applied[1].bandwidth_khz);
    EXPECT_EQ(previous.spreading_factor, applied[1].spreading_factor);
    EXPECT_EQ(previous.coding_rate, applied[1].coding_rate);
    EXPECT_EQ(previous.tx_power_dbm, applied[1].tx_power_dbm);
    EXPECT_EQ(previous.rx_boosted_gain, applied[1].rx_boosted_gain);
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

TEST_F(MeshWrapperTest, StateCheckpointAttemptsEveryStoreAfterFailure) {
    int calls = 0;
    const bool saved = sigurdos::mesh::detail::saveStateCheckpoint(
        true,
        [&]() { ++calls; return false; },
        [&]() { ++calls; return true; },
        [&]() { ++calls; return true; },
        [&]() { ++calls; return true; },
        [&]() { ++calls; return true; });
    EXPECT_FALSE(saved);
    EXPECT_EQ(calls, 5);
}

TEST_F(MeshWrapperTest, CleanRegionStoreIsNotRewritten) {
    int calls = 0;
    const bool saved = sigurdos::mesh::detail::saveStateCheckpoint(
        false,
        [&]() { ++calls; return true; },
        [&]() { ++calls; return true; },
        [&]() { ++calls; return true; },
        [&]() { ++calls; return true; },
        [&]() { ++calls; return false; });
    EXPECT_TRUE(saved);
    EXPECT_EQ(calls, 4);
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

TEST_F(MeshWrapperTest, ApplyFirstMeshMutationsRestoreSnapshotsOnWriteFailure) {
    enum class MutationPath {
        ChannelAdd,
        ChannelRemove,
        ChannelSlotSet,
        ContactAdd,
        ContactUpdate,
        ContactPathReset,
    };
    struct RuntimeState {
        int channel_count;
        int channel_value;
        int contact_count;
        int contact_value;
        bool contact_has_path;
    };
    struct Case {
        MutationPath path;
        const char* name;
    };
    static constexpr Case cases[] = {
        {MutationPath::ChannelAdd, "channel add"},
        {MutationPath::ChannelRemove, "channel remove"},
        {MutationPath::ChannelSlotSet, "companion channel set"},
        {MutationPath::ContactAdd, "contact add"},
        {MutationPath::ContactUpdate, "contact update"},
        {MutationPath::ContactPathReset, "contact path reset"},
    };

    for (const Case& test_case : cases) {
        SCOPED_TRACE(test_case.name);
        RuntimeState state{2, 17, 3, 29, true};
        const RuntimeState before = state;
        bool commit_attempted = false;
        bool rollback_called = false;

        const bool ok = sigurdos::mesh::detail::applyAndCommit(
            [&]() {
                switch (test_case.path) {
                    case MutationPath::ChannelAdd: ++state.channel_count; break;
                    case MutationPath::ChannelRemove: --state.channel_count; break;
                    case MutationPath::ChannelSlotSet: state.channel_value = 41; break;
                    case MutationPath::ContactAdd: ++state.contact_count; break;
                    case MutationPath::ContactUpdate: state.contact_value = 43; break;
                    case MutationPath::ContactPathReset:
                        state.contact_has_path = false;
                        break;
                }
                return true;
            },
            [&]() { commit_attempted = true; return false; },
            [&]() { state = before; rollback_called = true; });

        EXPECT_FALSE(ok);
        EXPECT_TRUE(commit_attempted);
        EXPECT_TRUE(rollback_called);
        EXPECT_EQ(before.channel_count, state.channel_count);
        EXPECT_EQ(before.channel_value, state.channel_value);
        EXPECT_EQ(before.contact_count, state.contact_count);
        EXPECT_EQ(before.contact_value, state.contact_value);
        EXPECT_EQ(before.contact_has_path, state.contact_has_path);
    }
}

TEST_F(MeshWrapperTest, FailedContactRemovalCommitDoesNotActivateRuntimeRemoval) {
    int contact_count = 3;
    bool removal_activated = false;

    const bool ok = sigurdos::mesh::detail::commitBeforeActivate(
        []() { return false; },
        [&]() { --contact_count; removal_activated = true; });

    EXPECT_FALSE(ok);
    EXPECT_FALSE(removal_activated);
    EXPECT_EQ(3, contact_count);
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
    using fn = bool (*)();
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
