#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "screen_id.h"

#include <cstddef>

namespace sigurdos::ui {

static constexpr std::size_t HOME_ROUTE_COUNT = 12;

enum class HomeRouteFilter {
    Default,
    Channels,
    DirectMessages,
    Rooms,
};

struct HomeRoute {
    const char* label;
    bool badge;
    Screen target;
    HomeRouteFilter filter;
};

std::size_t homeRouteCount();
const HomeRoute* homeRouteAt(std::size_t index);
const HomeRoute* findHomeRoute(const char* label);

} // namespace sigurdos::ui
