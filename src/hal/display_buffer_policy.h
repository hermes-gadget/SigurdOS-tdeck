// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace sigurdos::hal {

enum class DisplayBufferMode {
    FullDouble,
    FullSingle,
    Partial,
    Emergency,
    Failed,
};

inline DisplayBufferMode select_display_buffer_mode(
    bool full_first, bool full_second, bool partial, bool emergency)
{
    if (full_first && full_second) return DisplayBufferMode::FullDouble;
    if (full_first) return DisplayBufferMode::FullSingle;
    if (partial) return DisplayBufferMode::Partial;
    if (emergency) return DisplayBufferMode::Emergency;
    return DisplayBufferMode::Failed;
}

}  // namespace sigurdos::hal
