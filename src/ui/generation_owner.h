#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>

namespace sigurdos::ui {

inline uint32_t next_ui_generation(uint32_t current)
{
    ++current;
    return current == 0 ? 1 : current;
}

inline bool ui_generation_matches(const void* expected_owner,
                                  uint32_t expected_generation,
                                  const void* actual_owner,
                                  uint32_t actual_generation)
{
    return expected_owner && expected_owner == actual_owner &&
           expected_generation == actual_generation;
}

inline bool ui_deferred_action_matches(bool timer_is_current,
                                       bool route_is_current,
                                       const void* expected_owner,
                                       uint32_t expected_generation,
                                       const void* actual_owner,
                                       uint32_t actual_generation)
{
    return timer_is_current && route_is_current &&
           ui_generation_matches(expected_owner, expected_generation,
                                 actual_owner, actual_generation);
}

} // namespace sigurdos::ui
