// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos {
namespace mesh {
namespace node_discovery {

static constexpr uint8_t REQUEST = 0x80;
static constexpr uint8_t RESPONSE = 0x90;
static constexpr uint16_t RATE_MAX = 4;
static constexpr uint32_t RATE_WINDOW_SECS = 120;

struct Request {
    bool prefix_only = false;
    uint8_t filter = 0;
    uint32_t tag = 0;
    uint32_t since = 0;
};

inline bool parseRequest(const uint8_t* payload, size_t len, Request& out) {
    if (!payload || (len != 6 && len != 10) ||
        (payload[0] != REQUEST && payload[0] != (REQUEST | 1U))) return false;
    out.prefix_only = (payload[0] & 1U) != 0;
    out.filter = payload[1];
    std::memcpy(&out.tag, payload + 2, sizeof(out.tag));
    out.since = 0;
    if (len == 10) std::memcpy(&out.since, payload + 6, sizeof(out.since));
    return true;
}

inline bool matches(const Request& request, uint8_t node_type,
                    uint32_t modified_at) {
    return node_type > 0 && node_type < 8 && request.filter != 0 &&
        (request.filter & (uint8_t)(1U << node_type)) != 0 &&
        modified_at >= request.since;
}

inline size_t buildResponse(const Request& request, uint8_t node_type,
                            int8_t inbound_snr_x4, const uint8_t pub_key[32],
                            uint8_t* out, size_t capacity) {
    const size_t key_len = request.prefix_only ? 8U : 32U;
    const size_t required = 6U + key_len;
    if (!pub_key || !out || capacity < required || node_type == 0 ||
        node_type > 0x0F) return 0;
    out[0] = (uint8_t)(RESPONSE | node_type);
    out[1] = (uint8_t)inbound_snr_x4;
    std::memcpy(out + 2, &request.tag, sizeof(request.tag));
    std::memcpy(out + 6, pub_key, key_len);
    return required;
}

class RateLimiter {
public:
    bool allow(uint32_t now) {
        if (_count != 0 && (uint32_t)(now - _window_start) < RATE_WINDOW_SECS) {
            if (_count >= RATE_MAX) return false;
            ++_count;
            return true;
        }
        _window_start = now;
        _count = 1;
        return true;
    }

private:
    uint32_t _window_start = 0;
    uint16_t _count = 0;
};

} // namespace node_discovery
} // namespace mesh
} // namespace sigurdos
