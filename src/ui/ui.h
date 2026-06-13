#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.


#include "../hal/trackball.h"
#include "screens.h"  // screen navigation + update_wifi_status()
#include <cstdint>

namespace sigurdos {
namespace ui {

static constexpr uint32_t UI_SPLASH_DURATION_MS = 2000;

inline bool ui_splash_transition_elapsed(uint32_t now_ms,
                                         uint32_t start_ms,
                                         uint32_t duration_ms = UI_SPLASH_DURATION_MS)
{
    return static_cast<uint32_t>(now_ms - start_ms) > duration_ms;
}

void init();
void set_boot_status(const char* status);
void load_persisted_state();
void loop();
bool handle_trackball_event(SigurdOSTrackballEvent event);

} // namespace ui
} // namespace sigurdos
