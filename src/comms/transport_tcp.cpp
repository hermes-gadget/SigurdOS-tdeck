// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "transport_tcp.h"

#include <cstring>

#if defined(ESP32_PLATFORM)
#include "hal/wifi_ota.h"
#include <lwip/sockets.h>
#endif

namespace {
#if defined(ESP32_PLATFORM)
constexpr uint32_t TCP_WRITE_TIMEOUT_MS = 120;
#endif

bool networkTransportAllowed()
{
#if defined(ESP32_PLATFORM)
    return sigurdos::ota::companionTransportsAllowed();
#else
    return true;
#endif
}

#if defined(ESP32_PLATFORM)
bool socketWritableNow(WiFiClient& client)
{
    const int fd = client.fd();
    if (fd < 0) return client.connected();
    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(fd, &write_set);
    timeval timeout{};
    return select(fd + 1, nullptr, &write_set, nullptr, &timeout) > 0 &&
           FD_ISSET(fd, &write_set);
}
#endif
} // namespace

namespace sigurdos {
namespace comms {

TCPCompanionServer::TCPCompanionServer()
#if defined(ESP32_PLATFORM)
    : _server()
#endif
{
}

void TCPCompanionServer::begin(uint16_t port)
{
    stop();
    if (!networkTransportAllowed()) return;
    _port = port;
    _started = true;
#if defined(ESP32_PLATFORM)
    _server.begin(port);
#endif
}

void TCPCompanionServer::stop()
{
#if defined(ESP32_PLATFORM)
    _server.stop();
#endif
    for (int i = 0; i < MAX_CLIENTS; ++i) resetClient(i);
    _started = false;
}

void TCPCompanionServer::resetClient(int client_index)
{
    if (client_index < 0 || client_index >= MAX_CLIENTS) return;
#if defined(ESP32_PLATFORM)
    if (_clients[client_index].in_use) _clients[client_index].client.stop();
#else
    _clients[client_index].connected = false;
    _clients[client_index].tx_head = 0;
    _clients[client_index].tx_tail = 0;
    _clients[client_index].tx_count = 0;
#endif
    _clients[client_index].in_use = false;
    _clients[client_index].parser.reset();
}

#if defined(ESP32_PLATFORM)
void TCPCompanionServer::acceptNewClients()
{
    if (!networkTransportAllowed()) return;
    while (_server.hasClient()) {
        WiFiClient incoming = _server.accept();
        if (!incoming) continue;
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (!_clients[i].in_use) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            incoming.stop();
            continue;
        }
        ClientSlot& client = _clients[slot];
        client.client = incoming;
        client.in_use = true;
        client.parser.reset();
        ++client.generation;
        if (client.generation == 0) ++client.generation;
    }
}

void TCPCompanionServer::pruneDisconnected()
{
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (_clients[i].in_use && !_clients[i].client.connected()) resetClient(i);
    }
}

bool TCPCompanionServer::writeBytes(WiFiClient& client, const uint8_t* data,
                                    size_t len) const
{
    if (!data || !client.connected()) return false;
    size_t sent = 0;
    const uint32_t started = millis();
    while (sent < len) {
        if (!networkTransportAllowed()) return false;
        if (socketWritableNow(client)) {
            const size_t count = client.write(data + sent, len - sent);
            if (count > 0) {
                sent += count;
                continue;
            }
        }
        if (millis() - started >= TCP_WRITE_TIMEOUT_MS) return false;
        delay(1);
    }
    return true;
}
#endif

size_t TCPCompanionServer::pollRecvFrame(uint8_t dest[], int* client_index_out)
{
    if (!networkTransportAllowed()) {
        stop();
        return 0;
    }
    if (!_started || !dest) return 0;
#if defined(ESP32_PLATFORM)
    acceptNewClients();
    if (!networkTransportAllowed()) {
        stop();
        return 0;
    }
    pruneDisconnected();
#endif

    int start = _poll_start;
    if (start < 0 || start >= MAX_CLIENTS) start = 0;
    for (int offset = 0; offset < MAX_CLIENTS; ++offset) {
        const int index = (start + offset) % MAX_CLIENTS;
        ClientSlot& client = _clients[index];
#if defined(ESP32_PLATFORM)
        if (!client.in_use || !client.client.connected()) continue;
        while (client.client.available()) {
            const int value = client.client.read();
            if (value < 0) break;
            if (client.parser.push(static_cast<uint8_t>(value))) {
                size_t length = 0;
                if (!client.parser.take(dest, MAX_FRAME_SIZE, &length)) continue;
                if (client_index_out) *client_index_out = index;
                _poll_start = (index + 1) % MAX_CLIENTS;
                return length;
            }
        }
#else
        if (!client.in_use || !client.connected) continue;
        if (client.parser.available()) {
            size_t length = 0;
            if (client.parser.take(dest, MAX_FRAME_SIZE, &length)) {
                if (client_index_out) *client_index_out = index;
                _poll_start = (index + 1) % MAX_CLIENTS;
                return length;
            }
        }
#endif
    }
    _poll_start = (start + 1) % MAX_CLIENTS;
    return 0;
}

size_t TCPCompanionServer::writeToClient(int client_index, const uint8_t src[],
                                         size_t len)
{
    if (!_started || client_index < 0 || client_index >= MAX_CLIENTS ||
        !src || len == 0 || len > MAX_FRAME_SIZE ||
        !networkTransportAllowed() ||
        !isClientConnected(client_index)) return 0;
    uint8_t encoded[MAX_FRAME_SIZE + 3];
    const size_t encoded_len = encodeCompanionTransportFrame(
        encoded, sizeof(encoded), src, len);
    if (encoded_len == 0) return 0;
#if defined(ESP32_PLATFORM)
    if (!writeBytes(_clients[client_index].client, encoded, encoded_len)) return 0;
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

size_t TCPCompanionServer::writeToAllClients(const uint8_t src[], size_t len)
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

bool TCPCompanionServer::isClientConnected(int client_index) const
{
    if (!networkTransportAllowed() || client_index < 0 ||
        client_index >= MAX_CLIENTS) return false;
    const ClientSlot& client = _clients[client_index];
#if defined(ESP32_PLATFORM)
    return client.in_use && client.client.connected();
#else
    return client.in_use && client.connected;
#endif
}

int TCPCompanionServer::connectedCount() const
{
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (isClientConnected(i)) ++count;
    }
    return count;
}

uint32_t TCPCompanionServer::clientGeneration(int client_index) const
{
    if (client_index < 0 || client_index >= MAX_CLIENTS ||
        !isClientConnected(client_index)) return 0;
    return _clients[client_index].generation;
}

void TCPCompanionServer::disconnectClient(int client_index)
{
    resetClient(client_index);
}

#if !defined(ESP32_PLATFORM)
bool TCPCompanionServer::testConnect(int client_index)
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

bool TCPCompanionServer::testFeed(int client_index, const uint8_t* data, size_t len)
{
    if (!data || !isClientConnected(client_index)) return false;
    ClientSlot& client = _clients[client_index];
    bool completed = false;
    for (size_t i = 0; i < len; ++i) {
        if (client.parser.push(data[i])) completed = true;
    }
    return completed;
}

size_t TCPCompanionServer::testReadTx(int client_index, uint8_t* out, size_t capacity)
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
