#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>

namespace sigurdos::ui {

enum class TracePollDecision { Pending, Accept, DiscardUnrelated, TimedOut };

inline TracePollDecision trace_poll_decision(uint32_t now_ms, uint32_t deadline_ms,
                                             bool has_result, uint32_t expected_tag,
                                             uint32_t result_tag)
{
    if (has_result) {
        return result_tag == expected_tag ? TracePollDecision::Accept
                                          : TracePollDecision::DiscardUnrelated;
    }
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0
        ? TracePollDecision::TimedOut : TracePollDecision::Pending;
}

} // namespace sigurdos::ui
