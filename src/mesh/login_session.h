// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace mesh {
namespace login_session {

enum Status : uint8_t {
    NONE = 0,
    PENDING = 1,
    OK = 2,
    FAILED = 3,
    TIMED_OUT = 4,
    DROPPED = 5,
};

inline bool isTerminal(Status status)
{
    return status == FAILED || status == TIMED_OUT || status == DROPPED;
}

// Prefer never-used slots so terminal results remain observable when capacity
// permits. Once the table is otherwise full, a terminal result is reclaimable;
// pending and active sessions are never evicted.
template <typename Entry>
inline int selectReservationSlot(const Entry* entries, size_t count)
{
    if (!entries) return -1;
    for (size_t i = 0; i < count; ++i) {
        if (!entries[i].in_use) return static_cast<int>(i);
    }
    for (size_t i = 0; i < count; ++i) {
        if (isTerminal(static_cast<Status>(entries[i].status))) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static constexpr uint32_t MIN_TIMEOUT_MS = 10000;
static constexpr uint32_t TIMEOUT_GRACE_MS = 2000;
static constexpr uint32_t MAX_TIMEOUT_MS = 120000;

inline uint32_t normalizeTimeout(uint32_t estimated_ms)
{
    uint32_t timeout = estimated_ms;
    if (timeout > MAX_TIMEOUT_MS - TIMEOUT_GRACE_MS) timeout = MAX_TIMEOUT_MS;
    else timeout += TIMEOUT_GRACE_MS;
    if (timeout < MIN_TIMEOUT_MS) timeout = MIN_TIMEOUT_MS;
    if (timeout > MAX_TIMEOUT_MS) timeout = MAX_TIMEOUT_MS;
    return timeout;
}

inline bool elapsed(uint32_t started_at_ms, uint32_t timeout_ms, uint32_t now_ms)
{
    return timeout_ms > 0 && (uint32_t)(now_ms - started_at_ms) >= timeout_ms;
}

inline Status evaluate(Status status, uint32_t started_at_ms, uint32_t timeout_ms,
                       bool keep_alive_expected, bool connection_present,
                       uint32_t now_ms)
{
    if (status == PENDING && elapsed(started_at_ms, timeout_ms, now_ms)) {
        return TIMED_OUT;
    }
    if (status == OK && keep_alive_expected && !connection_present) {
        return DROPPED;
    }
    return status;
}

} // namespace login_session
} // namespace mesh
} // namespace sigurdos
