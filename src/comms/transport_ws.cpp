// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "transport_ws.h"

#include <cstring>

#if defined(ESP32_PLATFORM)
#include "hal/wifi_ota.h"
#endif

namespace {
bool networkTransportAllowed()
{
#if defined(ESP32_PLATFORM)
    return sigurdos::ota::companionTransportsAllowed();
#else
    return true;
#endif
}
} // namespace

namespace sigurdos {
namespace comms {

WebSocketCompanionServer::WebSocketCompanionServer() = default;

WebSocketCompanionServer::~WebSocketCompanionServer()
{
    stop();
}

void WebSocketCompanionServer::begin(uint16_t port)
{
    stop();
    if (!networkTransportAllowed()) return;
    _port = port;
    _started = true;
#if defined(ESP32_PLATFORM)
    _server = new WebSocketsServer(port);
    _server->onEvent([this](uint8_t client_num, WStype_t type,
                             uint8_t* payload, size_t length) {
        onEvent(client_num, type, payload, length);
    });
    _server->begin();
#endif
}

void WebSocketCompanionServer::stop()
{
#if defined(ESP32_PLATFORM)
    if (_server) {
        _server->close();
        delete _server;
        _server = nullptr;
    }
#endif
    for (int i = 0; i < MAX_CLIENTS; ++i) resetClient(i);
    _started = false;
}

void WebSocketCompanionServer::resetClient(int client_index)
{
    if (client_index < 0 || client_index >= MAX_CLIENTS) return;
#if defined(ESP32_PLATFORM)
    // The WebSockets callback also uses this routine for WStype_DISCONNECTED;
    // issuing another disconnect from that callback can recurse into the
    // library. Explicit disconnectClient() performs the socket close first.
#else
    _clients[client_index].connected = false;
    _clients[client_index].tx_head = 0;
    _clients[client_index].tx_tail = 0;
    _clients[client_index].tx_count = 0;
#endif
    _clients[client_index].in_use = false;
    _clients[client_index].parser.reset();
    _clients[client_index].rx_head = 0;
    _clients[client_index].rx_tail = 0;
    _clients[client_index].rx_count = 0;
}

void WebSocketCompanionServer::loop()
{
    if (!networkTransportAllowed()) {
        stop();
        return;
    }
#if defined(ESP32_PLATFORM)
    if (_started && _server) _server->loop();
#endif
}

#if defined(ESP32_PLATFORM)
void WebSocketCompanionServer::onEvent(uint8_t client_num, WStype_t type,
                                       uint8_t* payload, size_t length)
{
    if (client_num >= MAX_CLIENTS) {
        if (_server) _server->disconnect(client_num);
        return;
    }
    ClientSlot& client = _clients[client_num];
    if (type == WStype_CONNECTED) {
        if (!networkTransportAllowed()) {
            if (_server) _server->disconnect(client_num);
            resetClient(client_num);
            return;
        }
        client.in_use = true;
        client.parser.reset();
        client.rx_head = 0;
        client.rx_tail = 0;
        client.rx_count = 0;
        ++client.generation;
        if (client.generation == 0) ++client.generation;
        return;
    }
    if (type == WStype_DISCONNECTED) {
        resetClient(client_num);
        return;
    }
    if (!networkTransportAllowed()) {
        if (_server) _server->disconnect(client_num);
        resetClient(client_num);
        return;
    }
    if (!client.in_use) return;
    if (type == WStype_BIN || type == WStype_FRAGMENT_BIN_START ||
        type == WStype_FRAGMENT || type == WStype_FRAGMENT_FIN) {
        queuePayload(client_num, payload, length);
    }
}
#endif

void WebSocketCompanionServer::queuePayload(int client_index,
                                             const uint8_t* payload, size_t length)
{
    if (!payload || client_index < 0 || client_index >= MAX_CLIENTS ||
        !isClientConnected(client_index)) return;
    ClientSlot& client = _clients[client_index];
    for (size_t i = 0; i < length; ++i) {
        if (!client.parser.push(payload[i])) continue;
        if (client.rx_count >= 4) {
            client.parser.reset();
            continue;
        }
        QueuedFrame& frame = client.rx_queue[client.rx_tail];
        size_t frame_len = 0;
        if (!client.parser.take(frame.payload, MAX_FRAME_SIZE, &frame_len)) continue;
        frame.len = static_cast<uint8_t>(frame_len);
        client.rx_tail = static_cast<uint8_t>((client.rx_tail + 1) % 4);
        ++client.rx_count;
    }
}

size_t WebSocketCompanionServer::pollRecvFrame(uint8_t dest[],
                                               int* client_index_out)
{
    if (!networkTransportAllowed()) {
        stop();
        return 0;
    }
    if (!_started || !dest) return 0;
    loop();
    int start = _poll_start;
    if (start < 0 || start >= MAX_CLIENTS) start = 0;
    for (int offset = 0; offset < MAX_CLIENTS; ++offset) {
        const int index = (start + offset) % MAX_CLIENTS;
        ClientSlot& client = _clients[index];
        if (!isClientConnected(index) || client.rx_count == 0) continue;
        const QueuedFrame& frame = client.rx_queue[client.rx_head];
        std::memcpy(dest, frame.payload, frame.len);
        const size_t length = frame.len;
        client.rx_head = static_cast<uint8_t>((client.rx_head + 1) % 4);
        --client.rx_count;
        if (client_index_out) *client_index_out = index;
        _poll_start = (index + 1) % MAX_CLIENTS;
        return length;
    }
    _poll_start = (start + 1) % MAX_CLIENTS;
    return 0;
}

size_t WebSocketCompanionServer::writeToClient(int client_index,
                                               const uint8_t src[], size_t len)
{
    if (!_started || !networkTransportAllowed() || !src || len == 0 ||
        len > MAX_FRAME_SIZE ||
        !isClientConnected(client_index)) return 0;
    uint8_t encoded[MAX_FRAME_SIZE + 3];
    const size_t encoded_len = encodeCompanionTransportFrame(
        encoded, sizeof(encoded), src, len);
    if (encoded_len == 0) return 0;
#if defined(ESP32_PLATFORM)
    if (!_server || !_server->sendBIN(static_cast<uint8_t>(client_index),
                                      encoded, encoded_len)) return 0;
#else
    ClientSlot& client = _clients[client_index];
    if (client.tx_count >= 4) return 0;
    std::memcpy(client.tx_queue[client.tx_tail], encoded, encoded_len);
    client.tx_lengths[client.tx_tail] = static_cast<uint16_t>(encoded_len);
    client.tx_tail = static_cast<uint8_t>((client.tx_tail + 1) % 4);
    ++client.tx_count;
#endif
    return len;
}

size_t WebSocketCompanionServer::writeToAllClients(const uint8_t src[], size_t len)
{
    if (!networkTransportAllowed() || !src || len == 0 ||
        len > MAX_FRAME_SIZE) return 0;
    int connected = 0;
    int sent = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!isClientConnected(i)) continue;
        ++connected;
        if (writeToClient(i, src, len) == len) ++sent;
    }
    return connected > 0 && connected == sent ? len : 0;
}

