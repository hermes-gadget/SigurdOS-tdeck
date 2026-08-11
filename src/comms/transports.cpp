// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "transport_iface.h"
#include "transports_internal.h"
#include "transport_tcp.h"
#include "transport_ws.h"

#include <helpers/BaseSerialInterface.h>

#include "hal/prefs.h"
#include "hal/wifi_ota.h"

#include <atomic>

#if defined(ESP32_PLATFORM)
#include <WiFi.h>
#endif

namespace sigurdos {
namespace comms {

namespace {

struct Registry {
    BaseSerialInterface* serial = nullptr;
    TCPCompanionServer tcp;
    WebSocketCompanionServer ws;
    TransportFrameHandler frame_handler = nullptr;
    TransportServiceHandler service_handler = nullptr;
    bool initialized = false;
    bool tcp_enabled = false;
    bool ws_enabled = false;
    bool tcp_started = false;
    bool ws_started = false;
    bool request_active = false;
    TransportId request_id = TransportId::BLE;
    int request_client = -1;
    uint32_t request_generation = 0;
};

Registry g_registry;
std::atomic<bool> g_ota_parked{false};

bool networkTransportsAllowed()
{
#if defined(ESP32_PLATFORM)
    return !g_ota_parked.load(std::memory_order_acquire) &&
           sigurdos::ota::companionTransportsAllowed();
#else
    return !g_ota_parked.load(std::memory_order_acquire);
#endif
}

bool wifiReady()
{
#if defined(ESP32_PLATFORM)
    if (WiFi.status() == WL_CONNECTED) return true;
    return (WiFi.getMode() & WIFI_AP) != 0;
#else
    return true;
#endif
}

void stopTcp()
{
    if (!g_registry.tcp_started) return;
    g_registry.tcp.stop();
    g_registry.tcp_started = false;
}

void stopWs()
{
    if (!g_registry.ws_started) return;
    g_registry.ws.stop();
    g_registry.ws_started = false;
}

#if defined(ESP32_PLATFORM)
void otaTransportParkHook(bool parked)
{
    g_ota_parked.store(parked, std::memory_order_release);
    if (!parked) return;

    // OTA sets its AP-active gate before invoking this callback. Close every
    // client and listener synchronously before AP setup makes the network
    // reachable. Unpark is restored by the normal non-blocking loop.
    stopTcp();
    stopWs();
}
#endif

void reconcileServers()
{
    const bool ready = wifiReady() && networkTransportsAllowed();
    if (!g_registry.tcp_enabled || !ready) {
        stopTcp();
    } else if (!g_registry.tcp_started) {
        g_registry.tcp.begin(TCPCompanionServer::DEFAULT_PORT);
        g_registry.tcp_started = true;
    }

    if (!g_registry.ws_enabled || !ready) {
        stopWs();
    } else if (!g_registry.ws_started) {
        g_registry.ws.begin(WebSocketCompanionServer::DEFAULT_PORT);
        g_registry.ws_started = true;
    }
}

bool validFrame(const uint8_t* frame, size_t len)
{
    return frame && len > 0 && len <= MAX_FRAME_SIZE;
}

void dispatchFrame(TransportId id, int client_index, uint32_t generation,
                   const uint8_t* frame, size_t len)
{
    if (id != TransportId::BLE && !networkTransportsAllowed()) return;
    if (!g_registry.frame_handler || !validFrame(frame, len)) return;
    g_registry.request_active = true;
    g_registry.request_id = id;
    g_registry.request_client = client_index;
    g_registry.request_generation = generation;
    g_registry.frame_handler(frame, len, id);
    g_registry.request_active = false;
    g_registry.request_client = -1;
    g_registry.request_generation = 0;
}

bool writeToBle(const uint8_t* frame, size_t len)
{
    return g_registry.serial && g_registry.serial->isEnabled() &&
           g_registry.serial->isConnected() &&
           g_registry.serial->writeFrame(frame, len) == len;
}

} // namespace

void transports_set_frame_handler(TransportFrameHandler handler)
{
    g_registry.frame_handler = handler;
}

void transports_set_service_handler(TransportServiceHandler handler)
{
    g_registry.service_handler = handler;
}

void transports_attach_serial(BaseSerialInterface* serial)
{
    g_registry.serial = serial;
}

bool transports_attached()
{
    return g_registry.serial != nullptr;
}

void transports_init()
{
    const NodePrefs& prefs = prefs_get();
    g_registry.tcp_enabled = prefs.transport_tcp_enabled;
    g_registry.ws_enabled = prefs.transport_ws_enabled;
    g_registry.initialized = true;
#if defined(ESP32_PLATFORM)
    sigurdos::ota::setCompanionTransportParkHook(&otaTransportParkHook);
#endif
    reconcileServers();
}

void transports_loop()
{
    if (!g_registry.initialized) transports_init();
    reconcileServers();
    const bool network_allowed = networkTransportsAllowed();

    uint8_t frame[MAX_FRAME_SIZE];
    if (g_registry.serial && g_registry.serial->isEnabled()) {
        const size_t len = g_registry.serial->checkRecvFrame(frame);
        if (len > 0) {
            dispatchFrame(TransportId::BLE, 0,
                          g_registry.serial->connectionGeneration(), frame, len);
        }
    }

    if (network_allowed && g_registry.tcp_started) {
        int client_index = -1;
        const size_t len = g_registry.tcp.pollRecvFrame(frame, &client_index);
        if (len > 0) {
            dispatchFrame(TransportId::TCP, client_index,
                          g_registry.tcp.clientGeneration(client_index), frame, len);
        }
    }

    if (network_allowed && g_registry.ws_started) {
        int client_index = -1;
        const size_t len = g_registry.ws.pollRecvFrame(frame, &client_index);
        if (len > 0) {
            dispatchFrame(TransportId::WS, client_index,
                          g_registry.ws.clientGeneration(client_index), frame, len);
        }
    }

    if (g_registry.service_handler) g_registry.service_handler();
}

bool transport_set_enabled(TransportId id, bool on)
{
    if (id == TransportId::BLE) {
        if (!g_registry.serial) return false;
        if (on) g_registry.serial->enable();
        else g_registry.serial->disable();
        return g_registry.serial->isEnabled() == on;
    }

    // Keep the public setter's failure contract honest: a persisted preference
    // may request a listener before WiFi is ready during boot, but an explicit
    // runtime enable only succeeds once the endpoint can actually start.
    if (on && (!wifiReady() || !networkTransportsAllowed())) return false;

    NodePrefs candidate = prefs_get();
    bool* cached = nullptr;
    if (id == TransportId::TCP) {
        candidate.transport_tcp_enabled = on;
        cached = &g_registry.tcp_enabled;
    } else if (id == TransportId::WS) {
        candidate.transport_ws_enabled = on;
        cached = &g_registry.ws_enabled;
    } else {
        return false;
    }
    if (!prefs_set(candidate)) return false;
    *cached = on;
    if (!on) {
        if (id == TransportId::TCP) stopTcp();
        else stopWs();
    } else {
        reconcileServers();
    }
    return true;
}

bool transport_enabled(TransportId id)
{
    if (id == TransportId::BLE) return g_registry.serial && g_registry.serial->isEnabled();
    if (id == TransportId::TCP) return g_registry.tcp_enabled;
    if (id == TransportId::WS) return g_registry.ws_enabled;
    return false;
}

TransportStatus transport_status(TransportId id)
{
    TransportStatus status{};
    status.enabled = transport_enabled(id);
    if (id == TransportId::BLE) {
        status.connected = g_registry.serial && g_registry.serial->isConnected();
        status.clientCount = status.connected ? 1 : 0;
        status.detail = status.enabled ? (status.connected ? "connected" : "advertising")
                                       : "disabled";
    } else if (id == TransportId::TCP) {
        const bool allowed = networkTransportsAllowed();
        status.clientCount = allowed ? g_registry.tcp.connectedCount() : 0;
        status.connected = status.clientCount > 0;
        status.detail = !status.enabled ? "disabled" :
            (!allowed ? "parked for OTA" :
             (g_registry.tcp_started ? "tcp://:5000" : "waiting for WiFi"));
    } else if (id == TransportId::WS) {
        const bool allowed = networkTransportsAllowed();
        status.clientCount = allowed ? g_registry.ws.connectedCount() : 0;
        status.connected = status.clientCount > 0;
        status.detail = !status.enabled ? "disabled" :
            (!allowed ? "parked for OTA" :
             (g_registry.ws_started ? "ws://:8765" : "waiting for WiFi"));
    } else {
        status.detail = "unknown";
    }
    return status;
}

bool transports_client_connected(TransportId id, int client_index)
{
    if (id == TransportId::BLE) {
        return client_index == 0 && g_registry.serial &&
               g_registry.serial->isEnabled() && g_registry.serial->isConnected();
    }
    if (id == TransportId::TCP) {
        return networkTransportsAllowed() &&
               g_registry.tcp.isClientConnected(client_index);
    }
    if (id == TransportId::WS) {
        return networkTransportsAllowed() &&
               g_registry.ws.isClientConnected(client_index);
    }
    return false;
}

uint32_t transports_client_generation(TransportId id, int client_index)
{
    if (id == TransportId::BLE) {
        return transports_client_connected(id, client_index) && g_registry.serial
            ? g_registry.serial->connectionGeneration() : 0;
    }
    if (id == TransportId::TCP) {
        return networkTransportsAllowed()
            ? g_registry.tcp.clientGeneration(client_index) : 0;
    }
    if (id == TransportId::WS) {
        return networkTransportsAllowed()
            ? g_registry.ws.clientGeneration(client_index) : 0;
    }
    return 0;
}

bool transports_current_request(TransportId* id, int* client_index,
                                uint32_t* generation)
{
    if (!g_registry.request_active) return false;
    if (id) *id = g_registry.request_id;
    if (client_index) *client_index = g_registry.request_client;
    if (generation) *generation = g_registry.request_generation;
    return true;
}

bool transports_send_to_result(TransportId id, int client_index,
                               const uint8_t* frame, size_t len)
{
    if (!validFrame(frame, len)) return false;
    if (id == TransportId::BLE) {
        return client_index == 0 && writeToBle(frame, len);
    }
    if (id == TransportId::TCP) {
        if (!networkTransportsAllowed()) return false;
        return g_registry.tcp.writeToClient(client_index, frame, len) == len;
    }
    if (id == TransportId::WS) {
        if (!networkTransportsAllowed()) return false;
        return g_registry.ws.writeToClient(client_index, frame, len) == len;
    }
    return false;
}

bool transports_broadcast_result(TransportId id, const uint8_t* frame, size_t len)
{
    if (!validFrame(frame, len)) return false;
    if (id == TransportId::BLE) return writeToBle(frame, len);
    if (id == TransportId::TCP) {
        if (!networkTransportsAllowed()) return false;
        return g_registry.tcp.writeToAllClients(frame, len) == len;
    }
    if (id == TransportId::WS) {
        if (!networkTransportsAllowed()) return false;
        return g_registry.ws.writeToAllClients(frame, len) == len;
    }
    return false;
}

void transports_broadcast(TransportId id, const uint8_t* frame, size_t len)
{
    (void)transports_broadcast_result(id, frame, len);
}

void transport_send_to(TransportId id, int client_index,
                       const uint8_t* frame, size_t len)
{
    (void)transports_send_to_result(id, client_index, frame, len);
}

#if !defined(ESP32_PLATFORM)
void transports_test_reset()
{
    stopTcp();
    stopWs();
    g_registry.serial = nullptr;
    g_registry.frame_handler = nullptr;
    g_registry.service_handler = nullptr;
    g_registry.initialized = false;
    g_registry.tcp_enabled = false;
    g_registry.ws_enabled = false;
    g_registry.tcp_started = false;
    g_registry.ws_started = false;
    g_registry.request_active = false;
    g_registry.request_id = TransportId::BLE;
    g_registry.request_client = -1;
    g_registry.request_generation = 0;
    g_ota_parked.store(false, std::memory_order_release);
}

bool transports_test_connect(TransportId id, int client_index)
{
    if (!g_registry.initialized) transports_init();
    if (id == TransportId::TCP) {
        if (!g_registry.tcp_enabled) return false;
        if (!g_registry.tcp_started) g_registry.tcp.begin();
        g_registry.tcp_started = true;
        return g_registry.tcp.testConnect(client_index);
    }
    if (id == TransportId::WS) {
        if (!g_registry.ws_enabled) return false;
        if (!g_registry.ws_started) g_registry.ws.begin();
        g_registry.ws_started = true;
        return g_registry.ws.testConnect(client_index);
    }
    return false;
}

bool transports_test_feed(TransportId id, int client_index,
                          const uint8_t* data, size_t len)
{
    if (id == TransportId::TCP) return g_registry.tcp.testFeed(client_index, data, len);
    if (id == TransportId::WS) return g_registry.ws.testFeed(client_index, data, len);
    return false;
}

size_t transports_test_read_tx(TransportId id, int client_index,
                               uint8_t* out, size_t capacity)
{
    if (id == TransportId::TCP) return g_registry.tcp.testReadTx(client_index, out, capacity);
    if (id == TransportId::WS) return g_registry.ws.testReadTx(client_index, out, capacity);
    return 0;
}
#endif

} // namespace comms
} // namespace sigurdos
