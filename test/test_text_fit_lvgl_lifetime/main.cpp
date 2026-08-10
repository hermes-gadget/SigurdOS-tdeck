// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include <lvgl.h>

// Keep the fixture small enough to exercise overflow without constructing a
// production-sized screen. The firmware default remains 512 tracked labels.
#define SIGURDOS_TEXT_FIT_LABEL_STATE_CAPACITY 2
#include "../../src/ui/text_fit.cpp"
#include "../../src/ui/text_fit_lvgl.cpp"

namespace {

lv_mock_event_slot_t latest_event(lv_obj_t* object, int filter)
{
    lv_mock_event_slot_t result{};
    for (int i = 0; i < lvgl_mock::event_slot_count; ++i) {
        const auto& slot = lvgl_mock::event_slots[i];
        if (slot.obj == object && slot.filter == filter) result = slot;
    }
    return result;
}

class TextFitLvglLifetimeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        lvgl_mock::reset();
    }

    void TearDown() override
    {
        lvgl_mock::force_async_cancel_failure = false;
        lvgl_mock::drain_async();
        for (lv_obj_t* label : labels_) {
            if (lv_obj_is_valid(label)) lv_obj_delete(label);
        }
        lvgl_mock::drain_async();
        lvgl_mock::reset();
    }

    lv_obj_t* create_label()
    {
        lv_obj_t* label = sigurdos::ui::text_fit_label_create(nullptr);
        if (label && label_count_ < 4) labels_[label_count_++] = label;
        return label;
    }

    static void prepare_label(lv_obj_t* label, const char* text)
    {
        sigurdos::ui::text_fit_set_font(label, &lv_font_montserrat_14, 0);
        sigurdos::ui::text_fit_set_text(label, text);
    }

private:
    lv_obj_t* labels_[4] = {};
    int label_count_ = 0;
};

TEST_F(TextFitLvglLifetimeTest, DrawRefitsOnlyWhenInputsChange)
{
    lv_obj_t* label = create_label();
    ASSERT_NE(label, nullptr);
    prepare_label(label, "unchanged");
    const lv_mock_event_slot_t draw = latest_event(label, LV_EVENT_DRAW_MAIN);
    ASSERT_NE(draw.cb, nullptr);

    lvgl_mock::dispatch(draw, label);
    lvgl_mock::dispatch(draw, label);
    lvgl_mock::dispatch(draw, label);

    EXPECT_EQ(lvgl_mock::async_call_count, 0);
    EXPECT_EQ(lvgl_mock::async_pending_count(), 0);
    EXPECT_EQ(lvgl_mock::invalid_object_access_count, 0);

    // Exercise a direct LVGL update such as lv_label_set_text_fmt(), which
    // cannot be routed through the text-fit wrapper.
    lv_label_set_text(label, "after");
    lvgl_mock::dispatch(draw, label);
    lvgl_mock::dispatch(draw, label);
    lvgl_mock::dispatch(draw, label);

    EXPECT_EQ(lvgl_mock::async_call_count, 1);
    EXPECT_EQ(lvgl_mock::async_pending_count(), 1);
    lvgl_mock::drain_async();
    EXPECT_EQ(lvgl_mock::async_execute_count, 1);

    lvgl_mock::dispatch(draw, label);
    EXPECT_EQ(lvgl_mock::async_call_count, 1);
    EXPECT_EQ(lvgl_mock::async_pending_count(), 0);
    EXPECT_EQ(lvgl_mock::invalid_object_access_count, 0);

    lv_obj_set_width(label, 60);
    lvgl_mock::dispatch(draw, label);
    lvgl_mock::dispatch(draw, label);

    EXPECT_EQ(lvgl_mock::async_call_count, 2);
    EXPECT_EQ(lvgl_mock::async_pending_count(), 1);
    lvgl_mock::drain_async();
    EXPECT_EQ(lvgl_mock::async_execute_count, 2);

    lvgl_mock::dispatch(draw, label);
    EXPECT_EQ(lvgl_mock::async_call_count, 2);
    EXPECT_EQ(lvgl_mock::async_pending_count(), 0);
    EXPECT_EQ(lvgl_mock::invalid_object_access_count, 0);
}

