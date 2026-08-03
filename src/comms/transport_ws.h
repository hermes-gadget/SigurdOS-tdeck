// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

#include "transport_frame_parser.h"

#if defined(ESP32_PLATFORM)
#include <WebSocketsServer.h>
#endif

namespace sigurdos {
namespace comms {

class WebSocketCompanionServer {
public:
    static constexpr int MAX_CLIENTS = 4;
    static constexpr uint16_t DEFAULT_PORT = 8765;

    WebSocketCompanionServer();
    ~WebSocketCompanionServer();

    void begin(uint16_t port = DEFAULT_PORT);
    void stop();
    void loop();
    size_t pollRecvFrame(uint8_t dest[], int* client_index_out);
    size_t writeToClient(int client_index, const uint8_t src[], size_t len);
    size_t writeToAllClients(const uint8_t src[], size_t len);
    bool isClientConnected(int client_index) const;
    int connectedCount() const;
    uint32_t clientGeneration(int client_index) const;
    void disconnectClient(int client_index);

#if !defined(ESP32_PLATFORM)
    bool testConnect(int client_index);
    bool testFeed(int client_index, const uint8_t* data, size_t len);
    size_t testReadTx(int client_index, uint8_t* out, size_t capacity);
#endif

private:
    struct QueuedFrame {
        uint8_t len = 0;
        uint8_t payload[MAX_FRAME_SIZE]{};
    };

    struct ClientSlot {
        CompanionFrameParser parser;
        bool in_use = false;
        uint32_t generation = 0;
        QueuedFrame rx_queue[4];
        uint8_t rx_head = 0;
        uint8_t rx_tail = 0;
        uint8_t rx_count = 0;
#if !defined(ESP32_PLATFORM)
        bool connected = false;
        uint8_t tx_queue[4][MAX_FRAME_SIZE + 3]{};
        uint16_t tx_lengths[4]{};
        uint8_t tx_head = 0;
        uint8_t tx_tail = 0;
        uint8_t tx_count = 0;
#endif
    };

#if defined(ESP32_PLATFORM)
    WebSocketsServer* _server = nullptr;
#endif
    ClientSlot _clients[MAX_CLIENTS];
    uint16_t _port = DEFAULT_PORT;
    bool _started = false;
    int _poll_start = 0;

    void resetClient(int client_index);
#if defined(ESP32_PLATFORM)
    void onEvent(uint8_t client_num, WStype_t type, uint8_t* payload,
                 size_t length);
    void queuePayload(int client_index, const uint8_t* payload, size_t length);
#else
    void queuePayload(int client_index, const uint8_t* payload, size_t length);
#endif
};

} // namespace comms
} // namespace sigurdos
