#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstring>

namespace sigurdos::ui {

inline bool repeater_password_action(const char* command_prefix)
{
    return command_prefix &&
           (std::strcmp(command_prefix, "password ") == 0 ||
            std::strcmp(command_prefix, "set guest.password ") == 0);
}

inline const char* repeater_password_feedback(bool sent)
{
    return sent ? "Credential update sent" : "Credential update failed";
}

} // namespace sigurdos::ui
