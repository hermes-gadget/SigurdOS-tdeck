#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <lvgl.h>

namespace sigurdos::ui {

// Couples one LVGL timer to an object-owned context. Destroying the context,
// replacing the timer, or completing the callback deletes the timer exactly
// once and clears the retained handle first.
class LvTimerOwner {
public:
    LvTimerOwner() = default;
    LvTimerOwner(const LvTimerOwner&) = delete;
    LvTimerOwner& operator=(const LvTimerOwner&) = delete;

    ~LvTimerOwner() { cancel(); }

    void attach(lv_timer_t* timer)
    {
        if (timer_ == timer) return;
        cancel();
        timer_ = timer;
    }

    void cancel()
    {
        lv_timer_t* timer = timer_;
        timer_ = nullptr;
        if (timer) lv_timer_del(timer);
    }

    void complete(lv_timer_t* timer)
    {
        if (timer_ == timer) timer_ = nullptr;
        if (timer) lv_timer_del(timer);
    }

    lv_timer_t* get() const { return timer_; }

private:
    lv_timer_t* timer_ = nullptr;
};

}  // namespace sigurdos::ui
