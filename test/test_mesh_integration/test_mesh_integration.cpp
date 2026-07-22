// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#if !defined(SIGURDOS_MESHCORE_INTEGRATION)

TEST(MeshCoreIntegrationEnvironmentTest, UsesDedicatedProductionLinkedEnvironment) {
    SUCCEED() << "run with pio test -e native_mesh_integration";
}

#else

#include <Dispatcher.h>
#include <Packet.h>
#include <helpers/StaticPoolPacketManager.h>

#include <array>
#include <cstring>
#include <deque>
#include <vector>

namespace {

class FakeClock final : public mesh::MillisecondClock {
public:
    unsigned long now = 1;
    unsigned long getMillis() override { return now; }
};

class FakeRadio final : public mesh::Radio {
public:
    std::deque<std::vector<uint8_t>> incoming;
    std::vector<std::vector<uint8_t>> transmitted;
    bool send_ok = true;
    bool send_complete = true;
    bool recv_mode = true;
    int finished = 0;

    int recvRaw(uint8_t* bytes, int size) override {
        if (incoming.empty()) return 0;
        const std::vector<uint8_t> raw = incoming.front();
        incoming.pop_front();
        if (static_cast<int>(raw.size()) > size) return -1;
        std::memcpy(bytes, raw.data(), raw.size());
        return static_cast<int>(raw.size());
    }
    uint32_t getEstAirtimeFor(int bytes) override {
        return static_cast<uint32_t>(bytes);
    }
    float packetScore(float, int) override { return 1.0f; }
    bool startSendRaw(const uint8_t* bytes, int size) override {
        if (!send_ok) return false;
        transmitted.emplace_back(bytes, bytes + size);
        return true;
    }
    bool isSendComplete() override { return send_complete; }
    void onSendFinished() override { ++finished; }
    bool isInRecvMode() const override { return recv_mode; }
    float getLastRSSI() const override { return -72.0f; }
    float getLastSNR() const override { return 7.5f; }
};

class TestDispatcher final : public mesh::Dispatcher {
public:
    TestDispatcher(mesh::Radio& radio, mesh::MillisecondClock& clock,
                   mesh::PacketManager& packets)
        : Dispatcher(radio, clock, packets) {}

