// SPDX-License-Identifier: GPL-3.0-or-later
//
// Packet shape/bounds validation against the upstream MeshCore Packet parser.
//
// The parser validates transport/path encoding and buffer bounds itself
// (isValidPathLen, length-remaining checks, payload buffer cap). The per-type
// minimum payload floors were a SigurdOS core patch and are upstream-owned
// now, so this suite documents upstream's actual contract:
//   - structurally valid frames (>= 1 payload byte) are accepted for every
//     payload type;
//   - frames that end before their payload starts, carry reserved path-mode
//     bits, or truncate a declared path are rejected;
//   - raw reads across every route/path/header combination are bounds-safe.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#define MESH_DEBUG_PRINTLN(...) do {} while (0)
#include "../../lib/meshcore/src/Packet.cpp"

namespace {

std::vector<uint8_t> packetBytes(uint8_t type, size_t payload_length,
                                 uint8_t route = ROUTE_TYPE_FLOOD,
                                 uint8_t path_length = 0) {
    std::vector<uint8_t> raw;
    raw.push_back(static_cast<uint8_t>((type << PH_TYPE_SHIFT) | route));
    if (route == ROUTE_TYPE_TRANSPORT_FLOOD ||
        route == ROUTE_TYPE_TRANSPORT_DIRECT) {
        raw.insert(raw.end(), 4, 0);
    }
    raw.push_back(path_length);
    const size_t path_bytes = (path_length & 63) * ((path_length >> 6) + 1);
    raw.insert(raw.end(), path_bytes, 0);
    raw.insert(raw.end(), payload_length, 0);
    return raw;
}

TEST(PacketValidationTest, AcceptsEveryPayloadTypeWithAtLeastOnePayloadByte) {
    const std::array<uint8_t, 12> types = {
        PAYLOAD_TYPE_REQ, PAYLOAD_TYPE_RESPONSE, PAYLOAD_TYPE_TXT_MSG,
        PAYLOAD_TYPE_ACK, PAYLOAD_TYPE_ADVERT, PAYLOAD_TYPE_GRP_TXT,
        PAYLOAD_TYPE_GRP_DATA, PAYLOAD_TYPE_ANON_REQ, PAYLOAD_TYPE_PATH,
        PAYLOAD_TYPE_TRACE, PAYLOAD_TYPE_MULTIPART, PAYLOAD_TYPE_CONTROL,
    };

    for (uint8_t type : types) {
        const auto minimal = packetBytes(type, 1);
        mesh::Packet parsed;
        EXPECT_TRUE(parsed.readFrom(
            minimal.data(), static_cast<uint8_t>(minimal.size())))
            << "type=" << unsigned(type) << " payload_length=1";
        EXPECT_EQ(type, parsed.getPayloadType());

        // A frame that ends where the payload should begin is malformed:
        // every accepted frame carries at least one payload byte.
        const auto empty = packetBytes(type, 0);
        mesh::Packet empty_parsed;
        EXPECT_FALSE(empty_parsed.readFrom(
            empty.data(), static_cast<uint8_t>(empty.size())))
            << "type=" << unsigned(type) << " payload_length=0";
    }
}

TEST(PacketValidationTest, ChecksTransportAndPathBytesBeforeCopying) {
    mesh::Packet parsed;
    const uint8_t transport_header = ROUTE_TYPE_TRANSPORT_DIRECT;
    for (int length = 0; length < 6; ++length) {
        uint8_t raw[6] = {transport_header, 0, 0, 0, 0, 0};
        EXPECT_FALSE(parsed.readFrom(raw, static_cast<uint8_t>(length)));
    }

    auto valid = packetBytes(PAYLOAD_TYPE_RAW_CUSTOM, 1,
                             ROUTE_TYPE_TRANSPORT_DIRECT);
    EXPECT_TRUE(parsed.readFrom(
        valid.data(), static_cast<uint8_t>(valid.size())));

    // Keep the full buffer allocated, but claim one byte less than the
    // declared path needs: the parser must reject it via its length checks.
    auto truncated_path = packetBytes(PAYLOAD_TYPE_RAW_CUSTOM, 1,
                                      ROUTE_TYPE_FLOOD, 0x81);
    EXPECT_FALSE(parsed.readFrom(
        truncated_path.data(),
        static_cast<uint8_t>(truncated_path.size() - 1)));

    auto reserved_path = packetBytes(PAYLOAD_TYPE_RAW_CUSTOM, 1,
                                     ROUTE_TYPE_FLOOD, 0xC0);
    EXPECT_FALSE(parsed.readFrom(
        reserved_path.data(), static_cast<uint8_t>(reserved_path.size())));
}

TEST(PacketValidationTest, AllRawLengthsAndHeaderModesAreBoundsSafe) {
    std::array<uint8_t, MAX_TRANS_UNIT> raw{};
    mesh::Packet parsed;
    for (int route = 0; route < 4; ++route) {
        for (int path_mode = 0; path_mode < 4; ++path_mode) {
            raw.fill(0);
            raw[0] = static_cast<uint8_t>(
                (PAYLOAD_TYPE_RAW_CUSTOM << PH_TYPE_SHIFT) | route);
            const size_t path_index = 1 +
                ((route == ROUTE_TYPE_TRANSPORT_FLOOD ||
                  route == ROUTE_TYPE_TRANSPORT_DIRECT) ? 4 : 0);
            raw[path_index] = static_cast<uint8_t>(path_mode << 6);
            for (int length = 0; length <= MAX_TRANS_UNIT; ++length) {
                (void)parsed.readFrom(raw.data(), static_cast<uint8_t>(length));
            }
        }
    }
}

}  // namespace
