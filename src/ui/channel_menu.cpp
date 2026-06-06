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

// The body of a scope name, ignoring an optional leading '#'. A scope is
// just a name: MeshCore derives its transport key from "#<body>" (SHA256),
// so the user can type "eng-sw" or "#eng-sw" interchangeably.
static const char* scope_body(const char* name)
{
    return (name && name[0] == '#') ? name + 1 : name;
}

bool scope_name_valid(const char* name, const char** reason)
{
    // Empty/null is the "Public (unscoped)" sentinel — always allowed.
    if (!name || !name[0]) return true;

    const char* body = scope_body(name);
    if (!body[0]) {
        if (reason) *reason = "Name required";
        return false;
    }
    // Stored as "#<body>" in a 31-byte field (30 chars + null), so the body
    // is capped at 29 characters.
    if (strlen(body) > 29) {
        if (reason) *reason = "Name too long";
        return false;
    }
    // The body follows the same rules as a channel name: letters, digits and
    // single hyphens, no leading/trailing/double hyphens.
    return sigurdos::mesh::channel_name_valid(body, reason);
}

bool channel_scope_apply(const char* scope_name)
{
    if (!scope_name || !scope_name[0]) {
        // Public / wildcard — send unscoped.
        sigurdos::mesh::setActiveRegion("");
        return true;
    }
    if (!scope_name_valid(scope_name)) return false;

    // Normalise to "#<body>" so the transport key auto-derives (SHA256 of the
    // name) and the scope matches the same-named channel/region. addRegion is
    // a no-op when the region already exists.
    char norm[32];
    snprintf(norm, sizeof(norm), "#%s", scope_body(scope_name));
    sigurdos::mesh::addRegion(norm, nullptr);
    sigurdos::mesh::setActiveRegion(norm);
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
