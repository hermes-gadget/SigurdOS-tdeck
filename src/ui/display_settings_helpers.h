#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <cstdint>

namespace sigurdos::ui {

inline uint16_t normalize_auto_off_timeout(uint16_t value)
{
    switch (value) {
        case 0:
        case 15:
        case 30:
        case 60:
        case 120:
            return value;
        default:
            return 30;
    }
}

inline bool chat_history_cap_reduces_history(uint16_t original, uint16_t staged)
{
    return staged < original;
}

}  // namespace sigurdos::ui
