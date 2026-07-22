// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <lvgl.h>

namespace sigurdos::wifi_sta {
bool connected = false;
int rssi = -70;

bool isConnected() { return connected; }
int getRSSI() { return rssi; }
}

#include "../../src/ui/wifi_status_icon.cpp"

namespace {

class WifiIconLifetimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        lvgl_mock::reset();
        sigurdos::wifi_sta::connected = false;
        sigurdos::wifi_sta::rssi = -70;
        sigurdos::ui::screens_clear_wifi_icon();
    }
};

TEST_F(WifiIconLifetimeTest, InitialAndPeriodicUpdatesReflectConnection) {
    lv_obj_t* icon = lv_label_create(nullptr);
    sigurdos::ui::wifi_status_icon_attach(icon);
    EXPECT_TRUE(lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));

    sigurdos::wifi_sta::connected = true;
    sigurdos::wifi_sta::rssi = -55;
    sigurdos::ui::update_wifi_status();
    EXPECT_FALSE(lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));
    EXPECT_STREQ(icon->text, LV_SYMBOL_WIFI " -55");
    EXPECT_EQ(icon->text_color, sigurdos::theme::ACCENT_GREEN);
}

TEST_F(WifiIconLifetimeTest, DelayedOldScreenDeletionKeepsNewIconOwned) {
    lv_obj_t* old_icon = lv_label_create(nullptr);
    sigurdos::ui::wifi_status_icon_attach(old_icon);
    lv_obj_t* new_icon = lv_label_create(nullptr);
    sigurdos::ui::wifi_status_icon_attach(new_icon);

    lv_obj_delete(old_icon);
    sigurdos::wifi_sta::connected = true;
    sigurdos::wifi_sta::rssi = -65;
    sigurdos::ui::update_wifi_status();
    EXPECT_STREQ(new_icon->text, LV_SYMBOL_WIFI " -65");
    EXPECT_TRUE(lv_obj_is_valid(new_icon));
}

TEST_F(WifiIconLifetimeTest, DeleteThenPeriodicUpdateDoesNotTouchDeadObject) {
    lv_obj_t* icon = lv_label_create(nullptr);
    sigurdos::ui::wifi_status_icon_attach(icon);
    lv_obj_delete(icon);
    const int writes_after_delete = lvgl_mock::label_set_text_calls;

    sigurdos::wifi_sta::connected = true;
    sigurdos::ui::update_wifi_status();
    EXPECT_EQ(lvgl_mock::label_set_text_calls, writes_after_delete);
    EXPECT_FALSE(lv_obj_is_valid(icon));
}

TEST_F(WifiIconLifetimeTest, StaleDeleteCannotClearReusedPointerAddress) {
    lv_obj_t* old_icon = lv_label_create(nullptr);
    sigurdos::ui::wifi_status_icon_attach(old_icon);
    const lv_mock_event_slot_t stale_delete = lvgl_mock::event_slot(old_icon, 0);
    lv_obj_delete(old_icon);

    lvgl_mock::reset();
    lv_obj_t* reused_icon = lv_label_create(nullptr);
    ASSERT_EQ(reused_icon, old_icon);
    sigurdos::ui::wifi_status_icon_attach(reused_icon);

    lvgl_mock::dispatch(stale_delete, reused_icon);
    sigurdos::wifi_sta::connected = true;
    sigurdos::wifi_sta::rssi = -50;
    sigurdos::ui::update_wifi_status();
    EXPECT_STREQ(reused_icon->text, LV_SYMBOL_WIFI " -50");
}

TEST_F(WifiIconLifetimeTest, ExplicitClearInvalidatesPendingCallbacks) {
    lv_obj_t* icon = lv_label_create(nullptr);
    sigurdos::ui::wifi_status_icon_attach(icon);
    const int writes = lvgl_mock::label_set_text_calls;
    sigurdos::ui::screens_clear_wifi_icon();

    sigurdos::wifi_sta::connected = true;
    sigurdos::ui::update_wifi_status();
    EXPECT_EQ(lvgl_mock::label_set_text_calls, writes);
}

}  // namespace
