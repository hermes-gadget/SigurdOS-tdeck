// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "screen_lock.h"

#include "../screen_lifetime.h"
#include "../screens_common.h"
#include "../theme.h"
#include "../../fonts/emoji_font.h"
#include "../../hal/battery.h"
#include "../../hal/prefs.h"
#include "../../mesh/mesh_wrapper.h"
#include "../pin_gate_policy.h"
#include "../navigation.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sigurdos::ui {
namespace {

using sigurdos::power::WakeReason;

static constexpr int SWIPE_MIN_PX = 40;
static constexpr uint32_t LOCKOUT_STATUS_REFRESH_MS = 1000;

static lv_obj_t* g_lock_root = nullptr;
static lv_obj_t* g_pin_input = nullptr;
static lv_obj_t* g_unlock_button = nullptr;
static lv_obj_t* g_status_label = nullptr;
static lv_obj_t* g_clock_label = nullptr;
static lv_obj_t* g_battery_label = nullptr;
static lv_timer_t* g_clock_timer = nullptr;
static ScreenLifetime g_lock_lifetime;
static PinGateState g_lock_pin_gate;
static WakeReason g_last_wake_reason = WakeReason::None;
static int g_gesture_start_x = 0;
static int g_gesture_start_y = 0;
static bool g_gesture_active = false;

static bool pin_configured()
{
    return sigurdos::prefs_get().device_pin != 0;
}

static void update_clock()
{
    if (!g_clock_label || !lv_obj_is_valid(g_clock_label)) return;

    const uint32_t epoch = sigurdos::mesh::getCurrentTime();
    char text[8];
    if (epoch == 0) {
        snprintf(text, sizeof(text), "--:--");
    } else {
        const uint32_t seconds = epoch % 86400u;
        snprintf(text, sizeof(text), "%02lu:%02lu",
                 (unsigned long)((seconds / 3600u) % 24u),
                 (unsigned long)((seconds / 60u) % 60u));
    }
    lv_label_set_text(g_clock_label, text);
}

static void update_battery()
{
    if (!g_battery_label || !lv_obj_is_valid(g_battery_label)) return;

    char text[24];
    snprintf(text, sizeof(text), "BAT %u%%", (unsigned)sigurdos_battery_pct());
    lv_label_set_text(g_battery_label, text);
}

static void set_status(const char* text)
{
    if (!g_status_label || !lv_obj_is_valid(g_status_label)) return;
    lv_label_set_text(g_status_label, text ? text : "");
}

static void set_default_status()
{
    if (pin_configured()) {
        set_status("Swipe to focus PIN · type digits to unlock");
    } else {
        set_status("Swipe up or press a key to unlock");
    }
}

static void set_wake_status(WakeReason reason)
{
    char text[96];
    if (pin_configured()) {
        snprintf(text, sizeof(text), "Woke on %s · enter PIN or swipe",
                 sigurdos::power::wake_reason_name(reason));
    } else {
        snprintf(text, sizeof(text), "Woke on %s · swipe or press a key",
                 sigurdos::power::wake_reason_name(reason));
    }
    set_status(text);
}

static void set_pin_lockout_status(uint32_t now_ms)
{
    const uint32_t remaining = pinGateRemaining(g_lock_pin_gate, now_ms);
    char text[56];
    if (remaining != 0) {
        snprintf(text, sizeof(text), "PIN locked · retry in %lus",
                 (unsigned long)((remaining + 999u) / 1000u));
    } else {
        snprintf(text, sizeof(text), "%u failed attempts · try again",
                 (unsigned)g_lock_pin_gate.failures);
    }
    set_status(text);
}

static void focus_pin_input()
{
    if (!g_pin_input || !lv_obj_is_valid(g_pin_input)) return;
    lv_group_t* group = lv_group_get_default();
    if (group) lv_group_focus_obj(g_pin_input);
    set_status("PIN focused · type 4-6 digits");
}

static void unlock_from_input()
{
    if (!lock_screen_is_active()) return;
    sigurdos::ui::lock_screen_unlocked();
}

static void pin_value_changed(lv_event_t* event)
{
    lv_obj_t* textarea = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (!textarea || !lv_obj_is_valid(textarea) || !pin_configured()) return;

    const char* entered = lv_textarea_get_text(textarea);
    if (!entered) return;

    char expected[7];
    snprintf(expected, sizeof(expected), "%lu",
             (unsigned long)sigurdos::prefs_get().device_pin);
    if (strlen(entered) != strlen(expected)) return;

    const uint32_t now = millis();
    if (pinGateLocked(g_lock_pin_gate, now)) {
        lv_textarea_set_text(textarea, "");
        set_pin_lockout_status(now);
        return;
    }

    const uint32_t value = static_cast<uint32_t>(strtoul(entered, nullptr, 10));
    if (value == sigurdos::prefs_get().device_pin) {
        pinGateRecordSuccess(g_lock_pin_gate, now);
        unlock_from_input();
        return;
    }

    pinGateRecordFailure(g_lock_pin_gate, now);
    lv_textarea_set_text(textarea, "");
    set_pin_lockout_status(now);
}

static void unlock_button_event(lv_event_t* event)
{
    const int code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_KEY) {
        unlock_from_input();
    }
}