    int received = 0;
    uint8_t last_payload = 0;
    mesh::DispatcherAction receive_action = ACTION_RELEASE;

protected:
    mesh::DispatcherAction onRecvPacket(mesh::Packet* packet) override {
        ++received;
        last_payload = packet->payload_len ? packet->payload[0] : 0;
        return receive_action;
    }
};

std::vector<uint8_t> encodePacket(uint8_t route, uint8_t payload_type,
                                  std::initializer_list<uint8_t> payload) {
    mesh::Packet packet;
    packet.header = static_cast<uint8_t>(route | (payload_type << PH_TYPE_SHIFT));
    packet.path_len = 0;
    packet.payload_len = static_cast<uint16_t>(payload.size());
    std::copy(payload.begin(), payload.end(), packet.payload);
    std::array<uint8_t, MAX_TRANS_UNIT> raw{};
    const uint8_t size = packet.writeTo(raw.data());
    return {raw.begin(), raw.begin() + size};
}

TEST(MeshCoreIntegrationTest, ProductionParserRejectsMalformedPacketTable) {
    FakeRadio radio;
    FakeClock clock;
    StaticPoolPacketManager packets(4);
    TestDispatcher dispatcher(radio, clock, packets);
    mesh::Packet parsed;

    const std::vector<std::vector<uint8_t>> malformed = {
        {},
        {ROUTE_TYPE_FLOOD},
        {static_cast<uint8_t>(ROUTE_TYPE_DIRECT |
                              (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT)), 4, 1, 2},
        {static_cast<uint8_t>(ROUTE_TYPE_FLOOD |
                              (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT))},
        std::vector<uint8_t>(MAX_TRANS_UNIT + 1, 0),
    };
    for (const auto& raw : malformed) {
        EXPECT_FALSE(dispatcher.tryParsePacket(
            &parsed, raw.empty() ? nullptr : raw.data(), static_cast<int>(raw.size())));
    }
}

TEST(MeshCoreIntegrationTest, PoolSaturationUnwindsAndPreservesFifo) {
    StaticPoolPacketManager packets(3);
    mesh::Packet* first = packets.allocNew();
    mesh::Packet* second = packets.allocNew();
    mesh::Packet* third = packets.allocNew();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(packets.allocNew(), nullptr);

    packets.queueOutbound(first, 2, 10);
    packets.queueOutbound(second, 2, 10);
    packets.queueOutbound(third, 1, 10);
    EXPECT_EQ(packets.getNextOutbound(9), nullptr);
    EXPECT_EQ(packets.getNextOutbound(10), third);
    EXPECT_EQ(packets.getNextOutbound(10), first);
    EXPECT_EQ(packets.getNextOutbound(10), second);

    packets.free(first);
    packets.free(second);
    packets.free(third);
    EXPECT_EQ(packets.getFreeCount(), 3);
}

TEST(MeshCoreIntegrationTest, RealDispatcherTransmitsAndUpdatesCounters) {
    FakeRadio radio;
    FakeClock clock;
    StaticPoolPacketManager packets(4);
    TestDispatcher dispatcher(radio, clock, packets);
    dispatcher.begin();

    mesh::Packet* packet = dispatcher.obtainNewPacket();
    ASSERT_NE(packet, nullptr);
    packet->header = static_cast<uint8_t>(ROUTE_TYPE_FLOOD |
                                          (PAYLOAD_TYPE_CONTROL << PH_TYPE_SHIFT));
    packet->payload_len = 1;
    packet->payload[0] = 0x80;
    dispatcher.sendPacket(packet, 0);

    ++clock.now;
    dispatcher.loop();
    ASSERT_EQ(radio.transmitted.size(), 1U);
    dispatcher.loop();
    EXPECT_EQ(radio.finished, 1);
    EXPECT_EQ(dispatcher.getNumSentFlood(), 1U);
    EXPECT_EQ(packets.getFreeCount(), 4);
}

TEST(MeshCoreIntegrationTest, FailedRadioStartReleasesPacketWithoutFalseTx) {
    FakeRadio radio;
    radio.send_ok = false;
    FakeClock clock;
    StaticPoolPacketManager packets(2);
    TestDispatcher dispatcher(radio, clock, packets);
    dispatcher.begin();

    mesh::Packet* packet = dispatcher.obtainNewPacket();
    ASSERT_NE(packet, nullptr);
    packet->header = static_cast<uint8_t>(ROUTE_TYPE_FLOOD |
                                          (PAYLOAD_TYPE_CONTROL << PH_TYPE_SHIFT));
    packet->payload_len = 1;
    packet->payload[0] = 0x80;
    dispatcher.sendPacket(packet, 0);
    ++clock.now;
    dispatcher.loop();

    EXPECT_TRUE(radio.transmitted.empty());
    EXPECT_EQ(dispatcher.getNumSentFlood(), 0U);
    EXPECT_EQ(packets.getFreeCount(), 2);
}

TEST(MeshCoreIntegrationTest, ReceivePathFansOutValidAndDropsMalformedFrames) {
    FakeRadio radio;
    FakeClock clock;
    StaticPoolPacketManager packets(3);
    TestDispatcher dispatcher(radio, clock, packets);
    dispatcher.begin();

    radio.incoming.push_back({0xFF});
    radio.incoming.push_back(
        encodePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_CONTROL, {0x80, 0x42}));
    dispatcher.loop();
    EXPECT_EQ(dispatcher.received, 0);
    dispatcher.loop();
    EXPECT_EQ(dispatcher.received, 1);
    EXPECT_EQ(dispatcher.last_payload, 0x80);
    EXPECT_EQ(dispatcher.getNumRecvFlood(), 1U);
    EXPECT_EQ(packets.getFreeCount(), 3);
}

}  // namespace

#endif
