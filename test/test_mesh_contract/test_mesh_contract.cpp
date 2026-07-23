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

#include <cstddef>
#include <type_traits>

#include <gtest/gtest.h>

#include "mesh/mesh_wrapper.h"
#include "mesh/login_session.h"
#include "mesh/login_response.h"
#include "mesh/ping_result_policy.h"
#include "mesh/public_channel.h"
#include "mesh/response_copy.h"
#include "mesh/client_repeat_policy.h"
#include "mesh/capacity_policy.h"
#include "mesh/scope_activation_policy.h"
#include "mesh/request_correlation.h"

namespace {

using sigurdos::mesh::ContactInfo;
using sigurdos::mesh::GroupDataType;
using sigurdos::mesh::MeshMessage;
using sigurdos::mesh::NodeStatus;
using sigurdos::mesh::PacketLogEntry;
using sigurdos::mesh::TelemetryItem;
using sigurdos::mesh::TelemetryResult;

TEST(MeshContractTest, AdvertTypesMatchMeshCoreCompanionValues) {
    EXPECT_EQ(ADV_TYPE_NONE, 0);
    EXPECT_EQ(ADV_TYPE_CHAT, 1);
    EXPECT_EQ(ADV_TYPE_REPEATER, 2);
    EXPECT_EQ(ADV_TYPE_ROOM, 3);
    EXPECT_EQ(ADV_TYPE_SENSOR, 4);
}

TEST(MeshContractTest, ClientRepeatMatchesUpstreamCompanionSemantics) {
    EXPECT_FALSE(sigurdos::mesh::clientRepeatAllowsForward(0));
    EXPECT_TRUE(sigurdos::mesh::clientRepeatAllowsForward(1));
    EXPECT_TRUE(sigurdos::mesh::clientRepeatAllowsForward(255));
}

TEST(MeshContractTest, SignedOutputCapacityRejectsNonPositiveValues) {
    size_t converted = 99;
    EXPECT_FALSE(sigurdos::mesh::positiveOutputCapacity(-1, converted));
    EXPECT_EQ(converted, 0U);
    EXPECT_FALSE(sigurdos::mesh::positiveOutputCapacity(0, converted));
    EXPECT_TRUE(sigurdos::mesh::positiveOutputCapacity(16, converted));
    EXPECT_EQ(converted, 16U);
}

TEST(MeshContractTest, ScopeActivationUsesCanonicalRegionGrammar) {
    uint8_t key[16]{};
    key[0] = 1;
    EXPECT_TRUE(sigurdos::mesh::scopeActivationInputsValid("#public", nullptr));
    EXPECT_TRUE(sigurdos::mesh::scopeActivationInputsValid("$private", key));
    EXPECT_FALSE(sigurdos::mesh::scopeActivationInputsValid("#bad/name", nullptr));
    EXPECT_FALSE(sigurdos::mesh::scopeActivationInputsValid("$private", nullptr));
    uint8_t zero_key[16]{};
    EXPECT_FALSE(sigurdos::mesh::scopeActivationInputsValid(
        "$private", zero_key));
}

TEST(MeshContractTest, PublicChannelDefaultsStayStable) {
    EXPECT_STREQ(sigurdos::mesh::PUBLIC_CHANNEL_NAME, "Public");
    EXPECT_STREQ(sigurdos::mesh::PUBLIC_CHANNEL_PSK_BASE64,
                 "izOH6cXN6mrJ5e26oRXNcg==");
    EXPECT_TRUE(sigurdos::mesh::isPublicChannelName("Public"));
    EXPECT_FALSE(sigurdos::mesh::isPublicChannelName("#public"));
    EXPECT_FALSE(sigurdos::mesh::isPublicChannelName(nullptr));
}

TEST(MeshContractTest, ContactPermissionsFitPackedFlagBits) {
    EXPECT_EQ(PERM_ACL_GUEST, 0);
    EXPECT_EQ(PERM_ACL_READ_ONLY, 1);
    EXPECT_EQ(PERM_ACL_READ_WRITE, 2);
    EXPECT_EQ(PERM_ACL_ADMIN, 3);
}

TEST(MeshContractTest, LoginStatusValuesStayStableForUiStateMachine) {
    EXPECT_EQ(LOGIN_STATUS_NONE, 0);
    EXPECT_EQ(LOGIN_STATUS_PENDING, 1);
    EXPECT_EQ(LOGIN_STATUS_OK, 2);
    EXPECT_EQ(LOGIN_STATUS_FAILED, 3);
    EXPECT_EQ(LOGIN_STATUS_TIMEOUT, 4);
    EXPECT_EQ(LOGIN_STATUS_DROPPED, 5);
}

TEST(MeshContractTest, LoginTimeoutUsesBoundsAndGrace) {
    using namespace sigurdos::mesh::login_session;
    EXPECT_EQ(normalizeTimeout(0), MIN_TIMEOUT_MS);
    EXPECT_EQ(normalizeTimeout(9000), 11000u);
    EXPECT_EQ(normalizeTimeout(MAX_TIMEOUT_MS), MAX_TIMEOUT_MS);
}

TEST(MeshContractTest, LoginDeadlineIsWrapSafe) {
    using namespace sigurdos::mesh::login_session;
    EXPECT_FALSE(elapsed(0xFFFFFFF0u, 32, 0x0000000Fu));
    EXPECT_TRUE(elapsed(0xFFFFFFF0u, 32, 0x00000010u));
}

TEST(MeshContractTest, LoginSessionTransitionsExposeTimeoutAndDrop) {
    using namespace sigurdos::mesh::login_session;
    EXPECT_EQ(evaluate(PENDING, 100, 1000, false, false, 1099), PENDING);
    EXPECT_EQ(evaluate(PENDING, 100, 1000, false, false, 1100), TIMED_OUT);
    EXPECT_EQ(evaluate(OK, 0, 0, true, true, 0), OK);
    EXPECT_EQ(evaluate(OK, 0, 0, true, false, 0), DROPPED);
    EXPECT_EQ(evaluate(OK, 0, 0, false, false, 0), OK);
}

TEST(MeshContractTest, LoginReservationPrefersFreeThenTerminalSlots) {
    using namespace sigurdos::mesh::login_session;
    struct Slot {
        bool in_use;
        uint8_t status;
    };

    Slot slots[] = {
        {true, FAILED},
        {true, OK},
        {false, NONE},
        {true, TIMED_OUT},
    };
    EXPECT_EQ(selectReservationSlot(slots, 4), 2);

    slots[2] = {true, PENDING};
    EXPECT_EQ(selectReservationSlot(slots, 4), 0);

    slots[0] = {true, OK};
    EXPECT_EQ(selectReservationSlot(slots, 4), 3);

    slots[3] = {true, DROPPED};
    EXPECT_EQ(selectReservationSlot(slots, 4), 3);
}

TEST(MeshContractTest, LoginReservationNeverEvictsPendingOrActiveSessions) {
    using namespace sigurdos::mesh::login_session;
    struct Slot {
        bool in_use;
        uint8_t status;
    };
    const Slot full[] = {
        {true, PENDING},
        {true, OK},
        {true, PENDING},
        {true, OK},
    };
    EXPECT_EQ(selectReservationSlot(full, 4), -1);
    EXPECT_EQ(selectReservationSlot<Slot>(nullptr, 4), -1);
}

TEST(MeshContractTest, RepeatedTerminalLoginsCannotExhaustTable) {
    using namespace sigurdos::mesh::login_session;
    struct Slot {
        bool in_use;
        uint8_t status;
    };
    Slot slots[] = {
        {true, FAILED},
        {true, TIMED_OUT},
        {true, DROPPED},
        {true, FAILED},
    };

    for (int attempt = 0; attempt < 100; ++attempt) {
        const int slot = selectReservationSlot(slots, 4);
        ASSERT_GE(slot, 0);
        slots[slot] = {true, PENDING};
        slots[slot].status = static_cast<uint8_t>(
            attempt % 2 == 0 ? FAILED : TIMED_OUT);
    }
}

TEST(MeshContractTest, LegacyLoginResponseIsAcceptedAtSixBytes) {
    const uint8_t response[6] = {1, 2, 3, 4, 'O', 'K'};
    const auto parsed = sigurdos::mesh::login_response::parse(
        response, sizeof(response));
    EXPECT_EQ(parsed.format,
              sigurdos::mesh::login_response::Format::LegacySuccess);
}

TEST(MeshContractTest, TruncatedCurrentLoginResponseIsRejected) {
    const uint8_t response[7] = {1, 2, 3, 4, 0, 2, 3};
    const auto parsed = sigurdos::mesh::login_response::parse(
        response, sizeof(response));
    EXPECT_EQ(parsed.format,
              sigurdos::mesh::login_response::Format::Unrecognized);
}

TEST(MeshContractTest, CurrentLoginRequiresAndParsesFullWireShape) {
    const uint8_t response[13] = {
        1, 2, 3, 4, 0, 4, 2, 0xA5, 9, 8, 7, 6, 11,
    };
    const auto parsed = sigurdos::mesh::login_response::parse(
        response, sizeof(response));
    EXPECT_EQ(parsed.format,
              sigurdos::mesh::login_response::Format::CurrentSuccess);
    EXPECT_EQ(parsed.keep_alive_secs, 64);
    EXPECT_EQ(parsed.permission, 2);
    EXPECT_EQ(parsed.acl_permissions, 0xA5);
    EXPECT_EQ(parsed.firmware_level, 11);
}

TEST(MeshContractTest, LoginFailureRequiresCurrentBaseFields) {
    const uint8_t short_response[6] = {1, 2, 3, 4, 7, 0};
    EXPECT_EQ(sigurdos::mesh::login_response::parse(
                  short_response, sizeof(short_response)).format,
              sigurdos::mesh::login_response::Format::Unrecognized);

    const uint8_t failure[13] = {1, 2, 3, 4, 7, 0, 0, 0, 9, 8, 7, 6, 2};
    const auto parsed = sigurdos::mesh::login_response::parse(
        failure, sizeof(failure));
    EXPECT_EQ(parsed.format,
              sigurdos::mesh::login_response::Format::CurrentFailure);
    EXPECT_EQ(parsed.failure_code, 7);
}

TEST(MeshContractTest, ResponsesRequireExactTagKeyAndRequestType) {
    uint8_t expected_key[32]{};
    uint8_t other_key[32]{};
    expected_key[0] = 1;
    other_key[0] = 2;
    EXPECT_TRUE(sigurdos::mesh::typedResponseMatches(
        42, expected_key, 3, 42, expected_key, 3, sizeof(expected_key)));
    EXPECT_FALSE(sigurdos::mesh::typedResponseMatches(
        41, expected_key, 3, 42, expected_key, 3, sizeof(expected_key)));
    EXPECT_FALSE(sigurdos::mesh::typedResponseMatches(
        42, other_key, 3, 42, expected_key, 3, sizeof(expected_key)));
    EXPECT_FALSE(sigurdos::mesh::typedResponseMatches(
        42, expected_key, 1, 42, expected_key, 3, sizeof(expected_key)));
}

TEST(MeshContractTest, MessageAndContactBuffersKeepUiCapacities) {
    EXPECT_EQ(sizeof(MeshMessage::sender), 32u);
    EXPECT_EQ(sizeof(MeshMessage::channel), 32u);
    EXPECT_EQ(sizeof(MeshMessage::text), 256u);

    EXPECT_EQ(sizeof(ContactInfo::name), 32u);
    EXPECT_EQ(sizeof(PacketLogEntry::source), 32u);
    EXPECT_EQ(sizeof(PacketLogEntry::type), 16u);
}

TEST(MeshContractTest, NodeStatusWireSizeMatchesDeclaredResponseSize) {
    EXPECT_EQ(NODE_STATUS_RESPONSE_SIZE, 56);
    EXPECT_EQ(sizeof(NodeStatus), static_cast<std::size_t>(NODE_STATUS_RESPONSE_SIZE));
}

TEST(MeshContractTest, TelemetryResultKeepsFixedItemCapacity) {
    EXPECT_EQ(MAX_TELEMETRY_ITEMS, 12);
    EXPECT_EQ(sizeof(TelemetryItem::value_str), 24u);
    EXPECT_EQ(sizeof(TelemetryResult::items) / sizeof(TelemetryResult::items[0]),
              static_cast<std::size_t>(MAX_TELEMETRY_ITEMS));
}

TEST(MeshContractTest, GroupDataTypesMatchDocumentedWireValues) {
    EXPECT_EQ(static_cast<uint16_t>(GroupDataType::GDT_NONE), 0x0000);
    EXPECT_EQ(static_cast<uint16_t>(GroupDataType::GDT_TEMPERATURE), 0x0001);
    EXPECT_EQ(static_cast<uint16_t>(GroupDataType::GDT_HUMIDITY), 0x0002);
    EXPECT_EQ(static_cast<uint16_t>(GroupDataType::GDT_PRESSURE), 0x0003);
    EXPECT_EQ(static_cast<uint16_t>(GroupDataType::GDT_LOCATION), 0x0004);
    EXPECT_EQ(static_cast<uint16_t>(GroupDataType::GDT_BATTERY), 0x0005);
    EXPECT_EQ(static_cast<uint16_t>(GroupDataType::GDT_STATUS), 0x0006);
    EXPECT_EQ(static_cast<uint16_t>(GroupDataType::GDT_CUSTOM), 0x00FF);
}

TEST(MeshContractTest, ResponseGetterRequiresExplicitDestinationCapacities) {
    using Getter = bool (*)(int, uint32_t*, uint8_t*, size_t, uint8_t*,
                            char*, size_t, size_t*);
    static_assert(std::is_same<Getter,
                  decltype(&sigurdos::mesh::getResponse)>::value,
                  "response getter must expose both destination capacities");
    SUCCEED();
}

TEST(MeshContractTest, ResponseCopyReportsRequirementsWithoutPartialWrites) {
    const uint8_t source[] = {1, 2, 3, 4};
    uint8_t data[3] = {0xAA, 0xAA, 0xAA};
    char name[4] = {'x', 'x', 'x', '\0'};
    uint8_t data_required = 0;
    size_t name_required = 0;

    EXPECT_FALSE(sigurdos::mesh::detail::copyResponseBuffers(
        77, source, sizeof(source), "alice", 6, nullptr,
        data, sizeof(data), &data_required,
        name, sizeof(name), &name_required));
    EXPECT_EQ(data_required, sizeof(source));
    EXPECT_EQ(name_required, 6u);
    EXPECT_EQ(data[0], 0xAA);
    EXPECT_STREQ(name, "xxx");
}

TEST(MeshContractTest, ResponseCopyWritesOnlyWhenAllBuffersFit) {
    const uint8_t source[] = {1, 2, 3, 4};
    uint8_t data[sizeof(source)]{};
    char name[6]{};
    uint32_t tag = 0;

    EXPECT_TRUE(sigurdos::mesh::detail::copyResponseBuffers(
        77, source, sizeof(source), "alice", 6, &tag,
        data, sizeof(data), nullptr, name, sizeof(name), nullptr));
    EXPECT_EQ(tag, 77u);
    EXPECT_EQ(std::memcmp(data, source, sizeof(source)), 0);
    EXPECT_STREQ(name, "alice");
}

TEST(MeshContractTest, PingWindowUsesWrapSafeElapsedTime) {
    EXPECT_TRUE(sigurdos::mesh::pingWindowActive(100, 3099, 3000));
    EXPECT_FALSE(sigurdos::mesh::pingWindowActive(100, 3100, 3000));
    EXPECT_TRUE(sigurdos::mesh::pingWindowActive(0xFFFFFFF0u, 0x10u, 64));
    EXPECT_FALSE(sigurdos::mesh::pingWindowActive(0, 1, 3000));
}

TEST(MeshContractTest, PingHintsDeduplicateAndUseLocalSignalMeasurement) {
    sigurdos::mesh::PingResult hints[2]{};
    int count = 0;
    EXPECT_TRUE(sigurdos::mesh::recordUnauthenticatedPingHint(
        hints, count, 2, "alice", -90));
    EXPECT_TRUE(sigurdos::mesh::recordUnauthenticatedPingHint(
        hints, count, 2, "alice", -72));
    EXPECT_EQ(count, 1);
    EXPECT_STREQ(hints[0].name, "alice");
    EXPECT_EQ(hints[0].rssi, -72);

    EXPECT_TRUE(sigurdos::mesh::recordUnauthenticatedPingHint(
        hints, count, 2, "bob", -80));
    EXPECT_FALSE(sigurdos::mesh::recordUnauthenticatedPingHint(
        hints, count, 2, "mallory", -10));
    EXPECT_EQ(count, 2);
}

} // namespace
