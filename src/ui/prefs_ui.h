#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "notifications.h"
#include "../hal/prefs.h"

namespace sigurdos::ui {

static constexpr char PREFS_UI_SAVE_ERROR[] = "Settings could not be saved";

using PrefsUiErrorReporter = void (*)(NotificationEvent, const char*);

// Persist a candidate preference set before applying any dependent live/UI
// state.  The reporter overload is intentionally injectable for native tests.
inline bool prefs_ui_commit(const sigurdos::NodePrefs& candidate,
                            PrefsUiErrorReporter report_error)
{
    if (sigurdos::prefs_set(candidate)) return true;
    if (report_error) {
        report_error(NotificationEvent::UiError, PREFS_UI_SAVE_ERROR);
    }
    return false;
}

inline bool prefs_ui_commit(const sigurdos::NodePrefs& candidate)
{
    return prefs_ui_commit(candidate, notifications_post);
}

} // namespace sigurdos::ui
