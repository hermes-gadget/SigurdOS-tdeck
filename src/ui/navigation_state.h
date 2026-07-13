#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "screen_id.h"

#include <cstddef>

namespace sigurdos::ui {

enum class BackSwipeResult {
    Ignored,
    Consumed,
    Completed,
    Navigated,
};

// Pure navigation policy shared by firmware and native tests. Rendering stays
// in navigation.cpp; this class owns every state-machine decision.
class NavigationState {
public:
    static constexpr std::size_t MAX_HISTORY = 16;

    explicit NavigationState(Screen initial = Screen::Home);

    void reset(Screen initial = Screen::Home);
    bool navigateTo(Screen screen);
    bool goBack();
    BackSwipeResult handleBackSwipe(bool is_left);

    Screen current() const;
    bool canGoBack() const;
    std::size_t historySize() const;

private:
    void pushHistory(Screen screen);

    Screen current_;
    Screen history_[MAX_HISTORY]{};
    std::size_t history_size_ = 0;
    unsigned back_swipe_commit_ = 0;
};

} // namespace sigurdos::ui
