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

// ── Channel quick-action menu (Alt+C) ──────────────────────
// Pure, LVGL-free logic backing the in-chat channel menu. The chat
// screen builds the popup from channel_menu_build() and dispatches the
// user's choice through channel_menu_perform(). Keeping this free of
// LVGL lets the region/scope sequencing be unit-tested on the host.

#include <cstdint>

namespace sigurdos::ui {

// Actions offered for the active channel. Region actions are only
// applicable to public "#" channels (not DMs). MARK_READ is handled by
// the chat screen itself (it touches chat-local unread state).
enum class ChannelAction : uint8_t {
    None = 0,
    SetActiveRegion,    // scope my outgoing floods to this channel's region
    ClearActiveRegion,  // send Public (unscoped) again
    SetHomeRegion,      // make this channel's region my home region
    SetDefaultScope,    // make this channel's region my default flood scope
    MarkRead,           // clear this channel's unread badge (UI-only)
    LeaveChannel,       // remove this channel from the device
};

struct ChannelMenuItem {
    ChannelAction action;
    const char*   label;   // plain display label; the chat screen adds an icon
};

// True when `channel` is a public "#" channel that region actions apply
// to. False for DMs ("DM: name"), empty, or null.
bool channel_supports_regions(const char* channel);

// Fill `out` (up to `max` entries) with the menu items applicable to
// `channel`. Region actions are included only for "#" channels. Returns
// the number of items written.
int channel_menu_build(const char* channel, ChannelMenuItem* out, int max);

// Perform a mesh-backed channel action. Encapsulates the region
// sequencing the raw API requires (a region must exist before it can be
// set active / home / default, so this auto-creates it first).
// Returns true when the action was handled here; false for UI-only
// actions (MarkRead/None) and invalid inputs, which the caller handles.
bool channel_menu_perform(ChannelAction action, const char* channel, int channel_idx);

} // namespace sigurdos::ui
