#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace sigurdos::ui {

inline bool format_repeater_cli_line(char* out, size_t out_size, uint32_t timestamp,
                                     const char* marker, const char* text)
{
    if (!out || out_size == 0) return false;
    const uint32_t seconds = timestamp % 86400u;
    const int written = timestamp == 0
        ? std::snprintf(out, out_size, "[--:--:--] %s%s\n",
                        marker ? marker : "", text ? text : "")
        : std::snprintf(out, out_size, "[%02u:%02u:%02u] %s%s\n",
                        seconds / 3600u, (seconds / 60u) % 60u, seconds % 60u,
                        marker ? marker : "", text ? text : "");
    return written >= 0 && static_cast<size_t>(written) < out_size;
}

inline bool format_repeater_cli_command(char* out, size_t out_size, uint32_t timestamp,
                                        const char* text)
{
    return format_repeater_cli_line(out, out_size, timestamp, "> ", text);
}

inline bool format_repeater_cli_reply(char* out, size_t out_size, uint32_t timestamp,
                                      const char* text)
{
    return format_repeater_cli_line(out, out_size, timestamp, "< [CLI] ", text);
}

} // namespace sigurdos::ui
