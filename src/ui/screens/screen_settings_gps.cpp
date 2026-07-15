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
#include "../../hal/prefs.h"
#include "../../hal/gps.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

static constexpr uint16_t GPS_INT_VALUES[] = {5, 10, 30, 60};
static constexpr const char* GPS_INT_LABELS[] = {"5s", "10s", "30s", "60s"};
static lv_timer_t* g_gps_sync_timer = nullptr;
static lv_obj_t* g_gps_sync_row = nullptr;

static void gps_sync_timer_cb(lv_timer_t* timer)
{
    if (!g_gps_sync_row || !lv_obj_is_valid(g_gps_sync_row)) {
        sigurdos_gps_cancel_time_sync();
        lv_timer_del(timer);
        g_gps_sync_timer = nullptr;
        return;
    }
    char text[64];
    const SigurdOSGpsSyncStatus status = sigurdos_gps_time_sync_status();
    if (status == SigurdOSGpsSyncStatus::Waiting) {
        const unsigned seconds =
            (unsigned)((sigurdos_gps_time_sync_remaining_ms() + 999) / 1000);
        snprintf(text, sizeof(text), "  Sync time: waiting for fix (%us)", seconds);
        update_row_label(g_gps_sync_row, text);
        return;
    }
    if (status == SigurdOSGpsSyncStatus::Success && sigurdos_gps_epoch() != 0 &&
        sigurdos::mesh::setSystemTime(sigurdos_gps_epoch())) {
        update_row_label(g_gps_sync_row, "  Sync time: complete");
    } else {
        update_row_label(g_gps_sync_row, "  Sync time: timed out");
    }
    lv_timer_del(timer);
    g_gps_sync_timer = nullptr;
}

struct GpsDiagDialogCtx {
    lv_obj_t* label;
    lv_obj_t* row;
};

static const char* gps_diag_assessment()
{
    if (sigurdos_gps_active_baud() == 0 && sigurdos_gps_chars_processed() == 0) {
        return "not_initialized_or_no_uart";
    }
    if (sigurdos_gps_chars_processed() == 0) return "no_uart_chars";
    if (sigurdos_gps_sentences_received() == 0) return "partial_uart_no_lines";
    if (sigurdos_gps_valid_sentences() == 0) {
        return sigurdos_gps_checksum_failures() > 0
            ? "checksum_failures"
            : "no_valid_nmea";
    }
    if (sigurdos_gps_has_fix()) return "fix";
    if (sigurdos_gps_gsv_sentences() == 0) return "valid_nmea_no_gsv";
    if (sigurdos_gps_satellites_in_view() == 0) return "no_satellites_visible";
    if (sigurdos_gps_gsv_snr_count() == 0) return "satellites_no_snr";
    return "satellites_seen_waiting_fix";
}

static void update_gps_status_row(lv_obj_t* row)
{
    if (!row) return;
    char row_buf[48];
    snprintf(row_buf, sizeof(row_buf), "  GPS: %s", sigurdos_gps_has_fix() ? "Fix acquired" : "No fix");
    update_row_label(row, row_buf);
}

static void gps_diag_update(GpsDiagDialogCtx* ctx)
{
    if (!ctx || !ctx->label) return;

    const char rmc = sigurdos_gps_rmc_status() ? sigurdos_gps_rmc_status() : '-';
    char body[448];
    snprintf(body, sizeof(body),
             "State: %s\n"
             "Fix: %d qual=%u type=%u rmc=%c\n"
             "Sky: sat=%u view=%u snr=%u/%u\n"
             "UART: %lu baud chars=%lu lines=%lu\n"
             "NMEA: valid=%lu gga=%lu rmc=%lu\n"
             "More: gsv=%lu gsa=%lu cs=%lu sw=%lu\n"
             "Pos: %.5f %.5f alt=%.1f\n"
             "UTC: %02u:%02u:%02u sync=%d",
             gps_diag_assessment(),
             sigurdos_gps_has_fix() ? 1 : 0,
             (unsigned)sigurdos_gps_fix_quality(),
             (unsigned)sigurdos_gps_fix_type(),
             rmc,
             (unsigned)sigurdos_gps_satellites(),
             (unsigned)sigurdos_gps_satellites_in_view(),
             (unsigned)sigurdos_gps_gsv_snr_max(),
             (unsigned)sigurdos_gps_gsv_snr_count(),
             (unsigned long)sigurdos_gps_active_baud(),
             (unsigned long)sigurdos_gps_chars_processed(),
             (unsigned long)sigurdos_gps_sentences_received(),
             (unsigned long)sigurdos_gps_valid_sentences(),
             (unsigned long)sigurdos_gps_gga_sentences(),
             (unsigned long)sigurdos_gps_rmc_sentences(),
             (unsigned long)sigurdos_gps_gsv_sentences(),
             (unsigned long)sigurdos_gps_gsa_sentences(),
             (unsigned long)sigurdos_gps_checksum_failures(),
             (unsigned long)sigurdos_gps_baud_switches(),
             (double)sigurdos_gps_latitude(),
             (double)sigurdos_gps_longitude(),
             (double)sigurdos_gps_altitude_m(),
             (unsigned)sigurdos_gps_hour(),
             (unsigned)sigurdos_gps_minute(),
             (unsigned)sigurdos_gps_second(),
             sigurdos_gps_time_synced() ? 1 : 0);
    lv_label_set_text(ctx->label, body);
}

