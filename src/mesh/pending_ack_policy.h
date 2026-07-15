#pragma once

#include <cstdint>

namespace sigurdos {
namespace mesh {

static constexpr uint32_t PENDING_ACK_MIN_LIFETIME_MS = 30000;
static constexpr uint32_t PENDING_ACK_MAX_LIFETIME_MS = 300000;

inline uint32_t pendingAckLifetimeMs(uint32_t estimated_timeout_ms)
{
    uint64_t lifetime = (uint64_t)estimated_timeout_ms * 2u + 10000u;
    if (lifetime < PENDING_ACK_MIN_LIFETIME_MS) {
        lifetime = PENDING_ACK_MIN_LIFETIME_MS;
    }
    if (lifetime > PENDING_ACK_MAX_LIFETIME_MS) {
        lifetime = PENDING_ACK_MAX_LIFETIME_MS;
    }
    return (uint32_t)lifetime;
}

inline bool pendingAckDeadlineReached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

} // namespace mesh
} // namespace sigurdos
