#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Nulls manually-owned LVGL pointers when their screen is deleted.
//
// Screens keep file-scope statics (widgets, timers) that dangle when the
// outgoing screen is deleted asynchronously, so every screen hand-rolled an
// LV_EVENT_DELETE handler to null them (ARCH-001, #820). A ScreenLifetime
// instance replaces those handlers:
//
//   static ScreenLifetime g_lifetime;
//   ...
//   g_lifetime.bind(scr);                 // fresh registration per show()
//   g_lifetime.track(&g_list);            // nulled on delete
//   g_lifetime.trackTimer(&g_timer);      // lv_timer_del()'d + nulled
//   g_lifetime.onDelete([]{ g_count = -1; });  // extra reset hook
//
// notifyDeleted() is the delete-callback core and is public so native tests
// can drive it without a real LVGL event loop. The screen-aware overload is
// used by the LVGL callback to ignore a delayed delete from an older binding.

#include <lvgl.h>

namespace sigurdos {
namespace ui {

class ScreenLifetime {
public:
    static constexpr int MAX_TRACKED_OBJS = 24;
    static constexpr int MAX_TRACKED_TIMERS = 4;

    // Start a fresh registration for this screen instance and install its
    // LV_EVENT_DELETE callback. Clears any previous registration, so a
    // screen's show() can call it unconditionally on every rebuild. A screen
    // with a live timer must cancel that timer before rebinding; unlike widget
    // slots, an overwritten timer slot cannot safely identify the old timer.
    void bind(lv_obj_t* screen);

    // Track a widget pointer slot: *slot = nullptr on delete.
    // Returns false (and tracks nothing) when slot is null or the table is
    // full — callers overflowing MAX_TRACKED_OBJS must raise the cap.
    bool track(lv_obj_t** slot);

    // Track a timer slot: on delete, lv_timer_del(*slot) when set, then null.
    bool trackTimer(lv_timer_t** slot);

    // Optional screen-specific cleanup; runs after the tracked slots reset.
    void onDelete(void (*hook)());

    // Delete-callback core: null every tracked widget slot, delete + null
    // every tracked timer, clear the registration, then run the hook.
    void notifyDeleted();

    // Delete only when deleted_screen is the currently bound screen. Screen
    // deletion is asynchronous, so an older screen's callback can arrive
    // after the same ScreenLifetime has already been rebound to a new screen.
    void notifyDeleted(lv_obj_t* deleted_screen);

    bool isBound() const { return bound_; }
    int trackedCount() const { return obj_count_ + timer_count_; }

private:
    lv_obj_t** obj_slots_[MAX_TRACKED_OBJS] = {};
    lv_timer_t** timer_slots_[MAX_TRACKED_TIMERS] = {};
    int obj_count_ = 0;
    int timer_count_ = 0;
    void (*hook_)() = nullptr;
    lv_obj_t* bound_screen_ = nullptr;
    bool bound_ = false;
};

} // namespace ui
} // namespace sigurdos