static void show_gps_diag_dialog(lv_obj_t* parent, lv_obj_t* row)
{
    auto dlg_sz = dialog_size(302, 214);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(dlg, PIXEL_BORDER, 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "GPS Diagnostics");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* text = lv_label_create(dlg);
    lv_obj_set_width(text, dlg_sz.w - 16);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(text, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(text, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(text, LV_ALIGN_TOP_LEFT, 0, 28);

    auto* ctx = new GpsDiagDialogCtx{text, row};
    gps_diag_update(ctx);

    lv_obj_t* refresh_btn = lv_btn_create(dlg);
    lv_obj_set_size(refresh_btn, 78, 24);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    apply_pixel_btn_outline(refresh_btn);
    lv_obj_t* rl = lv_label_create(refresh_btn);
    lv_label_set_text(rl, "Refresh");
    lv_obj_set_style_text_font(rl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(rl);
    lv_obj_add_event_cb(refresh_btn, [](lv_event_t* e) {
        auto* ctx = (GpsDiagDialogCtx*)lv_event_get_user_data(e);
        gps_diag_update(ctx);
        update_gps_status_row(ctx ? ctx->row : nullptr);
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_t* close_btn = lv_btn_create(dlg);
    lv_obj_set_size(close_btn, 78, 24);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    apply_pixel_btn(close_btn);
    lv_obj_t* cl = lv_label_create(close_btn);
    lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_font(cl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (GpsDiagDialogCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, (void*)ctx);
}

void settings_gps_show()
{
    lv_obj_t* scr = make_screen_full("GPS / Location");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    char buf[128];
    int row = 0;

    // GPS status
    snprintf(buf, sizeof(buf), "  GPS: %s", sigurdos_gps_has_fix() ? "Fix acquired" : "No fix");
    lv_obj_t* row0 = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
    lv_obj_set_style_bg_color(row0, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(row0, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(row0, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(row0, [](lv_event_t* e) {
        lv_obj_t* row = (lv_obj_t*)lv_event_get_target(e);
        show_gps_diag_dialog(lv_obj_get_screen(row), row);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // GPS enable toggle
    snprintf(buf, sizeof(buf), "  GPS: %s", p.gps_enabled ? "ON" : "OFF");
    lv_obj_t* btn_gps_en = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
    lv_obj_set_style_bg_color(btn_gps_en, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_gps_en, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_gps_en, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_gps_en, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.gps_enabled = !np.gps_enabled;
        sigurdos::prefs_set(np);
        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  GPS: %s", np.gps_enabled ? "ON" : "OFF");
        lv_obj_t* lbl = lv_obj_get_child(target, 1);
        if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
            lv_label_set_text(lbl, row_buf);
        }
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // GPS interval cycle
    {
        static constexpr int NUM_GPS_INT = 4;
        int cur_gps = 0;
        for (int i = 0; i < NUM_GPS_INT; i++) {
            if (p.gps_interval == GPS_INT_VALUES[i]) { cur_gps = i; break; }
        }
        snprintf(buf, sizeof(buf), "  GPS interval: %s", GPS_INT_LABELS[cur_gps]);
        lv_obj_t* btn_gps_int = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
        lv_obj_set_style_bg_color(btn_gps_int, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_bg_opa(btn_gps_int, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_gps_int, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_gps_int, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            sigurdos::NodePrefs np = sigurdos::prefs_get();
            int idx = 0;
            for (int i = 0; i < 4; i++) {
                if (np.gps_interval == GPS_INT_VALUES[i]) { idx = i; break; }
            }
            idx = (idx + 1) % 4;
            np.gps_interval = GPS_INT_VALUES[idx];
            sigurdos::prefs_set(np);
            char row_buf[64];
            snprintf(row_buf, sizeof(row_buf), "  GPS interval: %s",
                     GPS_INT_LABELS[idx]);
            lv_obj_t* lbl = lv_obj_get_child(target, 1);
            if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
                lv_label_set_text(lbl, row_buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Explicit one-shot time acquisition. This temporarily polls GPS at high
    // rate even when background GPS is off, then stops on success/timeout.
    g_gps_sync_row = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "  Sync time from GPS");
    lv_obj_set_style_bg_color(g_gps_sync_row, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(g_gps_sync_row, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(g_gps_sync_row, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(g_gps_sync_row, [](lv_event_t*) {
        if (g_gps_sync_timer) {
            lv_timer_del(g_gps_sync_timer);
            g_gps_sync_timer = nullptr;
        }
        sigurdos_gps_start_time_sync(60000);
        update_row_label(g_gps_sync_row, "  Sync time: waiting for fix (60s)");
        g_gps_sync_timer = lv_timer_create(gps_sync_timer_cb, 500, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Share location toggle
    snprintf(buf, sizeof(buf), "  Share location: %s", p.share_location ? "ON" : "OFF");
    lv_obj_t* btn_share = lv_list_add_btn(list, LV_SYMBOL_GPS, buf);
    lv_obj_set_style_bg_color(btn_share, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_share, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_share, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_share, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        np.share_location = !np.share_location;
        sigurdos::prefs_set(np);
        char row_buf[64];
        snprintf(row_buf, sizeof(row_buf), "  Share location: %s", np.share_location ? "ON" : "OFF");
        lv_obj_t* lbl = lv_obj_get_child(target, 1);
        if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
            lv_label_set_text(lbl, row_buf);
        }
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        if (g_gps_sync_timer) {
            lv_timer_del(g_gps_sync_timer);
            g_gps_sync_timer = nullptr;
        }
        sigurdos_gps_cancel_time_sync();
        g_gps_sync_row = nullptr;
    }, LV_EVENT_DELETE, nullptr);
    show_screen(scr);
}

} // namespace sigurdos::ui
