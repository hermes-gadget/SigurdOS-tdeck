#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "navigation.h"

#include <cstddef>

namespace sigurdos::ui {

static constexpr std::size_t HOME_ROUTE_COUNT = 12;

struct HomeTileFilters {
    int chat_filter;
    bool rooms_only;
};

struct HomeRoute {
    const char* label;
    bool badge;
    Screen target;
    HomeTileFilters filters;
};

std::size_t homeRouteCount();
const HomeRoute* homeRouteAt(std::size_t index);
const HomeRoute* findHomeRoute(const char* label);
HomeTileFilters home_tile_filters(int tile_index);

} // namespace sigurdos::ui
