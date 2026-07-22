// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_status_icon.h"

#include "theme.h"
#include "../hal/wifi_ota.h"

#include <cstdint>
#include <cstdio>

namespace sigurdos::ui {

static lv_obj_t* g_wifi_icon = nullptr;
static uint32_t g_wifi_icon_generation = 0;

void wifi_status_icon_attach(lv_obj_t* icon) {
    g_wifi_icon = icon;
    const uint32_t generation = ++g_wifi_icon_generation;
    if (!icon) return;
    lv_obj_add_event_cb(icon, [](lv_event_t* event) {
        const auto callback_generation = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
        auto* deleted = static_cast<lv_obj_t*>(lv_event_get_target(event));
        if (g_wifi_icon == deleted &&
            g_wifi_icon_generation == callback_generation) {
            g_wifi_icon = nullptr;
        }
    }, LV_EVENT_DELETE,
    reinterpret_cast<void*>(static_cast<uintptr_t>(generation)));
    update_wifi_status();
}

void update_wifi_status() {
    if (!g_wifi_icon) return;
    if (!lv_obj_is_valid(g_wifi_icon)) {
        g_wifi_icon = nullptr;
        return;
    }
    const bool connected = sigurdos::wifi_sta::isConnected();
    if (connected) {
        const int rssi = sigurdos::wifi_sta::getRSSI();
        char text[16];
        snprintf(text, sizeof(text), "%s %d", LV_SYMBOL_WIFI, rssi);
        lv_label_set_text(g_wifi_icon, text);
        lv_obj_set_style_text_color(g_wifi_icon,
            lv_color_hex(rssi > -60 ? theme::ACCENT_GREEN : theme::ACCENT), 0);
        lv_obj_clear_flag(g_wifi_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_wifi_icon, LV_OBJ_FLAG_HIDDEN);
    }
}

void screens_clear_wifi_icon() {
    g_wifi_icon = nullptr;
    ++g_wifi_icon_generation;
}

}  // namespace sigurdos::ui
