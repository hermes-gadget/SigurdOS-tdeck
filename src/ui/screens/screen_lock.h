#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "../../hal/trackball.h"
#include "../../power/screen_sleep.h"

namespace sigurdos::ui {

void lock_screen_show();
bool lock_screen_handle_trackball(SigurdOSTrackballEvent event);
bool lock_screen_is_active();
void lock_screen_note_wake(sigurdos::power::WakeReason reason);

} // namespace sigurdos::ui
