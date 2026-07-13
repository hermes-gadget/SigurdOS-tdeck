#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>

namespace sigurdos::ui {

static constexpr uint32_t FINDER_PING_WINDOW_MS = 3000;

enum class FinderRefreshPhase {
    Active,
    Cooldown,
    Ready,
};

struct FinderRefreshSnapshot {
    FinderRefreshPhase phase = FinderRefreshPhase::Ready;
    uint32_t displayed_seconds = 0;
    int result_count = 0;

    bool operator==(const FinderRefreshSnapshot& other) const
    {
        return phase == other.phase &&
               displayed_seconds == other.displayed_seconds &&
               result_count == other.result_count;
    }

    bool operator!=(const FinderRefreshSnapshot& other) const
    {
        return !(*this == other);
    }

    bool needsPolling() const { return phase != FinderRefreshPhase::Ready; }
};

inline FinderRefreshSnapshot finder_refresh_snapshot(
    bool ping_active, uint32_t active_remaining_ms,
    bool on_cooldown, uint32_t cooldown_remaining_ms,
    int result_count)
{
    FinderRefreshSnapshot snapshot;
    snapshot.result_count = result_count;
    if (ping_active) {
        snapshot.phase = FinderRefreshPhase::Active;
        const uint32_t remaining = active_remaining_ms > FINDER_PING_WINDOW_MS
            ? FINDER_PING_WINDOW_MS : active_remaining_ms;
        snapshot.displayed_seconds = (FINDER_PING_WINDOW_MS - remaining) / 1000;
    } else if (on_cooldown) {
        snapshot.phase = FinderRefreshPhase::Cooldown;
        snapshot.displayed_seconds = (cooldown_remaining_ms + 999) / 1000;
    }
    return snapshot;
}

} // namespace sigurdos::ui
