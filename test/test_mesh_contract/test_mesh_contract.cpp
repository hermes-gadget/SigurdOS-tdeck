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
#include "mesh/public_channel.h"
#include "mesh/response_copy.h"

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

TEST(MeshContractTest, LegacyLoginResponseIsAcceptedAtSixBytes) {
    const uint8_t response[6] = {1, 2, 3, 4, 'O', 'K'};
    const auto parsed = sigurdos::mesh::login_response::parse(
        response, sizeof(response));
    EXPECT_EQ(parsed.format,
              sigurdos::mesh::login_response::Format::LegacySuccess);
}

TEST(MeshContractTest, CurrentLoginBaseResponseDoesNotRequireAcl) {
    const uint8_t response[7] = {1, 2, 3, 4, 0, 2, 3};
    const auto parsed = sigurdos::mesh::login_response::parse(
        response, sizeof(response));
    EXPECT_EQ(parsed.format,
              sigurdos::mesh::login_response::Format::CurrentSuccess);
    EXPECT_EQ(parsed.keep_alive_secs, 32);
    EXPECT_EQ(parsed.permission, 3);
    EXPECT_EQ(parsed.acl_permissions, 0);
    EXPECT_EQ(parsed.firmware_level, 0);
}

TEST(MeshContractTest, CurrentLoginParsesOptionalAclAndFirmwareLevel) {
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

    const uint8_t failure[7] = {1, 2, 3, 4, 7, 0, 0};
    const auto parsed = sigurdos::mesh::login_response::parse(
        failure, sizeof(failure));
    EXPECT_EQ(parsed.format,
              sigurdos::mesh::login_response::Format::CurrentFailure);
    EXPECT_EQ(parsed.failure_code, 7);
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

} // namespace
