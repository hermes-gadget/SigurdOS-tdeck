#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "comms/transport_frame_parser.h"
#include "comms/transport_iface.h"
#include "comms/transport_tcp.h"
#include "comms/transport_ws.h"
#include "comms/transports_internal.h"
#include "hal/prefs.h"
#include "mocks/mock_state.h"

namespace {

using sigurdos::comms::TransportId;

std::vector<uint8_t> inboundFrame(std::initializer_list<uint8_t> payload)
{
    std::vector<uint8_t> frame{'<', static_cast<uint8_t>(payload.size()), 0};
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

void drainResponse(TransportId id, int client, std::vector<uint8_t>& payload)
{
    uint8_t encoded[MAX_FRAME_SIZE + 3]{};
    const size_t len = sigurdos::comms::transports_test_read_tx(
        id, client, encoded, sizeof(encoded));
    ASSERT_GE(len, 3U);
    ASSERT_EQ(encoded[0], '>');
    const size_t frame_len = static_cast<size_t>(encoded[1]) |
        (static_cast<size_t>(encoded[2]) << 8);
    ASSERT_EQ(len, frame_len + 3);
    payload.assign(encoded + 3, encoded + len);
}

template <typename Server>
std::vector<uint8_t> directResponse(Server& server, int client)
{
    uint8_t encoded[MAX_FRAME_SIZE + 3]{};
    const size_t len = server.testReadTx(client, encoded, sizeof(encoded));
    EXPECT_GE(len, 3U);
    if (len < 3) return {};
    const size_t frame_len = static_cast<size_t>(encoded[1]) |
        (static_cast<size_t>(encoded[2]) << 8);
    EXPECT_EQ(len, frame_len + 3);
    return std::vector<uint8_t>(encoded + 3, encoded + len);
}

TEST(CompanionFrameParser, HandlesBoundariesNoiseAndOversizeFrames)
{
    sigurdos::comms::CompanionFrameParser parser;
    const uint8_t payload[] = {0x22, 0x00, 0x80, 0xFE};
    const uint8_t frame[] = {'x', '<', 4, 0, 0x22, 0x00, 0x80, 0xFE};
    for (size_t i = 0; i + 1 < sizeof(frame); ++i) {
        EXPECT_FALSE(parser.push(frame[i])) << "byte " << i;
    }
    EXPECT_TRUE(parser.push(frame[sizeof(frame) - 1]));
    uint8_t out[sizeof(payload)]{};
    size_t length = 0;
    ASSERT_TRUE(parser.take(out, sizeof(out), &length));
    EXPECT_EQ(length, sizeof(payload));
    EXPECT_EQ(std::vector<uint8_t>(out, out + length),
              std::vector<uint8_t>(payload, payload + sizeof(payload)));

    const uint8_t oversize[] = {'<', static_cast<uint8_t>(MAX_FRAME_SIZE + 1), 0};
    for (uint8_t byte : oversize) EXPECT_FALSE(parser.push(byte));
    const uint8_t valid[] = {'<', 1, 0, 0xA5};
    for (size_t i = 0; i < sizeof(valid); ++i) {
        EXPECT_EQ(parser.push(valid[i]), i + 1 == sizeof(valid));
    }
    ASSERT_TRUE(parser.take(out, sizeof(out), &length));
    ASSERT_EQ(length, 1U);
    EXPECT_EQ(out[0], 0xA5);
}

TEST(CompanionTransportServers, TCPAndWebSocketKeepClientStreamsSeparate)
{
    sigurdos::comms::TCPCompanionServer tcp;
    tcp.begin();
    ASSERT_TRUE(tcp.testConnect(0));
    ASSERT_TRUE(tcp.testConnect(1));
    ASSERT_FALSE(tcp.testConnect(4));
    EXPECT_EQ(tcp.connectedCount(), 2);

    const std::vector<uint8_t> first = inboundFrame({0x10, 0x11});
    ASSERT_FALSE(tcp.testFeed(1, first.data(), 2));
    ASSERT_TRUE(tcp.testFeed(1, first.data() + 2, first.size() - 2));
    uint8_t payload[8]{};
    int client = -1;
    ASSERT_EQ(tcp.pollRecvFrame(payload, &client), 2U);
    EXPECT_EQ(client, 1);
    EXPECT_EQ(payload[0], 0x10);
    EXPECT_EQ(payload[1], 0x11);

    ASSERT_EQ(tcp.writeToAllClients(payload, 2), 2U);
    std::vector<uint8_t> response;
    response = directResponse(tcp, 0);
    EXPECT_EQ(response, (std::vector<uint8_t>{0x10, 0x11}));
    response = directResponse(tcp, 1);
    EXPECT_EQ(response, (std::vector<uint8_t>{0x10, 0x11}));

    sigurdos::comms::WebSocketCompanionServer ws;
    ws.begin();
    ASSERT_TRUE(ws.testConnect(0));
    ASSERT_TRUE(ws.testConnect(1));
    const std::vector<uint8_t> second = inboundFrame({0x42});
    ASSERT_TRUE(ws.testFeed(0, second.data(), second.size()));
    ASSERT_EQ(ws.pollRecvFrame(payload, &client), 1U);
    EXPECT_EQ(client, 0);
    EXPECT_EQ(payload[0], 0x42);
    ASSERT_EQ(ws.writeToClient(0, payload, 1), 1U);
    response = directResponse(ws, 0);
    EXPECT_EQ(response, (std::vector<uint8_t>{0x42}));
    uint8_t no_response[MAX_FRAME_SIZE + 3]{};
    EXPECT_EQ(ws.testReadTx(1, no_response, sizeof(no_response)), 0U);
}

std::array<bool, 4> g_replayed{};

void registryReplyHandler(const uint8_t* frame, size_t len, TransportId from)
{
    TransportId current = from;
    int client = -1;
    uint32_t generation = 0;
    ASSERT_TRUE(sigurdos::comms::transports_current_request(
        &current, &client, &generation));
    ASSERT_GT(generation, 0U);
    if (len == 1 && frame[0] == 0x10 && client >= 0 && client < 4 &&
        !g_replayed[static_cast<size_t>(client)]) {
        g_replayed[static_cast<size_t>(client)] = true;
        sigurdos::comms::transport_send_to(current, client, frame, len);
    }
}

TEST(CompanionTransportRegistry, BroadcastsPushesAndDeduplicatesReplayPerClient)
{
    g_replayed.fill(false);
    sigurdos::prefs_mock_reset();
    sigurdos::comms::transports_test_reset();
    sigurdos::NodePrefs prefs = sigurdos::prefs_get();
    prefs.transport_tcp_enabled = true;
    prefs.transport_ws_enabled = true;
    ASSERT_TRUE(sigurdos::prefs_set(prefs));
    sigurdos::comms::transports_set_frame_handler(&registryReplyHandler);
    sigurdos::comms::transports_init();

    ASSERT_TRUE(sigurdos::comms::transports_test_connect(TransportId::TCP, 0));
    ASSERT_TRUE(sigurdos::comms::transports_test_connect(TransportId::TCP, 1));
    ASSERT_TRUE(sigurdos::comms::transports_test_connect(TransportId::WS, 0));
    EXPECT_EQ(sigurdos::comms::transport_status(TransportId::TCP).clientCount, 2);
    EXPECT_EQ(sigurdos::comms::transport_status(TransportId::WS).clientCount, 1);

    const std::vector<uint8_t> request = inboundFrame({0x10});
    ASSERT_TRUE(sigurdos::comms::transports_test_feed(
        TransportId::TCP, 0, request.data(), request.size()));
    sigurdos::comms::transports_loop();
    std::vector<uint8_t> response;
    drainResponse(TransportId::TCP, 0, response);
    EXPECT_EQ(response, (std::vector<uint8_t>{0x10}));
    uint8_t encoded[MAX_FRAME_SIZE + 3]{};
    EXPECT_EQ(sigurdos::comms::transports_test_read_tx(
                  TransportId::TCP, 1, encoded, sizeof(encoded)), 0U);

    // Repeating the same command on one client is not replayed to it, while a
    // second client has an independent replay window.
    ASSERT_TRUE(sigurdos::comms::transports_test_feed(
        TransportId::TCP, 0, request.data(), request.size()));
    ASSERT_TRUE(sigurdos::comms::transports_test_feed(
        TransportId::TCP, 1, request.data(), request.size()));
    sigurdos::comms::transports_loop();
    sigurdos::comms::transports_loop();
    EXPECT_EQ(sigurdos::comms::transports_test_read_tx(
                  TransportId::TCP, 0, encoded, sizeof(encoded)), 0U);
    drainResponse(TransportId::TCP, 1, response);
    EXPECT_EQ(response, (std::vector<uint8_t>{0x10}));

    const uint8_t push[] = {0x90, 0x01};
    sigurdos::comms::transports_broadcast(TransportId::TCP, push, sizeof(push));
    drainResponse(TransportId::TCP, 0, response);
    EXPECT_EQ(response, (std::vector<uint8_t>{0x90, 0x01}));
    drainResponse(TransportId::TCP, 1, response);
    EXPECT_EQ(response, (std::vector<uint8_t>{0x90, 0x01}));
    uint8_t ws_encoded[MAX_FRAME_SIZE + 3]{};
    EXPECT_EQ(sigurdos::comms::transports_test_read_tx(
                  TransportId::WS, 0, ws_encoded, sizeof(ws_encoded)), 0U);
}

} // namespace
