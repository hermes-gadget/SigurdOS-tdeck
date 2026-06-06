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
#include "../mesh/channel_validation.h"

namespace sigurdos::ui {

bool scope_name_valid(const char* name, const char** reason)
{
    // Empty/null is the "Public (unscoped)" sentinel — always allowed.
    if (!name || !name[0]) return true;

    char prefix = name[0];
    if (prefix != '#' && prefix != '$') {
        if (reason) *reason = "Use #public or $private";
        return false;
    }
    if (!name[1]) {
        if (reason) *reason = "Name required";
        return false;
    }
    if (strlen(name) > 30) {  // RegionEntry::name is 31 bytes (30 + null)
        if (reason) *reason = "Name too long";
        return false;
    }
    // The body (after the prefix) follows the same rules as a channel name:
    // letters, digits and single hyphens. channel_name_valid strips a leading
    // '#', which is harmless for the already-prefix-stripped body.
    return sigurdos::mesh::channel_name_valid(name + 1, reason);
}

bool channel_scope_apply(const char* scope_name)
{
    if (!scope_name || !scope_name[0]) {
        // Public / wildcard — send unscoped.
        sigurdos::mesh::setActiveRegion("");
        return true;
    }
    if (!scope_name_valid(scope_name)) return false;

    // Auto-create so the region (and, for #public names, its transport key)
    // exists before we bind it; addRegion is a no-op if already present.
    sigurdos::mesh::addRegion(scope_name, nullptr);
    sigurdos::mesh::setActiveRegion(scope_name);
    return true;
}

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
        push(ChannelAction::ChooseScope,     "Send scope...");
        push(ChannelAction::SetHomeRegion,   "Set as home region");
        push(ChannelAction::SetDefaultScope, "Set as default scope");
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
        // Scope outgoing floods to this channel's own region.
        return channel_scope_apply(channel);

    case ChannelAction::ClearActiveRegion:
        // Back to wildcard/Public — send unscoped.
        return channel_scope_apply("");

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

    case ChannelAction::ChooseScope:
    case ChannelAction::MarkRead:
    case ChannelAction::None:
    default:
        // UI-only or no-op — the caller handles it (opens the scope picker,
        // clears the unread badge, etc.).
        return false;
    }
}

} // namespace sigurdos::ui
