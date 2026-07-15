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

#include <cstdio>
#include <cstring>

#include "../mesh/channel_validation.h"
#include "../mesh/mesh_wrapper.h"
#include "../mesh/public_channel.h"

namespace sigurdos::ui {

static const char* private_scope_body(const char* name)
{
    return (name && name[0] == 0x24) ? name + 1 : name;
}

bool channel_supports_private_scope(const char* channel)
{
    return channel && channel[0];
}

static uint64_t fnv1a64(const char* text, uint64_t seed)
{
    uint64_t h = 1469598103934665603ULL ^ seed;
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        h ^= (uint64_t)*p++;
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static void derive_private_scope_key(const char* name, uint8_t key16[16])
{
    uint64_t a = mix64(fnv1a64(name, 0x7369677572646f73ULL));
    uint64_t b = mix64(fnv1a64(name, 0x7072697661746524ULL));
    for (int i = 0; i < 8; i++) {
        key16[i] = (uint8_t)(a >> (i * 8));
        key16[i + 8] = (uint8_t)(b >> (i * 8));
    }
}

bool private_scope_name_valid(const char* name, const char** reason)
{
    if (!name || !name[0]) return true;
    if (name[0] == '#') {
        if (reason) *reason = "Private scopes only";
        return false;
    }

    const char* body = private_scope_body(name);
    if (!body[0]) {
        if (reason) *reason = "Name required";
        return false;
    }
    if (std::strlen(body) > 29) {
        if (reason) *reason = "Name too long";
        return false;
    }
    return sigurdos::mesh::channel_name_valid(body, reason);
}

bool private_scope_prepare(const char* scope_name,
                           char* out_name, size_t out_name_len,
                           uint8_t key16[16],
                           const char** reason)
{
    if (!out_name || out_name_len == 0 || !key16) {
        if (reason) *reason = "Internal error";
        return false;
    }

    out_name[0] = 0;
    std::memset(key16, 0, 16);

    if (!scope_name || !scope_name[0]) {
        return true;
    }
    if (!private_scope_name_valid(scope_name, reason)) {
        return false;
    }

    const char* body = private_scope_body(scope_name);
    int written = std::snprintf(out_name, out_name_len, "$%s", body);
    if (written < 0 || (size_t)written >= out_name_len) {
        if (reason) *reason = "Name too long";
        out_name[0] = 0;
        return false;
    }

    derive_private_scope_key(out_name, key16);
    return true;
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

    if (channel_supports_private_scope(channel)) {
        push(ChannelAction::ChooseScope, "Private scope...");
    }
    push(ChannelAction::MarkRead,     "Mark all read");
    if (!sigurdos::mesh::isPublicChannelName(channel)) {
        push(ChannelAction::LeaveChannel, "Leave channel");
    }

    return n;
}

bool channel_menu_perform(ChannelAction action, const char* channel, int channel_idx)
{
    switch (action) {
    case ChannelAction::LeaveChannel:
        if (channel_idx < 0) return false;
        if (sigurdos::mesh::isPublicChannelName(channel)) return false;
        return sigurdos::mesh::removeChannel(channel_idx);

    case ChannelAction::ChooseScope:
    case ChannelAction::MarkRead:
    case ChannelAction::None:
    default:
        return false;
    }
}

} // namespace sigurdos::ui
