// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstdint>

inline uint32_t sigurdos_gps_normalize_interval(uint32_t seconds)
{
    return seconds > 86400 ? 86400 : seconds;
}

inline uint32_t sigurdos_gps_effective_poll_ms(bool background_enabled,
                                               uint32_t background_seconds,
                                               bool high_rate, bool one_shot_sync)
{
    if (high_rate || one_shot_sync) return 200;
    return background_enabled
        ? sigurdos_gps_normalize_interval(background_seconds) * 1000u
        : 0;
}

inline bool sigurdos_gps_poll_due(uint32_t now, uint32_t last, uint32_t interval)
{
    return interval > 0 && (last == 0 || (uint32_t)(now - last) >= interval);
}
