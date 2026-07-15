#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
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

// Channel quick-action menu. The LVGL-free logic here decides
// which menu actions are available and validates private per-chat scopes.

#include <cstddef>
#include <cstdint>

namespace sigurdos::ui {

enum class ChannelAction : uint8_t {
    None = 0,
    ChooseScope,   // open the private scope editor for this chat
    MarkRead,      // clear this chat unread badge (UI-only)
    LeaveChannel,  // remove this channel from the device
};

struct ChannelMenuItem {
    ChannelAction action;
    const char*   label;
};

// True when `channel` is a real conversation that can carry a private send
// scope. This includes #channels and DMs; empty/null names are rejected.
bool channel_supports_private_scope(const char* channel);

// Fill `out` (up to `max` entries) with the menu items applicable to
// `channel`. Private scope editing is included for every real conversation.
int channel_menu_build(const char* channel, ChannelMenuItem* out, int max);

// Perform a mesh-backed channel action. Returns true when the action was
// handled here; false for UI-only actions and invalid inputs.
bool channel_menu_perform(ChannelAction action, const char* channel, int channel_idx);

// Validate a private scope name. Empty/null is valid and means "clear this
// chat scope". Bare names and $names are accepted; public #names are rejected.
bool private_scope_name_valid(const char* name, const char** reason = nullptr);

// Normalize a user-entered private scope and derive its 16-byte key. Empty/null
// clears the scope and writes an empty out_name plus a zero key. Non-empty input
// writes "$<body>" to out_name and a stable 16-byte key to key16.
bool private_scope_prepare(const char* scope_name,
                           char* out_name, size_t out_name_len,
                           uint8_t key16[16],
                           const char** reason = nullptr);

} // namespace sigurdos::ui
