// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "navigation_state.h"

namespace sigurdos::ui {

NavigationState::NavigationState(Screen initial) : current_(initial) {}

void NavigationState::reset(Screen initial)
{
    current_ = initial;
    history_size_ = 0;
    back_swipe_commit_ = 0;
}

void NavigationState::pushHistory(Screen screen)
{
    if (history_size_ < MAX_HISTORY) {
        history_[history_size_++] = screen;
        return;
    }
    for (std::size_t i = 1; i < MAX_HISTORY; ++i) {
        history_[i - 1] = history_[i];
    }
    history_[MAX_HISTORY - 1] = screen;
}

bool NavigationState::navigateTo(Screen screen)
{
    if (screen == current_) return false;
    back_swipe_commit_ = 0;
    pushHistory(current_);
    current_ = screen;
    return true;
}

bool NavigationState::goBack()
{
    if (!canGoBack()) return false;
    back_swipe_commit_ = 0;
    current_ = history_[--history_size_];
    return true;
}

BackSwipeResult NavigationState::handleBackSwipe(bool is_left)
{
    if (!is_left) {
        back_swipe_commit_ = 0;
        return BackSwipeResult::Ignored;
    }
    if (++back_swipe_commit_ < 2) return BackSwipeResult::Consumed;
    back_swipe_commit_ = 0;
    return goBack() ? BackSwipeResult::Navigated : BackSwipeResult::Completed;
}

Screen NavigationState::current() const { return current_; }
bool NavigationState::canGoBack() const { return history_size_ != 0; }
std::size_t NavigationState::historySize() const { return history_size_; }

} // namespace sigurdos::ui