static bool is_pointer_gesture_event(int code)
{
    return code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
           code == LV_EVENT_RELEASED;
}

static void lock_gesture_event(lv_event_t* event)
{
    const int code = lv_event_get_code(event);
    if (!is_pointer_gesture_event(code)) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t point{};
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        g_gesture_start_x = point.x;
        g_gesture_start_y = point.y;
        g_gesture_active = true;
        return;
    }

    if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
        if (!g_gesture_active) return;
        const int dx = point.x - g_gesture_start_x;
        const int dy = point.y - g_gesture_start_y;
        if (code == LV_EVENT_RELEASED) g_gesture_active = false;

        const bool swiped = std::abs(dx) >= SWIPE_MIN_PX ||
                            std::abs(dy) >= SWIPE_MIN_PX;
        if (swiped && code == LV_EVENT_RELEASED) {
            if (pin_configured()) {
                focus_pin_input();
            } else {
                unlock_from_input();
            }
        }
    }
}

static void lock_timer_cb(lv_timer_t*)
{
    update_clock();
    update_battery();
    if (pin_configured() && pinGateLocked(g_lock_pin_gate, millis())) {
        set_pin_lockout_status(millis());
    }
}

static void lock_deleted()
{
    g_gesture_active = false;
    g_gesture_start_x = 0;
    g_gesture_start_y = 0;
}

static void style_label(lv_obj_t* label, const lv_font_t* font,
                        uint32_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

} // namespace