TEST_F(TextFitLvglLifetimeTest, DeleteCancelsQueuedFitBeforeAsyncDrain)
{
    lv_obj_t* label = create_label();
    ASSERT_NE(label, nullptr);
    prepare_label(label, "deleted");
    const lv_mock_event_slot_t draw = latest_event(label, LV_EVENT_DRAW_MAIN);
    ASSERT_NE(draw.cb, nullptr);
    lv_label_set_text(label, "deleted while pending");
    lvgl_mock::dispatch(draw, label);
    ASSERT_EQ(lvgl_mock::async_pending_count(), 1);

    lv_obj_delete(label);
    EXPECT_EQ(lvgl_mock::async_cancel_count, 1);
    EXPECT_EQ(lvgl_mock::async_pending_count(), 0);
    lvgl_mock::drain_async();

    EXPECT_EQ(lvgl_mock::async_execute_count, 0);
    EXPECT_EQ(lvgl_mock::invalid_object_access_count, 0);
}

TEST_F(TextFitLvglLifetimeTest, RetiredGenerationRejectsStaleWorkAfterPointerReuse)
{
    lv_obj_t* old_label = create_label();
    ASSERT_NE(old_label, nullptr);
    prepare_label(old_label, "old");
    const lv_mock_event_slot_t old_draw =
        latest_event(old_label, LV_EVENT_DRAW_MAIN);
    ASSERT_NE(old_draw.cb, nullptr);
    lv_label_set_text(old_label, "old pending");
    lvgl_mock::dispatch(old_draw, old_label);
    ASSERT_EQ(lvgl_mock::async_pending_count(), 1);

    // Exercise the generation guard independently from LVGL cancellation.
    lvgl_mock::force_async_cancel_failure = true;
    lv_obj_delete(old_label);
    lvgl_mock::force_async_cancel_failure = false;
    ASSERT_EQ(lvgl_mock::async_pending_count(), 1);

    lv_obj_t* reused_label = create_label();
    ASSERT_EQ(reused_label, old_label);
    prepare_label(reused_label, "new");
    const lv_mock_event_slot_t new_draw =
        latest_event(reused_label, LV_EVENT_DRAW_MAIN);
    ASSERT_NE(new_draw.cb, nullptr);
    lv_label_set_text(reused_label, "new pending");
    lvgl_mock::dispatch(new_draw, reused_label);
    ASSERT_EQ(lvgl_mock::async_pending_count(), 2);

    const int accesses_before_drain = lvgl_mock::invalid_object_access_count;
    const int text_reads_before_drain = lvgl_mock::label_get_text_calls;
    lvgl_mock::drain_async();

    EXPECT_EQ(lvgl_mock::invalid_object_access_count, accesses_before_drain);
    // Only the new generation may read the reused label. The retired callback
    // must not mistake the recycled address for its old label.
    EXPECT_EQ(lvgl_mock::label_get_text_calls, text_reads_before_drain + 1);
    EXPECT_TRUE(lv_obj_is_valid(reused_label));
}

TEST_F(TextFitLvglLifetimeTest, StateOverflowNeverQueuesUntrackedLabel)
{
    lv_obj_t* first = create_label();
    lv_obj_t* second = create_label();
    lv_obj_t* overflow = create_label();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(overflow, nullptr);

    EXPECT_NE(latest_event(first, LV_EVENT_DRAW_MAIN).cb, nullptr);
    EXPECT_NE(latest_event(second, LV_EVENT_DRAW_MAIN).cb, nullptr);
    EXPECT_EQ(latest_event(overflow, LV_EVENT_DRAW_MAIN).cb, nullptr);

    lv_obj_delete(overflow);
    sigurdos::ui::text_fit_apply(overflow);
    lvgl_mock::drain_async();
    EXPECT_EQ(lvgl_mock::async_call_count, 0);
    EXPECT_EQ(lvgl_mock::invalid_object_access_count, 0);
}

} // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
