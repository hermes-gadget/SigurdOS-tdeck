// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include "mesh/message_store.h"

#include <cstddef>
#include <cstdio>

namespace sigurdos::ui {

inline const char* message_text_type_name(uint8_t type)
{
    switch (type) {
        case 1: return "CLI";
        case 2: return "Signed";
        default: return "Plain";
    }
}

inline bool format_message_prefix(char* out, size_t size,
                                  const uint8_t prefix[6])
{
    if (!out || size < 13 || !prefix) return false;
    bool present = false;
    for (int i = 0; i < 6; ++i) present = present || prefix[i] != 0;
    if (!present) {
        std::snprintf(out, size, "Unavailable");
        return false;
    }
    std::snprintf(out, size, "%02X%02X%02X%02X%02X%02X",
                  prefix[0], prefix[1], prefix[2], prefix[3], prefix[4], prefix[5]);
    return true;
}

inline void format_message_route(char* out, size_t size,
                                 const sigurdos::mesh::StoredMessage& msg)
{
    if (!out || size == 0) return;
    if (!msg.route_known) std::snprintf(out, size, "Unknown");
    else if (!msg.route_flood) std::snprintf(out, size, "Direct");
    else if (msg.path_len == 0xFF) std::snprintf(out, size, "Flood");
    else std::snprintf(out, size, "Flood (%u hop%s)", msg.path_len & 63,
                       (msg.path_len & 63) == 1 ? "" : "s");
}

inline void format_message_signal(char* out, size_t size,
                                  const sigurdos::mesh::StoredMessage& msg)
{
    if (!out || size == 0) return;
    if (msg.is_self || (msg.rssi == 0 && msg.snr_quarters == 0)) {
        std::snprintf(out, size, "Unavailable");
    } else {
        std::snprintf(out, size, "%d dBm / %.1f dB", (int)msg.rssi,
                      (double)msg.snr_quarters / 4.0);
    }
}

inline const char* message_delivery_name(const sigurdos::mesh::StoredMessage& msg)
{
    if (!msg.is_self) return "Received";
    if (msg.is_channel) return "Broadcast (no ACK)";
    return msg.acked ? "Acknowledged" : "Pending / unconfirmed";
}

} // namespace sigurdos::ui
