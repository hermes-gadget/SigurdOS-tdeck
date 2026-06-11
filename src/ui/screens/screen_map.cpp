// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.

#include "../screens.h"
#include "../screens_common.h"
#include "../theme.h"
#include "../responsive.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../app/map_renderer.h"
#include <Arduino.h>
#include <lvgl.h>
#include <new>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ── Forward declarations for trackball handler ──
static void render_map_with_contacts();

// ════════════════════════════════════════════════════════
// Map — trackball pan navigation
// ════════════════════════════════════════════════════════

bool map_screen_handle_trackball(SigurdOSTrackballEvent event) {
    const int PAN_PX = 12;  // pixels per trackball tick
    switch (event) {
        case SigurdOSTrackballEvent::Up:
            sigurdos_map_pan(0, -PAN_PX);
            render_map_with_contacts();
            return true;
        case SigurdOSTrackballEvent::Down:
            sigurdos_map_pan(0, PAN_PX);
            render_map_with_contacts();
            return true;
        case SigurdOSTrackballEvent::Left:
            sigurdos_map_pan(-PAN_PX, 0);
            render_map_with_contacts();
            return true;
        case SigurdOSTrackballEvent::Right:
            sigurdos_map_pan(PAN_PX, 0);
            render_map_with_contacts();
            return true;
        case SigurdOSTrackballEvent::Click:
            sigurdos_map_zoom_in();
            render_map_with_contacts();
            return true;
        default:
            return false;
    }
}

// ════════════════════════════════════════════════════════
// Map — offline tile maps
// ════════════════════════════════════════════════════════

// Helper: render map tiles then overlay contact markers
static void render_map_with_contacts() {
    sigurdos_map_render();
    sigurdos::mesh::ContactInfo* contacts =
        new(std::nothrow) sigurdos::mesh::ContactInfo[MAX_CONTACTS];
    if (!contacts) {
        sigurdos_map_contact_render(nullptr, 0);
        return;
    }
    int n = sigurdos::mesh::exportContactsFull(contacts, MAX_CONTACTS);
    if (n < 0) n = 0;
    if (n > MAX_CONTACTS) n = MAX_CONTACTS;
    sigurdos_map_contact_render(contacts, n);
    delete[] contacts;
}

void map_screen_show()
{
    lv_obj_t* scr = make_screen_full("Map");

    // Create the map overlay container before initializing contacts
    lv_obj_t* map = lv_obj_create(scr);
    lv_obj_set_size(map, DISPLAY_W, CONTENT_H);
    lv_obj_align(map, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(map, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(map, 0, 0);
    lv_obj_add_flag(map, LV_OBJ_FLAG_CLICKABLE);

    sigurdos_map_init();
    sigurdos_map_reparent(scr);

    // Discover tiles on first map visit (deferred from boot to avoid blocking)
    sigurdos_map_discover_tiles();

    // Pre-allocate contact marker dots on top of map BEFORE rendering
    sigurdos_map_contact_init(map);
    sigurdos_map_contact_set_tap_cb(contact_detail_screen_show);

    render_map_with_contacts();

    static int drag_start_x = 0, drag_start_y = 0;
    static uint32_t map_last_render_ms = 0;

    // Reset drag state on every entry so stale values from a previous visit
    // don't cause a map jump or skip the first re-render.
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        drag_start_x = 0;
        drag_start_y = 0;
        map_last_render_ms = 0;
    }, LV_EVENT_SCREEN_LOADED, nullptr);

    lv_obj_add_event_cb(map, [](lv_event_t* e) {
        int code = lv_event_get_code(e);
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
        lv_indev_t* indev = lv_indev_get_act();
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        if (code == LV_EVENT_PRESSED) {
            drag_start_x = pt.x; drag_start_y = pt.y;
        } else if (code == LV_EVENT_PRESSING) {
            int dx = drag_start_x - pt.x;
            int dy = drag_start_y - pt.y;
            drag_start_x = pt.x; drag_start_y = pt.y;
            if (dx != 0 || dy != 0) sigurdos_map_pan(dx, dy);
            uint32_t now = millis();
            if (now - map_last_render_ms >= 200) {
                render_map_with_contacts();
                map_last_render_ms = now;
            }
        } else if (code == LV_EVENT_RELEASED) {
            render_map_with_contacts();
            map_last_render_ms = millis();
        }
    }, LV_EVENT_ALL, nullptr);

    // Zoom buttons (above bottom bar)
    int zoom_y_base = DISPLAY_H - BOT_BAR_H - DIVIDER_H - 8;

    lv_obj_t* zoom_in = lv_btn_create(scr);
    lv_obj_set_size(zoom_in, 32, 32);
    lv_obj_align(zoom_in, LV_ALIGN_BOTTOM_RIGHT, -8, -(BOT_BAR_H + DIVIDER_H + 8));
    lv_obj_set_style_bg_color(zoom_in, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(zoom_in, 0, 0);
    lv_obj_t* zi = lv_label_create(zoom_in);
    lv_label_set_text(zi, "+"); lv_obj_center(zi);
    lv_obj_add_event_cb(zoom_in, [](lv_event_t*) { sigurdos_map_zoom_in(); render_map_with_contacts(); },
                        LV_EVENT_CLICKED, nullptr);

    lv_obj_t* zoom_out = lv_btn_create(scr);
    lv_obj_set_size(zoom_out, 32, 32);
    lv_obj_align(zoom_out, LV_ALIGN_BOTTOM_RIGHT, -8, -(BOT_BAR_H + DIVIDER_H + 48));
    lv_obj_set_style_bg_color(zoom_out, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(zoom_out, 0, 0);
    lv_obj_t* zo = lv_label_create(zoom_out);
    lv_label_set_text(zo, "-"); lv_obj_center(zo);
    lv_obj_add_event_cb(zoom_out, [](lv_event_t*) { sigurdos_map_zoom_out(); render_map_with_contacts(); },
                        LV_EVENT_CLICKED, nullptr);

    (void)zoom_y_base;
    show_screen(scr);
    lv_timer_create([](lv_timer_t* t) {
        sigurdos_map_render();
        lv_timer_del(t);
    }, 250, nullptr);
}

} // namespace sigurdos::ui
