#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

namespace sigurdos::ui {

inline bool system_reboot_allowed(bool ota_active, bool github_ota_active)
{
    return !ota_active && !github_ota_active;
}

} // namespace sigurdos::ui
