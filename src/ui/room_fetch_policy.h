#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

namespace sigurdos::ui {

enum class RoomFetchUiAction {
    KeepDialog,
    ConfirmAndNavigate,
};

inline RoomFetchUiAction room_fetch_ui_action(bool request_accepted)
{
    return request_accepted ? RoomFetchUiAction::ConfirmAndNavigate
                            : RoomFetchUiAction::KeepDialog;
}

} // namespace sigurdos::ui
