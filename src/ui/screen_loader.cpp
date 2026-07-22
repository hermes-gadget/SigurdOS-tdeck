// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "screens_common.h"

namespace sigurdos::ui {

void show_screen(lv_obj_t* scr)
{
    // Use auto_del=false for ALL screen loads and manage outgoing-screen
    // deletion manually. Previously, mixing auto_del=true/false across
    // different screen types caused LVGL's internal screen-load state
    // machine (d->scr_to_load, d->prev_scr, d->del_prev) to deadlock
    // after ~4-5 round-trips between animated and instant screen loads.
    //
    // For NONE animation the load completes synchronously. Delete the old
    // root synchronously as well: async deletion needs two LVGL allocations
    // and silently leaves the root alive if either allocation fails.
    lv_obj_t* old_scr = lv_screen_active();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    if (old_scr && old_scr != scr) {
        lv_obj_delete(old_scr);
    }
}

} // namespace sigurdos::ui
