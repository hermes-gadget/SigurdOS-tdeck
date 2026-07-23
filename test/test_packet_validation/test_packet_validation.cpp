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

TEST(PacketValidationTest, RejectsEveryPayloadShorterThanItsFixedShape) {
    const std::array<uint8_t, 12> types = {
        PAYLOAD_TYPE_REQ, PAYLOAD_TYPE_RESPONSE, PAYLOAD_TYPE_TXT_MSG,
        PAYLOAD_TYPE_ACK, PAYLOAD_TYPE_ADVERT, PAYLOAD_TYPE_GRP_TXT,
        PAYLOAD_TYPE_GRP_DATA, PAYLOAD_TYPE_ANON_REQ, PAYLOAD_TYPE_PATH,
        PAYLOAD_TYPE_TRACE, PAYLOAD_TYPE_MULTIPART, PAYLOAD_TYPE_CONTROL,
    };

    for (uint8_t type : types) {
        const size_t minimum = mesh::Packet::minimumPayloadLength(type);
        ASSERT_GT(minimum, 0U);
        for (size_t length = 0; length < minimum; ++length) {
            const auto raw = packetBytes(type, length);
            mesh::Packet parsed;
            EXPECT_FALSE(parsed.readFrom(
                raw.data(), static_cast<uint8_t>(raw.size())))
                << "type=" << unsigned(type) << " payload_length=" << length;
        }
        const auto exact = packetBytes(type, minimum);
        mesh::Packet parsed;
        EXPECT_TRUE(parsed.readFrom(
            exact.data(), static_cast<uint8_t>(exact.size())))
            << "type=" << unsigned(type);
    }
}

TEST(PacketValidationTest, ChecksTransportAndPathBytesBeforeCopying) {
    mesh::Packet parsed;
    const uint8_t transport_header = ROUTE_TYPE_TRANSPORT_DIRECT;
    for (int length = 0; length < 6; ++length) {
        uint8_t raw[6] = {transport_header, 0, 0, 0, 0, 0};
        EXPECT_FALSE(parsed.readFrom(raw, static_cast<uint8_t>(length)));
    }

    auto valid = packetBytes(PAYLOAD_TYPE_RAW_CUSTOM, 0,
                             ROUTE_TYPE_TRANSPORT_DIRECT);
    EXPECT_TRUE(parsed.readFrom(
        valid.data(), static_cast<uint8_t>(valid.size())));

    auto truncated_path = packetBytes(PAYLOAD_TYPE_RAW_CUSTOM, 0,
                                      ROUTE_TYPE_FLOOD, 0x81);
    truncated_path.pop_back();
    EXPECT_FALSE(parsed.readFrom(
        truncated_path.data(), static_cast<uint8_t>(truncated_path.size())));

    auto reserved_path = packetBytes(PAYLOAD_TYPE_RAW_CUSTOM, 0,
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
    EXPECT_FALSE(parsed.readFrom(nullptr, 2));
}

}  // namespace
