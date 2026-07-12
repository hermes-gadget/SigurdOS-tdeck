// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "screen_lifetime.h"

namespace sigurdos {
namespace ui {

void ScreenLifetime::bind(lv_obj_t* screen)
{
    obj_count_ = 0;
    timer_count_ = 0;
    hook_ = nullptr;
    bound_screen_ = screen;
    bound_ = screen != nullptr;
    if (!screen) return;
    lv_obj_add_event_cb(screen, [](lv_event_t* e) {
        auto* self = static_cast<ScreenLifetime*>(lv_event_get_user_data(e));
        if (self) self->notifyDeleted(static_cast<lv_obj_t*>(lv_event_get_target(e)));
    }, LV_EVENT_DELETE, this);
}

bool ScreenLifetime::track(lv_obj_t** slot)
{
    if (!slot || obj_count_ >= MAX_TRACKED_OBJS) return false;
    obj_slots_[obj_count_++] = slot;
    return true;
}

bool ScreenLifetime::trackTimer(lv_timer_t** slot)
{
    if (!slot || timer_count_ >= MAX_TRACKED_TIMERS) return false;
    timer_slots_[timer_count_++] = slot;
    return true;
}

void ScreenLifetime::onDelete(void (*hook)())
{
    hook_ = hook;
}

void ScreenLifetime::notifyDeleted()
{
    for (int i = 0; i < obj_count_; i++) *obj_slots_[i] = nullptr;
    for (int i = 0; i < timer_count_; i++) {
        if (*timer_slots_[i]) {
            lv_timer_del(*timer_slots_[i]);
            *timer_slots_[i] = nullptr;
        }
    }
    void (*hook)() = hook_;
    obj_count_ = 0;
    timer_count_ = 0;
    hook_ = nullptr;
    bound_screen_ = nullptr;
    bound_ = false;
    if (hook) hook();
}

void ScreenLifetime::notifyDeleted(lv_obj_t* deleted_screen)
{
    if (!bound_ || deleted_screen != bound_screen_) return;
    notifyDeleted();
}

} // namespace ui
} // namespace sigurdos
