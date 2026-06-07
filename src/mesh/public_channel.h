#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstring>

namespace sigurdos::mesh {

static constexpr char PUBLIC_CHANNEL_NAME[] = "Public";
static constexpr char PUBLIC_CHANNEL_PSK_BASE64[] = "izOH6cXN6mrJ5e26oRXNcg==";

inline bool isPublicChannelName(const char* name)
{
    return name && std::strcmp(name, PUBLIC_CHANNEL_NAME) == 0;
}

} // namespace sigurdos::mesh
