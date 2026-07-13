#pragma once

#include <cstdint>
#include <cstddef>

namespace sigurdos {
namespace mesh {

static constexpr uint32_t PENDING_REQUEST_MIN_LIFETIME_MS = 30000;
static constexpr uint32_t PENDING_REQUEST_MAX_LIFETIME_MS = 300000;
static constexpr uint32_t DISCOVERY_COMPLETION_RETENTION_MS = 30000;

inline uint32_t pendingRequestLifetimeMs(uint32_t estimated_timeout_ms)
{
    uint64_t lifetime = (uint64_t)estimated_timeout_ms * 2u + 10000u;
    if (lifetime < PENDING_REQUEST_MIN_LIFETIME_MS) {
        lifetime = PENDING_REQUEST_MIN_LIFETIME_MS;
    }
    if (lifetime > PENDING_REQUEST_MAX_LIFETIME_MS) {
        lifetime = PENDING_REQUEST_MAX_LIFETIME_MS;
    }
    return (uint32_t)lifetime;
}

inline bool pendingRequestDeadlineReached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

// Callers run on the mesh task. Marking in_use here, before returning the
// index, makes capacity reservation indivisible from the caller's perspective.
// Prefer a clean slot so recently timed-out results remain observable.
template <typename Slot>
inline int reservePendingOperationSlot(Slot* slots, size_t count)
{
    if (!slots) return -1;
    int timed_out_slot = -1;
    for (size_t i = 0; i < count; ++i) {
        if (slots[i].in_use) continue;
        if (!slots[i].timed_out) {
            slots[i].in_use = true;
            return (int)i;
        }
        if (timed_out_slot < 0) timed_out_slot = (int)i;
    }
    if (timed_out_slot >= 0) {
        slots[timed_out_slot].in_use = true;
    }
    return timed_out_slot;
}

} // namespace mesh
} // namespace sigurdos
