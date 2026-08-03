// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

#include "transport_iface.h"

class BaseSerialInterface;

namespace sigurdos {
namespace comms {

// Internal seams used by CompanionBridge.  The public transport contract stays
// intentionally small so UI code cannot depend on a concrete socket server.
void transports_attach_serial(BaseSerialInterface* serial);
bool transports_attached();
bool transports_client_connected(TransportId id, int client_index);
uint32_t transports_client_generation(TransportId id, int client_index);
bool transports_current_request(TransportId* id, int* client_index,
                                uint32_t* generation);
bool transports_send_to_result(TransportId id, int client_index,
                               const uint8_t* frame, size_t len);
bool transports_broadcast_result(TransportId id, const uint8_t* frame, size_t len);

using TransportServiceHandler = void (*)();
void transports_set_service_handler(TransportServiceHandler handler);

#if !defined(ESP32_PLATFORM)
// Host-only seams for exercising client routing without a WiFi stack.
void transports_test_reset();
bool transports_test_connect(TransportId id, int client_index);
bool transports_test_feed(TransportId id, int client_index,
                          const uint8_t* data, size_t len);
size_t transports_test_read_tx(TransportId id, int client_index,
                               uint8_t* out, size_t capacity);
#endif

} // namespace comms
} // namespace sigurdos
