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

#include <gtest/gtest.h>

#include "mesh/mesh_wrapper.h"
#include "mesh/public_channel.h"

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

} // namespace
