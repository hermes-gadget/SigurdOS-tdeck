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

#include "channel_menu.h"

#include <cstring>

#include "../mesh/mesh_wrapper.h"
#include "../mesh/regions.h"

namespace sigurdos::ui {

bool channel_supports_regions(const char* channel)
{
    // Region/flood scope is keyed on the channel hash, which only exists
    // for public "#" channels. DMs ("DM: name") and unset names have none.
    return channel && channel[0] == '#';
}

int channel_menu_build(const char* channel, ChannelMenuItem* out, int max)
{
    if (!out || max <= 0) return 0;

    int n = 0;
    auto push = [&](ChannelAction a, const char* label) {
        if (n < max) {
            out[n].action = a;
            out[n].label  = label;
            n++;
        }
    };

    if (channel_supports_regions(channel)) {
        push(ChannelAction::SetActiveRegion,   "Scope sends here");
        push(ChannelAction::ClearActiveRegion, "Send Public (unscoped)");
        push(ChannelAction::SetHomeRegion,     "Set as home region");
        push(ChannelAction::SetDefaultScope,   "Set as default scope");
    }
    push(ChannelAction::MarkRead,     "Mark all read");
    push(ChannelAction::LeaveChannel, "Leave channel");

    return n;
}

bool channel_menu_perform(ChannelAction action, const char* channel, int channel_idx)
{
    switch (action) {
    case ChannelAction::SetActiveRegion:
        if (!channel_supports_regions(channel)) return false;
        // setActiveRegion only scopes when the region (and its transport
        // key) already exists, so create it first. addRegion is a no-op
        // when the region is already present.
        sigurdos::mesh::addRegion(channel, nullptr);
        sigurdos::mesh::setActiveRegion(channel);
        return true;

    case ChannelAction::ClearActiveRegion:
        // Back to wildcard/Public — send unscoped.
        sigurdos::mesh::setActiveRegion("");
        return true;

    case ChannelAction::SetHomeRegion:
        if (!channel_supports_regions(channel)) return false;
        // setHomeRegion requires an existing region to bind to.
        sigurdos::mesh::addRegion(channel, nullptr);
        return sigurdos::mesh::setHomeRegion(channel);

    case ChannelAction::SetDefaultScope:
        if (!channel_supports_regions(channel)) return false;
        // setDefaultScope auto-creates, but addRegion derives the
        // transport key for "#" names so scoped sends actually encrypt.
        sigurdos::mesh::addRegion(channel, nullptr);
        return sigurdos::mesh::setDefaultScope(channel);

    case ChannelAction::LeaveChannel:
        if (channel_idx < 0) return false;
        return sigurdos::mesh::removeChannel(channel_idx);

    case ChannelAction::MarkRead:
    case ChannelAction::None:
    default:
        // UI-only or no-op — the caller handles it.
        return false;
    }
}

} // namespace sigurdos::ui
