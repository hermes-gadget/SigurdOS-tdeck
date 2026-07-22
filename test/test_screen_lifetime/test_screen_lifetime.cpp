// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Tests for ScreenLifetime (ARCH-001, #820) — the guard that replaces the
// hand-rolled LV_EVENT_DELETE handlers nulling screens' file-scope LVGL
// pointers. notifyDeleted() is the delete-callback core; tests drive it
// directly instead of simulating LVGL events.

#include <gtest/gtest.h>
#include "ui/screen_lifetime.h"

namespace {

using sigurdos::ui::ScreenLifetime;

static int g_hook_runs = 0;
static void hook_fn() { g_hook_runs++; }

class ScreenLifetimeTest : public ::testing::Test {
protected:
    void SetUp() override { g_hook_runs = 0; }

    ScreenLifetime lt;
    lv_obj_t screen{};
    lv_obj_t widget_a{}, widget_b{};
    lv_timer_t timer{};
};

TEST_F(ScreenLifetimeTest, NullsTrackedWidgetSlotsOnDelete)
{
    lv_obj_t* slot_a = &widget_a;
    lv_obj_t* slot_b = &widget_b;
    lt.bind(&screen);
    EXPECT_TRUE(lt.track(&slot_a));
    EXPECT_TRUE(lt.track(&slot_b));
    EXPECT_EQ(lt.trackedCount(), 2);

    lt.notifyDeleted();
    EXPECT_EQ(slot_a, nullptr);
    EXPECT_EQ(slot_b, nullptr);
    EXPECT_FALSE(lt.isBound());
    EXPECT_EQ(lt.trackedCount(), 0);
}

TEST_F(ScreenLifetimeTest, DeletesAndNullsTrackedTimerSlots)
{
    lv_timer_t* slot = &timer;
    lt.bind(&screen);
    EXPECT_TRUE(lt.trackTimer(&slot));
    lt.notifyDeleted();
    EXPECT_EQ(slot, nullptr);

    // An already-null timer slot must be tolerated (screens null it early).
    lv_timer_t* empty = nullptr;
    lt.bind(&screen);
    EXPECT_TRUE(lt.trackTimer(&empty));
    lt.notifyDeleted();
    EXPECT_EQ(empty, nullptr);
}

TEST_F(ScreenLifetimeTest, RunsHookAfterSlotsReset)
{
    lv_obj_t* slot = &widget_a;
    lt.bind(&screen);
    lt.track(&slot);
    lt.onDelete(hook_fn);
    lt.notifyDeleted();
    EXPECT_EQ(slot, nullptr);
    EXPECT_EQ(g_hook_runs, 1);

    // The hook is part of the cleared registration — it must not fire again.
    lt.notifyDeleted();
    EXPECT_EQ(g_hook_runs, 1);
}

TEST_F(ScreenLifetimeTest, BindResetsPreviousRegistration)
{
    lv_obj_t* stale = &widget_a;
    lt.bind(&screen);
    lt.track(&stale);
    lt.onDelete(hook_fn);

    // A rebuilt screen re-registers from scratch; the stale slot and hook
    // from the previous instance must not survive.
    lv_obj_t* fresh = &widget_b;
    lt.bind(&screen);
    lt.track(&fresh);
    EXPECT_EQ(lt.trackedCount(), 1);

    lt.notifyDeleted();
    EXPECT_EQ(stale, &widget_a);
    EXPECT_EQ(fresh, nullptr);
    EXPECT_EQ(g_hook_runs, 0);
}

TEST_F(ScreenLifetimeTest, DelayedDeleteFromPreviousBindingIsIgnored)
{
    lv_obj_t old_screen{}, new_screen{};
    lv_obj_t* old_slot = &widget_a;
    lv_obj_t* new_slot = &widget_b;

    lt.bind(&old_screen);
    lt.track(&old_slot);
    lt.bind(&new_screen);
    lt.track(&new_slot);

    // lv_obj_del_async(old_screen) runs after the replacement was bound.
    lt.notifyDeleted(&old_screen);
    EXPECT_EQ(new_slot, &widget_b);
    EXPECT_TRUE(lt.isBound());

    lt.notifyDeleted(&new_screen);
    EXPECT_EQ(new_slot, nullptr);
    EXPECT_FALSE(lt.isBound());
}

TEST_F(ScreenLifetimeTest, ReparentedMapStateSurvivesItsPreviousRootDelete)
{
    lv_obj_t previous_root{}, current_root{};
    lv_obj_t shared_canvas{};
    lv_obj_t* active_canvas = &shared_canvas;

    lt.bind(&previous_root);
    lt.track(&active_canvas);
    lt.bind(&current_root);
    lt.track(&active_canvas);

    lt.notifyDeleted(&previous_root);
    EXPECT_EQ(active_canvas, &shared_canvas);
    EXPECT_TRUE(lt.isBound());

    lt.notifyDeleted(&current_root);
    EXPECT_EQ(active_canvas, nullptr);
}

TEST_F(ScreenLifetimeTest, TwentyRapidReplacementCyclesIgnoreStaleDeletes)
{
    lv_obj_t screens[20]{};
    lv_obj_t widgets[20]{};
    lv_obj_t* slot = nullptr;

    for (int cycle = 0; cycle < 20; cycle++) {
        slot = &widgets[cycle];
        lt.bind(&screens[cycle]);
        ASSERT_TRUE(lt.track(&slot));

        if (cycle > 0) {
            lt.notifyDeleted(&screens[cycle - 1]);
            EXPECT_EQ(slot, &widgets[cycle]) << "cycle " << cycle;
            EXPECT_TRUE(lt.isBound()) << "cycle " << cycle;
        }
    }

    lt.notifyDeleted(&screens[19]);
    EXPECT_EQ(slot, nullptr);
    EXPECT_FALSE(lt.isBound());
}

TEST_F(ScreenLifetimeTest, RejectsNullSlotsAndOverflow)
{
    lt.bind(&screen);
    EXPECT_FALSE(lt.track(nullptr));
    EXPECT_FALSE(lt.trackTimer(nullptr));

    lv_obj_t* slots[ScreenLifetime::MAX_TRACKED_OBJS + 1];
    for (auto& s : slots) s = &widget_a;
    for (int i = 0; i < ScreenLifetime::MAX_TRACKED_OBJS; i++) {
        EXPECT_TRUE(lt.track(&slots[i])) << "slot " << i;
    }
    EXPECT_FALSE(lt.track(&slots[ScreenLifetime::MAX_TRACKED_OBJS]));

    lv_timer_t* tslots[ScreenLifetime::MAX_TRACKED_TIMERS + 1];
    for (auto& t : tslots) t = &timer;
    for (int i = 0; i < ScreenLifetime::MAX_TRACKED_TIMERS; i++) {
        EXPECT_TRUE(lt.trackTimer(&tslots[i])) << "timer slot " << i;
    }
    EXPECT_FALSE(lt.trackTimer(&tslots[ScreenLifetime::MAX_TRACKED_TIMERS]));
}

TEST_F(ScreenLifetimeTest, NotifyWithoutBindIsANoOp)
{
    lt.notifyDeleted();          // must not crash
    EXPECT_FALSE(lt.isBound());

    lt.bind(nullptr);            // null screen: nothing to guard
    EXPECT_FALSE(lt.isBound());
    lt.notifyDeleted();
    EXPECT_EQ(g_hook_runs, 0);
}

TEST_F(ScreenLifetimeTest, ReusableAcrossShowCycles)
{
    for (int cycle = 0; cycle < 20; cycle++) {
        lv_obj_t* slot = &widget_a;
        lt.bind(&screen);
        EXPECT_TRUE(lt.track(&slot));
        EXPECT_TRUE(lt.isBound());
        lt.notifyDeleted();
        EXPECT_EQ(slot, nullptr);
        EXPECT_FALSE(lt.isBound());
    }
}

} // namespace
