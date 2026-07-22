// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdio>

namespace sigurdos::ui {

inline bool repeater_command_feedback(char* out, size_t out_size, bool sent,
                                      const char* command,
                                      const char* success_format)
{
    if (!out || out_size == 0 || !command || !success_format) return false;
    const int written = sent
        ? std::snprintf(out, out_size, success_format, command)
        : std::snprintf(out, out_size, "Send failed: %s", command);
    return written >= 0 && static_cast<size_t>(written) < out_size;
}

} // namespace sigurdos::ui
