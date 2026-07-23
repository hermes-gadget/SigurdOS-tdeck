// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace mesh {

static constexpr uint32_t DISCOVERY_COMPLETION_RETENTION_MS = 30000;

inline bool pendingOperationElapsed(uint32_t started_at_ms,
                                    uint32_t timeout_ms,
                                    uint32_t now_ms)
{
    return timeout_ms > 0 &&
           static_cast<uint32_t>(now_ms - started_at_ms) >= timeout_ms;
}

// Prefer never-used slots so a recent timeout remains observable. A timeout
// is terminal and reclaimable when no clean slot remains; active/completed
// operations are never evicted.
template <typename Slot>
inline int selectReusablePendingSlot(const Slot* slots, size_t count)
{
    if (!slots) return -1;
    int timed_out_slot = -1;
    for (size_t i = 0; i < count; ++i) {
        if (slots[i].in_use) continue;
        if (!slots[i].timed_out) return static_cast<int>(i);
        if (timed_out_slot < 0) timed_out_slot = static_cast<int>(i);
    }
    return timed_out_slot;
}

} // namespace mesh
} // namespace sigurdos