void lock_screen_show()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    g_lock_root = scr;
    g_pin_input = nullptr;
    g_unlock_button = nullptr;
    g_status_label = nullptr;
    g_clock_label = nullptr;
    g_battery_label = nullptr;
    g_clock_timer = nullptr;
    g_lock_lifetime.bind(scr);
    g_lock_lifetime.track(&g_lock_root);
    g_lock_lifetime.track(&g_pin_input);
    g_lock_lifetime.track(&g_unlock_button);
    g_lock_lifetime.track(&g_status_label);
    g_lock_lifetime.track(&g_clock_label);
    g_lock_lifetime.track(&g_battery_label);
    g_lock_lifetime.trackTimer(&g_clock_timer);
    g_lock_lifetime.onDelete(lock_deleted);

    sigurdos::theme::apply_lock_screen_bg(scr);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, lock_gesture_event, LV_EVENT_ALL, nullptr);

    g_battery_label = lv_label_create(scr);
    style_label(g_battery_label, emoji_wrapped_montserrat_12,
                sigurdos::theme::TEXT_PRIMARY);
    lv_obj_align(g_battery_label, LV_ALIGN_TOP_LEFT, 10, 8);
    update_battery();

    g_clock_label = lv_label_create(scr);
    style_label(g_clock_label, emoji_wrapped_montserrat_12,
                sigurdos::theme::TEXT_PRIMARY);
    lv_obj_align(g_clock_label, LV_ALIGN_TOP_RIGHT, -10, 8);
    update_clock();

    lv_obj_t* panel = lv_obj_create(scr);
    lv_obj_set_size(panel, LV_PCT(84), 146);
    lv_obj_center(panel);
    sigurdos::theme::apply_lock_screen_panel(panel);
    lv_obj_add_event_cb(panel, lock_gesture_event, LV_EVENT_ALL, nullptr);

    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "SIGURDOS LOCKED");
    style_label(title, emoji_wrapped_montserrat_16, sigurdos::theme::ACCENT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* subtitle = lv_label_create(panel);
    lv_label_set_text(subtitle, "Idle power-save active");
    style_label(subtitle, emoji_wrapped_montserrat_10,
                sigurdos::theme::TEXT_SECONDARY);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 28);

    if (pin_configured()) {
        g_pin_input = lv_textarea_create(panel);
        lv_obj_set_size(g_pin_input, 150, 38);
        lv_obj_align(g_pin_input, LV_ALIGN_CENTER, 0, 10);
        lv_obj_set_style_text_align(g_pin_input, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(g_pin_input, emoji_wrapped_montserrat_14, 0);
        lv_textarea_set_password_mode(g_pin_input, true);
        lv_textarea_set_one_line(g_pin_input, true);
        lv_textarea_set_max_length(g_pin_input, 6);
        lv_textarea_set_accepted_chars(g_pin_input, "0123456789");
        lv_textarea_set_placeholder_text(g_pin_input, "----");
        sigurdos::theme::apply_lock_screen_input(g_pin_input);
        lv_obj_add_event_cb(g_pin_input, lock_gesture_event, LV_EVENT_ALL,
                            nullptr);
        lv_obj_add_event_cb(g_pin_input, pin_value_changed,
                            LV_EVENT_VALUE_CHANGED, nullptr);

        lv_group_t* group = lv_group_get_default();
        if (group) {
            lv_group_add_obj(group, g_pin_input);
            lv_group_focus_obj(g_pin_input);
        }
    } else {
        g_unlock_button = lv_btn_create(panel);
        lv_obj_set_size(g_unlock_button, 190, 38);
        lv_obj_align(g_unlock_button, LV_ALIGN_CENTER, 0, 10);
        sigurdos::theme::apply_pixel_btn_outline(g_unlock_button);
        lv_obj_add_event_cb(g_unlock_button, unlock_button_event,
                            LV_EVENT_ALL, nullptr);
        lv_obj_t* button_label = lv_label_create(g_unlock_button);
        lv_label_set_text(button_label, "SWIPE / PRESS KEY");
        style_label(button_label, emoji_wrapped_montserrat_10,
                    sigurdos::theme::TEXT_PRIMARY);
        lv_obj_center(button_label);

        lv_group_t* group = lv_group_get_default();
        if (group) {
            lv_group_add_obj(group, g_unlock_button);
            lv_group_focus_obj(g_unlock_button);
        }
    }

    g_status_label = lv_label_create(scr);
    lv_obj_set_width(g_status_label, LV_PCT(94));
    lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, 0);
    style_label(g_status_label, emoji_wrapped_montserrat_10,
                sigurdos::theme::TEXT_SECONDARY);
    lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_MID, 0, -14);
    if (g_last_wake_reason == WakeReason::None) set_default_status();
    else set_wake_status(g_last_wake_reason);

    g_clock_timer = lv_timer_create(lock_timer_cb, LOCKOUT_STATUS_REFRESH_MS,
                                    nullptr);
    show_screen(scr);
}

bool lock_screen_handle_trackball(SigurdOSTrackballEvent event)
{
    if (!lock_screen_is_active()) return false;
    if (event == SigurdOSTrackballEvent::None) return true;

    if (pin_configured()) {
        focus_pin_input();
        return true;
    }

    unlock_from_input();
    return true;
}

bool lock_screen_is_active()
{
    return g_lock_root && lv_obj_is_valid(g_lock_root);
}

void lock_screen_note_wake(WakeReason reason)
{
    g_last_wake_reason = reason;
    if (lock_screen_is_active()) set_wake_status(reason);
}

} // namespace sigurdos::ui
