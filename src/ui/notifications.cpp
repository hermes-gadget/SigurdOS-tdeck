// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "notifications.h"
#include "home_screen.h"
#include "screens.h"
#include "theme.h"
#include "responsive.h"
#include "../fonts/emoji_font.h"
#include "../hal/battery.h"
#include "../hal/buzzer.h"
#include "../hal/github_ota.h"
#include "../hal/prefs.h"
#include "../hal/sdcard.h"
#include "../mesh/mesh_wrapper.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>
#include <cstring>

namespace sigurdos::ui {

namespace {

NotificationQueue g_queue;
lv_obj_t* g_toast = nullptr;
lv_obj_t* g_toast_screen = nullptr;
bool g_unread_mention = false;
bool g_companion_seeded = false;
bool g_companion_connected = false;
bool g_low_battery_latched = false;
bool g_storage_latched = false;
bool g_ota_failure_latched = false;
uint32_t g_last_observe_ms = 0;

void destroy_toast()
{
    if (g_toast && lv_obj_is_valid(g_toast)) lv_obj_del_async(g_toast);
    g_toast = nullptr;
    g_toast_screen = nullptr;
}

uint32_t level_color(NotificationLevel level)
{
    switch (level) {
        case NotificationLevel::Important: return theme::ACCENT_ORANGE;
        case NotificationLevel::Critical:  return theme::ACCENT_RED;
        default:                           return theme::ACCENT;
    }
}

void render_toast()
{
    if (!g_queue.has_current()) {
        destroy_toast();
        return;
    }
    lv_obj_t* screen = lv_screen_active();
    if (!screen) return;
    if (g_toast && (!lv_obj_is_valid(g_toast) || g_toast_screen != screen)) {
        g_toast = nullptr;
        g_toast_screen = nullptr;
    }
    if (g_toast) return;

    const NotificationItem& item = g_queue.current();
    g_toast = lv_obj_create(screen);
    g_toast_screen = screen;
    lv_obj_set_size(g_toast, responsive::DISPLAY_W - 16, 42);
    lv_obj_align(g_toast, LV_ALIGN_TOP_MID, 0, responsive::TOP_BAR_H + 4);
    lv_obj_set_style_bg_color(g_toast, lv_color_hex(theme::BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(g_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_toast, lv_color_hex(level_color(item.level)), 0);
    lv_obj_set_style_border_width(g_toast, item.level == NotificationLevel::Info ? 2 : 3, 0);
    lv_obj_set_style_radius(g_toast, 0, 0);
    lv_obj_set_style_pad_all(g_toast, 5, 0);
    lv_obj_clear_flag(g_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_toast, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* label = lv_label_create(g_toast);
    lv_label_set_text(label, item.text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, responsive::DISPLAY_W - 32);
    lv_obj_set_style_text_color(label, lv_color_hex(theme::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(label, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(label);

    lv_obj_add_event_cb(g_toast, [](lv_event_t* event) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(event);
        g_queue.dismiss(millis());
        if (target && lv_obj_is_valid(target)) lv_obj_del_async(target);
        g_toast = nullptr;
        g_toast_screen = nullptr;
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_move_to_index(g_toast, -1);
}

void observe_system_state(uint32_t now)
{
    const bool connected = sigurdos::mesh::companionBleConnected();
    if (!g_companion_seeded) {
        g_companion_connected = connected;
        g_companion_seeded = true;
        if (connected) {
            notifications_post(NotificationEvent::CompanionConnected,
                               "Companion connected");
        }
    } else if (connected != g_companion_connected) {
        g_companion_connected = connected;
        notifications_post(connected ? NotificationEvent::CompanionConnected
                                     : NotificationEvent::CompanionDisconnected,
                           connected ? "Companion connected" : "Companion disconnected");
    }
    update_companion_status();

    if (static_cast<uint32_t>(now - g_last_observe_ms) < 5000) return;
    g_last_observe_ms = now;

    const int battery = sigurdos_battery_pct();
    if (notification_low_battery(battery)) {
        if (!g_low_battery_latched) {
            char text[48];
            std::snprintf(text, sizeof(text), "Low battery: %d%%", battery);
            notifications_post(NotificationEvent::LowBattery, text);
            g_low_battery_latched = true;
        }
    } else if (battery > 20) {
        g_low_battery_latched = false;
    }

    if (sigurdos_sdcard_mounted()) {
        const uint64_t total = sigurdos_sdcard_capacity_bytes();
        const uint64_t free = sigurdos_sdcard_free_bytes();
        if (notification_storage_low(free, total)) {
            if (!g_storage_latched) {
                notifications_post(NotificationEvent::StorageFull,
                                   "SD card storage is nearly full");
                g_storage_latched = true;
            }
        } else {
            g_storage_latched = false;
        }
    } else {
        g_storage_latched = false;
    }

    const auto& ota = sigurdos::github_ota::getStatus();
    if (ota.state == sigurdos::github_ota::GitHubOTAState::Failed) {
        if (!g_ota_failure_latched) {
            char text[96];
            std::snprintf(text, sizeof(text), "OTA failed: %.80s",
                          ota.error_msg[0] ? ota.error_msg : "unknown error");
            notifications_post(NotificationEvent::OtaFailure, text);
            g_ota_failure_latched = true;
        }
    } else {
        g_ota_failure_latched = false;
    }
}

} // namespace

void notifications_init()
{
    g_queue.reset();
    g_toast = nullptr;
    g_toast_screen = nullptr;
    g_unread_mention = false;
    g_companion_seeded = false;
    g_companion_connected = false;
    g_low_battery_latched = false;
    g_storage_latched = false;
    g_ota_failure_latched = false;
    g_last_observe_ms = 0;
}

void notifications_post(NotificationEvent event, const char* text)
{
    char previous[96] = {};
    if (g_queue.has_current()) {
        std::strncpy(previous, g_queue.current().text, sizeof(previous) - 1);
    }
    g_queue.post(event, text, millis());
    if (!g_toast || std::strcmp(previous, g_queue.current().text) != 0) {
        destroy_toast();
    }
}

void notifications_message(const char* channel, const char* sender,
                           const char* text, bool is_self)
{
    if (is_self) return;
    const bool is_dm = !channel || channel[0] == '\0';
    const bool mention = !is_dm && notification_contains_mention(
        text, sigurdos::mesh::getOwnName());
    char message[96];
    if (is_dm) {
        std::snprintf(message, sizeof(message), "DM from %s", sender ? sender : "Unknown");
        notifications_post(NotificationEvent::DirectMessage, message);
    } else if (mention) {
        notification_mention_text(message, sizeof(message), sender, channel);
        notifications_post(NotificationEvent::Mention, message);
        g_unread_mention = true;
    }

    if (!sigurdos::prefs_get().buzzer_quiet) {
        if (is_dm) sigurdos::hal::buzzer_beep_short();
        else sigurdos::hal::buzzer_beep_double();
    }
}

void notifications_login_result(const char* contact_name, bool success)
{
    char text[96];
    std::snprintf(text, sizeof(text), "%s: login %s",
                  contact_name ? contact_name : "Server",
                  success ? "successful" : "failed");
    notifications_post(success ? NotificationEvent::LoginSuccess
                               : NotificationEvent::LoginFailure, text);
}

void notifications_login_failure(const char* contact_name,
                                 LoginFailureReason reason)
{
    char text[96];
    notification_login_failure_text(text, sizeof(text), contact_name, reason);
    notifications_post(NotificationEvent::LoginFailure, text);
}

bool notifications_has_unread_mention() { return g_unread_mention; }
void notifications_clear_unread_mentions() { g_unread_mention = false; }

void notifications_loop()
{
    const uint32_t now = millis();
    observe_system_state(now);
    if (g_queue.tick(now)) destroy_toast();
    render_toast();
}

} // namespace sigurdos::ui
