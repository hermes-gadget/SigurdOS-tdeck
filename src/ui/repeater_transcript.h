#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdio>

namespace sigurdos::ui {

inline bool format_repeater_cli_reply(char* out, size_t out_size, const char* text)
{
    if (!out || out_size == 0) return false;
    const int written = std::snprintf(out, out_size, "< [CLI] %s\n", text ? text : "");
    return written >= 0 && static_cast<size_t>(written) < out_size;
}

} // namespace sigurdos::ui