bool WebSocketCompanionServer::isClientConnected(int client_index) const
{
    if (!networkTransportAllowed() || client_index < 0 ||
        client_index >= MAX_CLIENTS) return false;
    const ClientSlot& client = _clients[client_index];
#if defined(ESP32_PLATFORM)
    return client.in_use && _server && _server->clientIsConnected(
        static_cast<uint8_t>(client_index));
#else
    return client.in_use && client.connected;
#endif
}

int WebSocketCompanionServer::connectedCount() const
{
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (isClientConnected(i)) ++count;
    }
    return count;
}

uint32_t WebSocketCompanionServer::clientGeneration(int client_index) const
{
    if (client_index < 0 || client_index >= MAX_CLIENTS ||
        !isClientConnected(client_index)) return 0;
    return _clients[client_index].generation;
}

void WebSocketCompanionServer::disconnectClient(int client_index)
{
#if defined(ESP32_PLATFORM)
    if (_server && client_index >= 0 && client_index < MAX_CLIENTS &&
        _clients[client_index].in_use) {
        _server->disconnect(static_cast<uint8_t>(client_index));
    }
#endif
    resetClient(client_index);
}

#if !defined(ESP32_PLATFORM)
bool WebSocketCompanionServer::testConnect(int client_index)
{
    if (client_index < 0 || client_index >= MAX_CLIENTS || !_started) return false;
    resetClient(client_index);
    ClientSlot& client = _clients[client_index];
    client.in_use = true;
    client.connected = true;
    ++client.generation;
    if (client.generation == 0) ++client.generation;
    return true;
}

bool WebSocketCompanionServer::testFeed(int client_index, const uint8_t* data,
                                        size_t len)
{
    if (!data || !isClientConnected(client_index)) return false;
    const uint8_t before = _clients[client_index].rx_count;
    queuePayload(client_index, data, len);
    return _clients[client_index].rx_count != before;
}

size_t WebSocketCompanionServer::testReadTx(int client_index, uint8_t* out,
                                            size_t capacity)
{
    if (!out || !isClientConnected(client_index)) return 0;
    ClientSlot& client = _clients[client_index];
    if (client.tx_count == 0 || capacity < client.tx_lengths[client.tx_head]) return 0;
    const size_t len = client.tx_lengths[client.tx_head];
    std::memcpy(out, client.tx_queue[client.tx_head], len);
    client.tx_head = static_cast<uint8_t>((client.tx_head + 1) % 4);
    --client.tx_count;
    return len;
}
#endif

} // namespace comms
} // namespace sigurdos
