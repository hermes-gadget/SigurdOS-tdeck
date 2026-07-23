// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>

namespace sigurdos {
namespace ui {

constexpr std::size_t MAX_TERMINAL_LINES = 64;

// Evict at most one row before one append. In particular, this deliberately
// does not loop on child_count(): an asynchronous deleter may leave the count
// unchanged until a later LVGL tick and would otherwise livelock the caller.
template <typename Object, typename ChildCount, typename FirstChild,
          typename DeleteNow>
bool terminal_prune_oldest_for_append(Object* log,
                                      ChildCount child_count,
                                      FirstChild first_child,
                                      DeleteNow delete_now)
{
    if (!log) return false;
    if (static_cast<std::size_t>(child_count(log)) < MAX_TERMINAL_LINES) {
        return true;
    }

    Object* first = first_child(log);
    if (!first) return false;
    delete_now(first);
    return true;
}

} // namespace ui
} // namespace sigurdos
