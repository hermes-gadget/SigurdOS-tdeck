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
#include "../navigation.h"
#include "../theme.h"
#include "../pin_gate_policy.h"
#include "../responsive.h"
#include "../lv_timer_owner.h"
#include "../system_action_policy.h"
#include "../home_screen.h"
#include "../../hal/keyboard.h"
#include "../../hal/prefs.h"
#include "../../hal/sdcard.h"
#include "../../hal/tdeck_pins.h"
#include "../../hal/touch.h"
#include "../../hal/trackball.h"
#include "../../hal/launcher_env.h"
#include "../../hal/wifi_ota.h"
#include "../../hal/github_ota.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../diagnostics/build_info.h"
#include "../../fonts/emoji_font.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <new>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

static lv_obj_t* g_date_row = nullptr;   // for live update after setting time
static lv_obj_t* g_time_row = nullptr;
static lv_obj_t* g_time_source_row = nullptr;

static void update_time_source_row()
{
    if (!g_time_source_row || !lv_obj_is_valid(g_time_source_row)) return;
    char status[48];
    sigurdos::mesh::formatTimeSyncStatus(
        sigurdos::mesh::getTimeSyncStatus(), status, sizeof(status));
    char row[72];
    snprintf(row, sizeof(row), "  Time source: %s", status);
    update_row_label(g_time_source_row, row);
}

struct GitHubOtaDialogCtx {
    lv_obj_t* dialog;
    LvTimerOwner poll_timer;
};

static void show_build_info_dialog(lv_obj_t* parent)
{
    const auto& info = sigurdos::build::info();
    auto dlg_sz = dialog_size(292, 214);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(dlg, PIXEL_BORDER, 0);
    lv_obj_set_style_radius(dlg, 0, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Build Info");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    char run_line[64];
    const bool has_attempt = info.actions_run_attempt && info.actions_run_attempt[0] != '\0';
    snprintf(run_line, sizeof(run_line), "%s%s%s",
             info.actions_run_id ? info.actions_run_id : "unknown",
             has_attempt ? "." : "",
             has_attempt ? info.actions_run_attempt : "");

    char body[512];
    snprintf(body, sizeof(body),
             "Version: %s\nGit: %s%s\nMeshCore: %s\nEnv: %s\nPartitions: %s\nBoard: %s\nMCU: %s\nSource: %s\nRun: %s\nRef: %s",
             info.firmware_version, info.git_sha, info.git_dirty ? " dirty" : "",
             info.meshcore_sha, info.build_env, info.partitions, info.board, info.mcu,
             info.build_source, run_line, info.actions_ref);
    lv_obj_t* text = lv_label_create(dlg);
    lv_label_set_text(text, body);
    lv_obj_set_width(text, dlg_sz.w - 18);
    lv_obj_set_style_text_color(text, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(text, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(text, LV_ALIGN_TOP_LEFT, 9, 32);

    lv_obj_t* close_btn = lv_btn_create(dlg);
    lv_obj_set_size(close_btn, 88, 26);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(close_btn, 0, 0);
    lv_obj_t* cl = lv_label_create(close_btn);
    lv_label_set_text(cl, "Close");
    lv_obj_center(cl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
    }, LV_EVENT_CLICKED, nullptr);
}

struct SdDiagDialogCtx {
    lv_obj_t* label;
    lv_obj_t* row;
    lv_obj_t* retry_btn;
    std::atomic<int> refs{1};
    std::atomic<bool> active{true};
    bool retry_running = false;
    bool retry_result = false;
};

static void update_sd_row_label(lv_obj_t* row);
static void sd_diag_update(SdDiagDialogCtx* ctx);

static void sd_diag_release(SdDiagDialogCtx* ctx)
{
    if (ctx && ctx->refs.fetch_sub(1) == 1) delete ctx;
}

static void sd_diag_retry_complete(void* data)
{
    auto* ctx = static_cast<SdDiagDialogCtx*>(data);
    ctx->retry_running = false;
    if (ctx->active.load()) {
        sd_diag_update(ctx);
        update_sd_row_label(ctx->row);
        lv_obj_clear_state(ctx->retry_btn, LV_STATE_DISABLED);
    }
    sd_diag_release(ctx);
}

static void sd_diag_retry_worker(void* data)
{
    auto* ctx = static_cast<SdDiagDialogCtx*>(data);
    ctx->retry_result = sigurdos_sdcard_retry();
    if (lv_async_call(sd_diag_retry_complete, ctx) != LV_RESULT_OK) {
        ctx->retry_running = false;
        sd_diag_release(ctx);
    }
    vTaskDelete(nullptr);
}

static void update_sd_row_label(lv_obj_t* row)
{
    if (!row) return;
    char buf[40];
    snprintf(buf, sizeof(buf), "  SD Card: %s", sigurdos_sdcard_mounted() ? "Mounted" : "Not mounted");
    update_row_label(row, buf);
}

static void sd_diag_update(SdDiagDialogCtx* ctx)
{
    if (!ctx || !ctx->label) return;

    SigurdosSdMountDiagnostic diag = sigurdos_sdcard_diagnostics();
    char total_buf[24] = "n/a";
    char free_buf[24] = "n/a";
    if (diag.mounted) {
        sigurdos_sdcard_format_size(sigurdos_sdcard_capacity_bytes(), total_buf, sizeof(total_buf));
        sigurdos_sdcard_format_size(sigurdos_sdcard_free_bytes(), free_buf, sizeof(free_buf));
    }

    char body[256];
    snprintf(body, sizeof(body),
             "Mounted: %s\n"
             "Attempts: %u\n"
             "Last: %s / %s\n"
             "Backoff: %lu ms\n"
             "Free: %s / %s",
             diag.mounted ? "yes" : "no",
             diag.attempt_count,
             sigurdos_sdcard_mount_source_name(diag.last_source),
             sigurdos_sdcard_mount_error_name(diag.last_error),
             (unsigned long)diag.last_backoff_ms,
             free_buf,
             total_buf);
    lv_label_set_text(ctx->label, body);
}

static void show_sd_diag_dialog(lv_obj_t* parent, lv_obj_t* row)
{
    auto dlg_sz = dialog_size(270, 146);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(dlg, PIXEL_BORDER, 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "SD Card");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* text = lv_label_create(dlg);
    lv_obj_set_width(text, dlg_sz.w - 16);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(text, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(text, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(text, LV_ALIGN_TOP_LEFT, 0, 28);

    auto* ctx = new(std::nothrow) SdDiagDialogCtx{};
    if (!ctx) {
        lv_obj_del_async(dlg);
        return;
    }
    ctx->label = text;
    ctx->row = row;
    sd_diag_update(ctx);

    lv_obj_t* retry_btn = lv_btn_create(dlg);
    ctx->retry_btn = retry_btn;
    lv_obj_set_size(retry_btn, 72, 24);
    lv_obj_align(retry_btn, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    apply_pixel_btn_outline(retry_btn);
    lv_obj_t* rl = lv_label_create(retry_btn);
    lv_label_set_text(rl, "Retry");
    lv_obj_set_style_text_font(rl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(rl);
    lv_obj_add_event_cb(retry_btn, [](lv_event_t* e) {
        auto* ctx = (SdDiagDialogCtx*)lv_event_get_user_data(e);
        if (!ctx || ctx->retry_running || !ctx->active.load()) return;
        ctx->retry_running = true;
        ctx->refs.fetch_add(1);
        lv_obj_add_state(ctx->retry_btn, LV_STATE_DISABLED);
        lv_label_set_text(ctx->label, "Retrying SD mount...");
        if (xTaskCreate(sd_diag_retry_worker, "sd-retry", 4096, ctx, 1, nullptr) != pdPASS) {
            ctx->retry_running = false;
            lv_obj_clear_state(ctx->retry_btn, LV_STATE_DISABLED);
            lv_label_set_text(ctx->label, "Unable to start SD retry");
            sd_diag_release(ctx);
        }
    }, LV_EVENT_CLICKED, (void*)ctx);

    lv_obj_t* close_btn = lv_btn_create(dlg);
    lv_obj_set_size(close_btn, 72, 24);
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
        auto* ctx = (SdDiagDialogCtx*)lv_event_get_user_data(e);
        if (!ctx) return;
        ctx->active.store(false);
        ctx->label = nullptr;
        ctx->row = nullptr;
        ctx->retry_btn = nullptr;
        sd_diag_release(ctx);
    }, LV_EVENT_DELETE, (void*)ctx);
}

struct InputDiagDialogCtx {
    lv_obj_t* dialog;
    lv_obj_t* touch_label;
    lv_obj_t* touch_marker;
    lv_obj_t* touch_pad;
    lv_obj_t* trackball_label;
    lv_obj_t* keyboard_label;
    lv_timer_t* timer;
};

static const char* input_diag_trackball_event_name(SigurdOSTrackballEvent ev)
{
    switch (ev) {
    case SigurdOSTrackballEvent::Up: return "up";
    case SigurdOSTrackballEvent::Down: return "down";
    case SigurdOSTrackballEvent::Left: return "left";
    case SigurdOSTrackballEvent::Right: return "right";
    case SigurdOSTrackballEvent::Click: return "click";
    case SigurdOSTrackballEvent::None:
    default:
        return "none";
    }
}

static int input_diag_clamp(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static void input_diag_update(InputDiagDialogCtx* ctx)
{
    if (!ctx) return;

    SigurdOSTouchDiag td{};
    bool touch_ok = sigurdos_touch_get_diag(&td);
    char text[256];
    snprintf(text, sizeof(text),
             "Touch: %s %s x=%d y=%d\npress=%lu drag=%lu rel=%lu err=%d",
             touch_ok && td.initialized ? "ready" : "offline",
             td.pressed ? "down" : "up",
             td.x, td.y,
             (unsigned long)td.press_count,
             (unsigned long)td.move_count,
             (unsigned long)td.release_count,
             td.consecutive_i2c_errors);
    lv_label_set_text(ctx->touch_label, text);

    if (touch_ok && td.initialized && td.pressed) {
        const int pad_w = (int)lv_obj_get_width(ctx->touch_pad);
        const int pad_h = (int)lv_obj_get_height(ctx->touch_pad);
        const int marker_w = (int)lv_obj_get_width(ctx->touch_marker);
        const int marker_h = (int)lv_obj_get_height(ctx->touch_marker);
        int mx = (td.x * pad_w) / TFT_WIDTH - marker_w / 2;
        int my = (td.y * pad_h) / TFT_HEIGHT - marker_h / 2;
        mx = input_diag_clamp(mx, 0, pad_w - marker_w);
        my = input_diag_clamp(my, 0, pad_h - marker_h);
        lv_obj_set_pos(ctx->touch_marker, mx, my);
        lv_obj_clear_flag(ctx->touch_marker, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->touch_marker, LV_OBJ_FLAG_HIDDEN);
    }

    SigurdOSTrackballDiag bd{};
    bool tb_ok = sigurdos_trackball_get_diag(&bd);
    snprintf(text, sizeof(text),
             "Trackball: %s last=%s q=%u ev=%lu ov=%lu\n"
             "active UDLRC=%d%d%d%d%d raw=%u,%u,%u,%u,%u",
             tb_ok && bd.initialized ? "ready" : "offline",
             input_diag_trackball_event_name(bd.last_event),
             bd.queue_count,
             (unsigned long)bd.event_count,
             (unsigned long)bd.overflow_count,
             bd.active[0] ? 1 : 0,
             bd.active[1] ? 1 : 0,
             bd.active[2] ? 1 : 0,
             bd.active[3] ? 1 : 0,
             bd.active[4] ? 1 : 0,
             bd.raw_levels[0], bd.raw_levels[1], bd.raw_levels[2],
             bd.raw_levels[3], bd.raw_levels[4]);
    lv_label_set_text(ctx->trackball_label, text);

    SigurdOSKeyboardDiag kd{};
    bool key_ok = sigurdos_keyboard_get_diag(&kd);
    char keymode_printable = (kd.last_key_mode_byte >= 32 && kd.last_key_mode_byte <= 126)
        ? (char)kd.last_key_mode_byte : '.';
    char output_printable = (kd.last_output_codepoint >= 32 && kd.last_output_codepoint <= 126)
        ? (char)kd.last_output_codepoint : '.';
    snprintf(text, sizeof(text),
             "Kbd: %s lay=%u km=%02X '%c' out=%04lX '%c'\n"
             "ev=%lu drop=%lu ms=%lu raw=%s\n"
             "mat=%02X %02X %02X %02X %02X mod=S%d C%d A%d Y%d M%d",
             key_ok && kd.initialized ? "ready" : "offline",
             kd.layout,
             kd.last_key_mode_byte,
             keymode_printable,
             (unsigned long)kd.last_output_codepoint,
             output_printable,
             (unsigned long)kd.event_count,
             (unsigned long)kd.overwrite_count,
             (unsigned long)kd.last_event_ms,
             kd.raw_supported ? (kd.raw_valid ? "ok" : "bad") : "n/a",
             kd.raw_matrix[0],
             kd.raw_matrix[1],
             kd.raw_matrix[2],
             kd.raw_matrix[3],
             kd.raw_matrix[4],
             kd.shift ? 1 : 0,
             kd.ctrl ? 1 : 0,
             kd.alt ? 1 : 0,
             kd.sym_down ? 1 : 0,
             kd.mic_down ? 1 : 0);
    lv_label_set_text(ctx->keyboard_label, text);
}

static void input_diag_timer_cb(lv_timer_t* timer)
{
    auto* ctx = (InputDiagDialogCtx*)lv_timer_get_user_data(timer);
    if (!ctx || !ctx->dialog) {
        lv_timer_del(timer);
        return;
    }
    input_diag_update(ctx);
}

static void show_input_diag_dialog(lv_obj_t* parent)
{
    auto dlg_sz = dialog_size(300, 214);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(dlg, PIXEL_BORDER, 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Input Self-Test");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 3);

    lv_obj_t* pad = lv_obj_create(dlg);
    lv_obj_set_size(pad, 112, 76);
    lv_obj_align(pad, LV_ALIGN_TOP_LEFT, 0, 25);
    lv_obj_set_style_bg_color(pad, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_border_color(pad, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_border_width(pad, PIXEL_BORDER, 0);
    lv_obj_set_style_radius(pad, 0, 0);
    lv_obj_set_style_pad_all(pad, 0, 0);

    lv_obj_t* marker = lv_obj_create(pad);
    lv_obj_set_size(marker, 8, 8);
    lv_obj_set_style_bg_color(marker, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_border_width(marker, 0, 0);
    lv_obj_set_style_radius(marker, 0, 0);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* touch_label = lv_label_create(dlg);
    lv_obj_set_width(touch_label, dlg_sz.w - 134);
    lv_label_set_long_mode(touch_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(touch_label, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(touch_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(touch_label, LV_ALIGN_TOP_LEFT, 122, 27);

    lv_obj_t* trackball_label = lv_label_create(dlg);
    lv_obj_set_width(trackball_label, dlg_sz.w - 16);
    lv_label_set_long_mode(trackball_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(trackball_label, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(trackball_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(trackball_label, LV_ALIGN_TOP_LEFT, 0, 106);

    lv_obj_t* keyboard_label = lv_label_create(dlg);
    lv_obj_set_width(keyboard_label, dlg_sz.w - 16);
    lv_label_set_long_mode(keyboard_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(keyboard_label, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(keyboard_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(keyboard_label, LV_ALIGN_TOP_LEFT, 0, 138);

    lv_obj_t* close_btn = lv_btn_create(dlg);
    lv_obj_set_size(close_btn, 70, 22);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -2);
    apply_pixel_btn(close_btn);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_set_style_text_font(close_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, nullptr);

    auto* ctx = new InputDiagDialogCtx{
        dlg, touch_label, marker, pad, trackball_label, keyboard_label, nullptr
    };
    ctx->timer = lv_timer_create(input_diag_timer_cb, 100, ctx);
    input_diag_update(ctx);

    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        auto* ctx = (InputDiagDialogCtx*)lv_event_get_user_data(e);
        if (ctx) {
            if (ctx->timer) {
                lv_timer_del(ctx->timer);
                ctx->timer = nullptr;
            }
            ctx->dialog = nullptr;
            delete ctx;
        }
    }, LV_EVENT_DELETE, ctx);
}

struct DateTimeDialogCtx {
    lv_obj_t* input;
    lv_obj_t* feedback;
    bool       is_date;
};

static void datetime_set_dialog(lv_obj_t* parent, bool is_date)
{
    int y, mo, d, h, mi;
    sigurdos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);

    char cur[16];
    if (is_date) snprintf(cur, sizeof(cur), "%04d-%02d-%02d", y, mo, d);
    else         snprintf(cur, sizeof(cur), "%02d:%02d", h, mi);

    auto dlg_sz = dialog_size(260, 120);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, is_date ? "Set Date (YYYY-MM-DD)" : "Set Time (HH:MM 24h)");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* input = lv_textarea_create(dlg);
    lv_obj_set_size(input, dlg_sz.w - 16, 28);
    lv_obj_align(input, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(input, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(input, 0, 0);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, is_date ? 10 : 5);
    lv_textarea_set_accepted_chars(input,
        is_date ? "0123456789-" : "0123456789:");
    lv_textarea_set_text(input, cur);
    apply_focus_style(input);

    // Focus immediately so the physical keyboard works without tapping the field
    lv_group_t* grp = lv_group_get_default();
    if (grp) {
        lv_group_add_obj(grp, input);
        lv_group_focus_obj(input);
    }

    lv_obj_t* fb = lv_label_create(dlg);
    lv_obj_set_style_text_color(fb, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(fb, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(fb, LV_ALIGN_BOTTOM_MID, 0, -28);

    lv_obj_t* cancel_btn = lv_btn_create(dlg);
    lv_obj_set_size(cancel_btn, 72, 24);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(cancel_btn, 0, 0);
    lv_obj_t* cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_font(cl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 72, 24);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_t* sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_set_style_text_font(sl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(sl);

    // ctx lives until the dialog is deleted (see LV_EVENT_DELETE below)
    auto* ctx = new DateTimeDialogCtx{ input, fb, is_date };
    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (DateTimeDialogCtx*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, (void*)ctx);

    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        auto* ctx = (DateTimeDialogCtx*)lv_event_get_user_data(e);
        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
        const char* s = lv_textarea_get_text(ctx->input);

        bool valid = false;
        uint32_t epoch = 0;

        if (ctx->is_date) {
            int ny, nm, nd;
            // Days in month lookup: jan=31, feb=28, mar=31, ...
            static const uint8_t DAYS_IN_MONTH[] = {31,28,31,30,31,30,31,31,30,31,30,31};
            if (sscanf(s, "%d-%d-%d", &ny, &nm, &nd) == 3 &&
                ny > 2020 && nm >= 1 && nm <= 12 && nd >= 1) {
                // Check days in month (with leap year for February)
                uint8_t max_days = DAYS_IN_MONTH[nm - 1];
                if (nm == 2 && (ny % 4 == 0 && (ny % 100 != 0 || ny % 400 == 0)))
                    max_days = 29;
                if (nd <= max_days) {
                    int cy, cmo, cd, ch, cmi;
                    sigurdos::mesh::getCurrentLocalDateTime(&cy, &cmo, &cd, &ch, &cmi);
                    epoch = sigurdos::mesh::makeEpoch(ny, nm, nd, ch, cmi);
                    valid = true;
                } else {
                    lv_label_set_text(ctx->feedback, "Invalid day for month");
                }
            } else {
                lv_label_set_text(ctx->feedback, "Invalid date (YYYY-MM-DD)");
            }
        } else {
            int nh, nm_v;
            if (sscanf(s, "%d:%d", &nh, &nm_v) == 2 &&
                nh >= 0 && nh <= 23 && nm_v >= 0 && nm_v <= 59) {
                int cy, cmo, cd, ch, cmi;
                sigurdos::mesh::getCurrentLocalDateTime(&cy, &cmo, &cd, &ch, &cmi);
                epoch = sigurdos::mesh::makeEpoch(cy, cmo, cd, nh, nm_v);
                valid = true;
            } else {
                lv_label_set_text(ctx->feedback, "Invalid time (HH:MM)");
            }
        }

        if (valid && sigurdos::mesh::setSystemTime(
                epoch, sigurdos::mesh::TimeSource::Manual)) {
            int yy, mmo, dd, hh, mmi;
            sigurdos::mesh::getCurrentLocalDateTime(&yy, &mmo, &dd, &hh, &mmi);
            char dbuf[32], tbuf[16], home_time[6];
            snprintf(dbuf, sizeof(dbuf), "  Date: %04d-%02d-%02d", yy, mmo, dd);
            snprintf(tbuf, sizeof(tbuf), "  Time: %02d:%02d", hh, mmi);
            snprintf(home_time, sizeof(home_time), "%02d:%02d", hh, mmi);
            update_row_label(g_date_row, dbuf);
            update_row_label(g_time_row, tbuf);
            update_time_source_row();
            home_screen_update_time(home_time);
            lv_obj_del_async(dlg);
        }
    }, LV_EVENT_CLICKED, (void*)ctx);
}

void settings_system_show()
{
    lv_obj_t* scr = make_screen_full("System");

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    char buf[128];
    int row = 0;

    // Node name
    snprintf(buf, sizeof(buf), "  Name: %s", p.node_name);
    lv_obj_t* r0 = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
    lv_obj_set_style_bg_color(r0, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r0, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r0, lv_color_hex(TEXT_PRIMARY), 0);
    row++;

    // SD Card
    snprintf(buf, sizeof(buf), "  SD Card: %s", sigurdos_sdcard_mounted() ? "Mounted" : "Not mounted");
    lv_obj_t* r1 = lv_list_add_btn(list, LV_SYMBOL_SD_CARD, buf);
    lv_obj_set_style_bg_color(r1, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(r1, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r1, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(r1, [](lv_event_t* e) {
        lv_obj_t* row = (lv_obj_t*)lv_event_get_target(e);
        show_sd_diag_dialog(lv_obj_get_screen(row), row);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Date
    {
        int y, mo, d, h, mi;
        sigurdos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);
        snprintf(buf, sizeof(buf), "  Date: %04d-%02d-%02d", y, mo, d);
        lv_obj_t* btn_date = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_date, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_date, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_date, lv_color_hex(TEXT_PRIMARY), 0);
        g_date_row = btn_date;
        lv_obj_add_event_cb(btn_date, [](lv_event_t* e) {
            datetime_set_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)), true);
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Time
    {
        int y, mo, d, h, mi;
        sigurdos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);
        snprintf(buf, sizeof(buf), "  Time: %02d:%02d", h, mi);
        lv_obj_t* btn_time = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_time, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_time, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_time, lv_color_hex(TEXT_PRIMARY), 0);
        g_time_row = btn_time;
        lv_obj_add_event_cb(btn_time, [](lv_event_t* e) {
            datetime_set_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)), false);
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Time source and last-sync age
    {
        char status[48];
        sigurdos::mesh::formatTimeSyncStatus(
            sigurdos::mesh::getTimeSyncStatus(), status, sizeof(status));
        snprintf(buf, sizeof(buf), "  Time source: %s", status);
        lv_obj_t* source_row = lv_list_add_btn(list, LV_SYMBOL_REFRESH, buf);
        lv_obj_set_style_bg_color(
            source_row, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(source_row, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(source_row, lv_color_hex(TEXT_PRIMARY), 0);
        g_time_source_row = source_row;
        lv_obj_add_event_cb(source_row, [](lv_event_t*) {
            update_time_source_row();
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Run Setup Wizard
    lv_obj_t* btn_wizard = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "  Run Setup Wizard");
    lv_obj_set_style_bg_color(btn_wizard, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_wizard, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_wizard, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_wizard, [](lv_event_t*) {
        navigate_to(Screen::Onboarding);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Input self-test
    lv_obj_t* btn_input_diag = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "  Input Self-Test");
    lv_obj_set_style_bg_color(btn_input_diag, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_input_diag, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_input_diag, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_input_diag, [](lv_event_t* e) {
        show_input_diag_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Device PIN
    {
        bool has_pin = (p.device_pin != 0);
        snprintf(buf, sizeof(buf), "  Device PIN: %s", has_pin ? "Change" : "Set");
        lv_obj_t* btn_pin = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, buf);
        lv_obj_set_style_bg_color(btn_pin, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_pin, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_pin, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_pin, [](lv_event_t* e) {
            lv_obj_t* scr_pin = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
            auto dlg_sz = dialog_size(260, 190);
            lv_obj_t* dlg = lv_obj_create(scr_pin);
            lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
            lv_obj_center(dlg);
            lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
            lv_obj_set_style_radius(dlg, 0, 0);
            lv_obj_set_style_border_width(dlg, 2, 0);
            lv_obj_set_style_border_color(dlg, lv_color_hex(DIVIDER), 0);
            lv_obj_set_style_pad_all(dlg, 8, 0);

            lv_obj_t* title = lv_label_create(dlg);
            lv_label_set_text(title, "Set/Change PIN");
            lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
            lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

            lv_obj_t* msg = lv_label_create(dlg);
            lv_label_set_text(msg, "Enter and confirm a 4-6 digit PIN");
            lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(msg, LV_ALIGN_TOP_LEFT, 8, 24);

            lv_obj_t* pin_ta = lv_textarea_create(dlg);
            lv_obj_set_size(pin_ta, 120, 30);
            lv_obj_align(pin_ta, LV_ALIGN_TOP_MID, 0, 44);
            lv_textarea_set_password_mode(pin_ta, true);
            lv_textarea_set_one_line(pin_ta, true);
            lv_textarea_set_max_length(pin_ta, 6);
            lv_textarea_set_accepted_chars(pin_ta, "0123456789");
            lv_obj_set_style_text_align(pin_ta, LV_TEXT_ALIGN_CENTER, 0);
            apply_pixel_input(pin_ta);

            lv_obj_t* confirm_ta = lv_textarea_create(dlg);
            lv_obj_set_size(confirm_ta, 120, 30);
            lv_obj_align(confirm_ta, LV_ALIGN_TOP_MID, 0, 80);
            lv_textarea_set_password_mode(confirm_ta, true);
            lv_textarea_set_one_line(confirm_ta, true);
            lv_textarea_set_max_length(confirm_ta, 6);
            lv_textarea_set_accepted_chars(confirm_ta, "0123456789");
            lv_textarea_set_placeholder_text(confirm_ta, "Confirm");
            lv_obj_set_style_text_align(confirm_ta, LV_TEXT_ALIGN_CENTER, 0);
            apply_pixel_input(confirm_ta);
            lv_obj_set_user_data(pin_ta, confirm_ta);
            lv_obj_set_user_data(confirm_ta, msg);

            // Save button
            lv_obj_t* save_btn = lv_btn_create(dlg);
            lv_obj_set_size(save_btn, 80, 26);
            lv_obj_align(save_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
            apply_pixel_btn(save_btn);
            lv_obj_t* save_lbl = lv_label_create(save_btn);
            lv_label_set_text(save_lbl, "Save");
            lv_obj_center(save_lbl);
            lv_obj_add_event_cb(save_btn, [](lv_event_t* ev) {
                lv_obj_t* ta = (lv_obj_t*)lv_event_get_user_data(ev);
                lv_obj_t* dlg = lv_obj_get_parent(ta);
                const char* pin_str = lv_textarea_get_text(ta);
                lv_obj_t* confirm = (lv_obj_t*)lv_obj_get_user_data(ta);
                const char* confirm_str = confirm ? lv_textarea_get_text(confirm) : nullptr;
                lv_obj_t* feedback = confirm ? (lv_obj_t*)lv_obj_get_user_data(confirm) : nullptr;
                if (!pin_confirmation_valid(pin_str, confirm_str)) {
                    if (feedback) {
                        lv_label_set_text(feedback,
                            pin_str && pin_str[0] == '0'
                                ? "PIN cannot start with zero; use Clear to disable"
                                : "PINs must be 4-6 digits and match");
                        lv_obj_set_style_text_color(feedback, lv_color_hex(ACCENT_RED), 0);
                    }
                    return;
                }
                auto p = sigurdos::prefs_get();
                p.device_pin = (uint32_t)atoi(pin_str);
                if (!sigurdos::prefs_set(p)) {
                    if (feedback) lv_label_set_text(feedback, "PIN could not be saved");
                    return;
                }
                pin_clear_grace();
                lv_obj_del_async(dlg);
            }, LV_EVENT_CLICKED, pin_ta);

            // Cancel button
            lv_obj_t* cancel_btn = lv_btn_create(dlg);
            lv_obj_set_size(cancel_btn, 80, 26);
            lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
            apply_pixel_btn_outline(cancel_btn);
            lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
            lv_label_set_text(cancel_lbl, "Cancel");
            lv_obj_center(cancel_lbl);
            lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ev) {
                lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
            }, LV_EVENT_CLICKED, nullptr);

            // If PIN is already set, add a "Clear PIN" button
            if (sigurdos::prefs_get().device_pin != 0) {
                lv_obj_t* clear_btn = lv_btn_create(dlg);
                lv_obj_set_size(clear_btn, 80, 26);
                lv_obj_align(clear_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
                lv_obj_set_style_bg_color(clear_btn, lv_color_hex(ACCENT_RED), 0);
                lv_obj_set_style_radius(clear_btn, 0, 0);
                lv_obj_t* clear_lbl = lv_label_create(clear_btn);
                lv_label_set_text(clear_lbl, "Clear");
                lv_obj_center(clear_lbl);
                lv_obj_add_event_cb(clear_btn, [](lv_event_t* ev) {
                    lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev));
                    auto p = sigurdos::prefs_get();
                    p.device_pin = 0;
                    sigurdos::prefs_set(p);
                    pin_clear_grace();
                    lv_obj_del_async(dlg);
                }, LV_EVENT_CLICKED, nullptr);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // WiFi SSID / Password (for GitHub OTA)
    {
        const char* ssid_label = p.wifi_ssid[0]
            ? p.wifi_ssid : "Not set";
        snprintf(buf, sizeof(buf), "  WiFi: %s", ssid_label);
        lv_obj_t* btn_wifi = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
        lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_wifi, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_wifi, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_wifi, [](lv_event_t*) {
            navigate_to(Screen::WiFiNetworks);
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // OTA firmware update (WiFi AP + web upload)
    lv_obj_t* btn_ota = lv_list_add_btn(list, LV_SYMBOL_WIFI, "  OTA Update");
    lv_obj_set_style_bg_color(btn_ota, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_ota, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_ota, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_ota, [](lv_event_t* e) {
        lv_obj_t* scr_ota = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));

        if (sigurdos_is_under_launcher()) {
            auto dlg_sz = dialog_size(260, 80);
            lv_obj_t* dlg = lv_obj_create(scr_ota);
            lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
            lv_obj_center(dlg);
            lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
            lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_border_width(dlg, 2, 0);
            lv_obj_set_style_radius(dlg, 0, 0);
            lv_obj_set_style_pad_all(dlg, 8, 0);

            lv_obj_t* msg = lv_label_create(dlg);
            lv_label_set_text(msg, "Update SigurdOS\nthrough Launcher instead");
            lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

            lv_obj_t* close_btn = lv_btn_create(dlg);
            lv_obj_set_size(close_btn, 80, 24);
            lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_obj_set_style_bg_color(close_btn, lv_color_hex(BG_INPUT), 0);
            lv_obj_set_style_radius(close_btn, 0, 0);
            lv_obj_t* cl = lv_label_create(close_btn);
            lv_label_set_text(cl, "OK");
            lv_obj_center(cl);
            lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
                lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
            }, LV_EVENT_CLICKED, nullptr);
            return;
        }

        // Require a device PIN before exposing the OTA flash endpoint. The web
        // upload form authenticates with the device PIN; without one set, the
        // endpoint would accept unauthenticated firmware over an open AP (#687).
        if (sigurdos::prefs_get().device_pin == 0) {
            auto dlg_sz = dialog_size(260, 96);
            lv_obj_t* dlg = lv_obj_create(scr_ota);
            lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
            lv_obj_center(dlg);
            lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
            lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT_RED), 0);
            lv_obj_set_style_border_width(dlg, 2, 0);
            lv_obj_set_style_radius(dlg, 0, 0);
            lv_obj_set_style_pad_all(dlg, 8, 0);

            lv_obj_t* msg = lv_label_create(dlg);
            lv_label_set_text(msg,
                "Set a device PIN first\n(System > Device PIN).\n"
                "OTA upload requires it.");
            lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

            lv_obj_t* close_btn = lv_btn_create(dlg);
            lv_obj_set_size(close_btn, 80, 24);
            lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_obj_set_style_bg_color(close_btn, lv_color_hex(BG_INPUT), 0);
            lv_obj_set_style_radius(close_btn, 0, 0);
            lv_obj_t* cl = lv_label_create(close_btn);
            lv_label_set_text(cl, "OK");
            lv_obj_center(cl);
            lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
                lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
            }, LV_EVENT_CLICKED, nullptr);
            return;
        }

        auto dlg_sz = dialog_size(280, 150);
        lv_obj_t* dlg = lv_obj_create(scr_ota);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);

        if (!sigurdos::ota::start("SigurdOS-OTA")) {
            lv_obj_t* err = lv_label_create(dlg);
            lv_label_set_text(err, "OTA failed to start");
            lv_obj_set_style_text_color(err, lv_color_hex(ACCENT_RED), 0);
            lv_obj_set_style_text_font(err, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(err, LV_ALIGN_TOP_MID, 0, 4);

            lv_obj_t* eclose = lv_btn_create(dlg);
            lv_obj_set_size(eclose, 80, 24);
            lv_obj_align(eclose, LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_obj_set_style_bg_color(eclose, lv_color_hex(BG_INPUT), 0);
            lv_obj_set_style_radius(eclose, 0, 0);
            lv_obj_t* ecl = lv_label_create(eclose);
            lv_label_set_text(ecl, "OK");
            lv_obj_center(ecl);
            lv_obj_add_event_cb(eclose, [](lv_event_t* ev) {
                lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
            }, LV_EVENT_CLICKED, nullptr);
            return;
        }
        const char* ip = sigurdos::ota::getIP();

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, "OTA Update Active");
        lv_obj_set_style_text_color(title, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        char info[160];
        const char* ap_password = sigurdos::ota::getAPPassword();
        if (ap_password[0]) {
            snprintf(info, sizeof(info),
                "WiFi: SigurdOS-OTA\nPassword: %s\nIP: %s\n"
                "Enter device PIN + firmware.bin\nExpires in 10 minutes",
                ap_password, ip);
        } else {
            snprintf(info, sizeof(info),
                "Connected WiFi\nIP: %s\n"
                "Enter device PIN + firmware.bin\nExpires in 10 minutes", ip);
        }
        lv_obj_t* msg = lv_label_create(dlg);
        lv_label_set_text(msg, info);
        lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t* close_btn = lv_btn_create(dlg);
        lv_obj_set_size(close_btn, 80, 24);
        lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_radius(close_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(close_btn);
        lv_label_set_text(cl, "Close");
        lv_obj_center(cl);
        lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
            lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // OTA release channel — cycling through main → dev → latest
    {
        const char* branches[] = {"main", "dev", "latest"};
        int n_branches = 3;
        int current = 0;
        const char* br = p.ota_branch;
        for (int i = 0; i < n_branches; i++) {
            if (strcmp(br, branches[i]) == 0) { current = i; break; }
        }
        snprintf(buf, sizeof(buf), "  OTA Branch: %s", branches[current]);
        lv_obj_t* btn_branch = lv_list_add_btn(list, LV_SYMBOL_REFRESH, buf);
        lv_obj_set_style_bg_color(btn_branch, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_branch, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_branch, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_branch, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            NodePrefs np = prefs_get();
            const char* branches[] = {"main", "dev", "latest"};
            int n_branches = 3;
            int current = 0;
            for (int i = 0; i < n_branches; i++) {
                if (strcmp(np.ota_branch, branches[i]) == 0) { current = (i + 1) % n_branches; break; }
            }
            strncpy(np.ota_branch, branches[current], sizeof(np.ota_branch) - 1);
            np.ota_branch[sizeof(np.ota_branch) - 1] = '\0';
            prefs_set(np);
            // Update button label without rebuilding the screen
            lv_obj_t* label = lv_obj_get_child(btn, 0);
            if (label) {
                char lbl[48];
                snprintf(lbl, sizeof(lbl), "  OTA Branch: %s", branches[current]);
                lv_label_set_text(label, lbl);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // Pre-release toggle
    {
        snprintf(buf, sizeof(buf), "  Pre-releases: %s",
                 p.ota_allow_prerelease ? "ON" : "OFF");
        lv_obj_t* btn_pre = lv_list_add_btn(list, LV_SYMBOL_EDIT, buf);
        lv_obj_set_style_bg_color(btn_pre, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(btn_pre, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_pre, lv_color_hex(
            p.ota_allow_prerelease ? ACCENT : TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(btn_pre, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            NodePrefs np = prefs_get();
            np.ota_allow_prerelease = !np.ota_allow_prerelease;
            prefs_set(np);
            // Update button label without rebuilding the screen
            lv_obj_t* label = lv_obj_get_child(btn, 0);
            if (label) {
                lv_label_set_text(label, np.ota_allow_prerelease
                    ? "  Pre-releases: ON" : "  Pre-releases: OFF");
                lv_obj_set_style_text_color(label, lv_color_hex(
                    np.ota_allow_prerelease ? ACCENT : TEXT_PRIMARY), 0);
            }
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    // OTA from GitHub (WiFi STA + download)
    lv_obj_t* btn_gh_ota = lv_list_add_btn(list, LV_SYMBOL_DOWNLOAD, "  OTA from GitHub");
    lv_obj_set_style_bg_color(btn_gh_ota, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn_gh_ota, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_gh_ota, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_gh_ota, [](lv_event_t* e) {
        lv_obj_t* scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));

        if (sigurdos_is_under_launcher()) {
            auto dlg_sz = dialog_size(260, 80);
            lv_obj_t* dlg = lv_obj_create(scr);
            lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
            lv_obj_center(dlg);
            lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
            lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_border_width(dlg, 2, 0);
            lv_obj_set_style_radius(dlg, 0, 0);
            lv_obj_set_style_pad_all(dlg, 8, 0);

            lv_obj_t* msg = lv_label_create(dlg);
            lv_label_set_text(msg, "Update SigurdOS\nthrough Launcher instead");
            lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

            lv_obj_t* close_btn = lv_btn_create(dlg);
            lv_obj_set_size(close_btn, 80, 24);
            lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_obj_set_style_bg_color(close_btn, lv_color_hex(BG_INPUT), 0);
            lv_obj_set_style_radius(close_btn, 0, 0);
            lv_obj_t* cl = lv_label_create(close_btn);
            lv_label_set_text(cl, "OK");
            lv_obj_center(cl);
            lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
                lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
            }, LV_EVENT_CLICKED, nullptr);
            return;
        }

        auto dlg_sz = dialog_size(280, 160);
        lv_obj_t* dlg = lv_obj_create(scr);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, sigurdos::github_ota::getDownloadLabel());
        lv_obj_set_style_text_color(title, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* status_lbl = lv_label_create(dlg);
        lv_label_set_text(status_lbl, "Starting...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(status_lbl, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, -10);

        lv_obj_t* bar = lv_bar_create(dlg);
        lv_obj_set_size(bar, 240, 14);
        lv_obj_align(bar, LV_ALIGN_CENTER, 0, 20);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(ACCENT), LV_PART_INDICATOR);

        // Start the update
        if (!sigurdos::github_ota::startGitHubUpdate()) {
            lv_label_set_text(status_lbl, sigurdos::github_ota::getStatus().error_msg);
            lv_obj_set_style_text_color(status_lbl, lv_color_hex(ACCENT_RED), 0);
        }

        auto* ota_ctx = new GitHubOtaDialogCtx{dlg, {}};
        lv_obj_add_event_cb(dlg, [](lv_event_t* ev) {
            delete (GitHubOtaDialogCtx*)lv_event_get_user_data(ev);
        }, LV_EVENT_DELETE, ota_ctx);

        // Polling timer — finds label/bar from dialog children each tick.
        ota_ctx->poll_timer.attach(lv_timer_create([](lv_timer_t* t) {
            auto* ctx = (GitHubOtaDialogCtx*)lv_timer_get_user_data(t);
            if (!ctx || !lv_obj_is_valid(ctx->dialog)) {
                if (ctx) ctx->poll_timer.complete(t);
                else lv_timer_del(t);
                return;
            }
            lv_obj_t* dlg = ctx->dialog;

            const auto& st = sigurdos::github_ota::getStatus();
            // Find status label (second label) and bar
            lv_obj_t* lbl = nullptr;
            lv_obj_t* prog_bar = nullptr;
            int label_count = 0;
            uint32_t cnt = lv_obj_get_child_cnt(dlg);
            for (uint32_t i = 0; i < cnt; i++) {
                lv_obj_t* c = lv_obj_get_child(dlg, i);
                if (lv_obj_check_type(c, &lv_label_class)) {
                    label_count++;
                    if (label_count == 2) lbl = c;  // second label = status
                }
                if (lv_obj_check_type(c, &lv_bar_class)) {
                    prog_bar = c;
                }
            }
            if (lbl) {
                lv_label_set_text(lbl, st.status_msg);
                if (st.state == sigurdos::github_ota::GitHubOTAState::Failed) {
                    lv_obj_set_style_text_color(lbl, lv_color_hex(ACCENT_RED), 0);
                }
            }
            if (prog_bar) {
                lv_bar_set_value(prog_bar, st.progress_pct, LV_ANIM_ON);
            }
            if (st.state == sigurdos::github_ota::GitHubOTAState::Success ||
                st.state == sigurdos::github_ota::GitHubOTAState::Failed) {
                ctx->poll_timer.complete(t);
            }
        }, 500, ota_ctx));

        // Close button
        lv_obj_t* close_btn = lv_btn_create(dlg);
        lv_obj_set_size(close_btn, 80, 24);
        lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_radius(close_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(close_btn);
        lv_label_set_text(cl, "Close");
        lv_obj_center(cl);
        lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
            lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev));
            sigurdos::github_ota::cancel();
            lv_obj_del_async(dlg);
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    // Shut down
    lv_obj_t* btn_shutdown = lv_list_add_btn(list, LV_SYMBOL_POWER, "  Shut down");
    lv_obj_set_style_bg_color(btn_shutdown, lv_color_hex(0x4a2020), 0);
    lv_obj_set_style_bg_color(btn_shutdown, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_shutdown, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_shutdown, [](lv_event_t* e) {
        lv_obj_t* scr_sh = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        auto dlg_sz = dialog_size(240, 100);
        lv_obj_t* dlg = lv_obj_create(scr_sh);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_border_width(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, "Shut down?");
        lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* msg = lv_label_create(dlg);
        lv_label_set_text(msg, "Save state and power off?");
        lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, -4);

        lv_obj_t* cancel_btn = lv_btn_create(dlg);
        lv_obj_set_size(cancel_btn, 64, 24);
        lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -4);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_radius(cancel_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(cancel_btn);
        lv_label_set_text(cl, "Cancel");
        lv_obj_center(cl);
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ev) {
            lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* confirm_btn = lv_btn_create(dlg);
        lv_obj_set_size(confirm_btn, 64, 24);
        lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
        lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(confirm_btn, 0, 0);
        lv_obj_t* cfl = lv_label_create(confirm_btn);
        lv_label_set_text(cfl, "Shut down");
        lv_obj_center(cfl);
        lv_obj_add_event_cb(confirm_btn, [](lv_event_t*) {
            pin_clear_grace();
            sigurdos::mesh::shutdown();
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;

    row++;

    // Reboot
    lv_obj_t* btn_reboot = lv_list_add_btn(list, LV_SYMBOL_POWER, "  Reboot");
    lv_obj_set_style_bg_color(btn_reboot, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(btn_reboot, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn_reboot, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_reboot, [](lv_event_t* e) {
        lv_obj_t* root = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        auto size = dialog_size(250, 126);
        lv_obj_t* dlg = lv_obj_create(root);
        lv_obj_set_size(dlg, size.w, size.h);
        lv_obj_center(dlg);
        apply_pixel_card_accent(dlg);

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, "Confirm Reboot");
        lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

        lv_obj_t* message = lv_label_create(dlg);
        const bool allowed = system_reboot_allowed(
            sigurdos::ota::isActive(), sigurdos::github_ota::isActive());
        lv_label_set_text(message, allowed
            ? "Save all state and restart?"
            : "Reboot blocked while an update is active.");
        lv_obj_set_width(message, size.w - 20);
        lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(message,
            lv_color_hex(allowed ? TEXT_SECONDARY : ACCENT_RED), 0);
        lv_obj_align(message, LV_ALIGN_CENTER, 0, -4);

        lv_obj_t* cancel = lv_btn_create(dlg);
        lv_obj_set_size(cancel, 72, 24);
        lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 12, -6);
        apply_pixel_btn_outline(cancel);
        lv_obj_t* cancel_label = lv_label_create(cancel);
        lv_label_set_text(cancel_label, "Cancel");
        lv_obj_center(cancel_label);
        lv_obj_add_event_cb(cancel, [](lv_event_t* event) {
            lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(event)));
        }, LV_EVENT_CLICKED, nullptr);

        if (!allowed) return;
        lv_obj_t* confirm = lv_btn_create(dlg);
        lv_obj_set_size(confirm, 72, 24);
        lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, -12, -6);
        lv_obj_set_style_bg_color(confirm, lv_color_hex(ACCENT_RED), 0);
        lv_obj_t* confirm_label = lv_label_create(confirm);
        lv_label_set_text(confirm_label, "Reboot");
        lv_obj_center(confirm_label);
        lv_obj_add_event_cb(confirm, [](lv_event_t* event) {
            lv_obj_t* button = (lv_obj_t*)lv_event_get_target(event);
            lv_obj_t* dialog = lv_obj_get_parent(button);
            if (!system_reboot_allowed(sigurdos::ota::isActive(),
                                       sigurdos::github_ota::isActive())) return;
            lv_obj_add_state(button, LV_STATE_DISABLED);
            lv_obj_t* label = lv_obj_get_child(dialog, 1);
            if (label) lv_label_set_text(label, "Saving state...");
            lv_timer_t* timer = lv_timer_create([](lv_timer_t* task) {
                lv_obj_t* dialog = (lv_obj_t*)lv_timer_get_user_data(task);
                lv_timer_del(task);
                if (!lv_obj_is_valid(dialog)) return;
                if (!system_reboot_allowed(sigurdos::ota::isActive(),
                                           sigurdos::github_ota::isActive())) {
                    lv_label_set_text(lv_obj_get_child(dialog, 1),
                                      "Update started; reboot cancelled.");
                    return;
                }
                const bool saved = sigurdos::mesh::saveState() &&
                                   sigurdos::mesh::saveChannels();
                if (!saved) {
                    lv_label_set_text(lv_obj_get_child(dialog, 1),
                                      "Save failed; reboot cancelled.");
                    return;
                }
                esp_restart();
            }, 1, dialog);
            if (timer) lv_timer_set_repeat_count(timer, 1);
            else if (label) lv_label_set_text(label, "Unable to schedule reboot.");
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);

    // Factory reset
    lv_obj_t* btn_reset = lv_list_add_btn(list, LV_SYMBOL_WARNING, "  Factory reset");
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x4a2020), 0);
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x4a2020), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_reset, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_add_event_cb(btn_reset, [](lv_event_t* e) {
        lv_obj_t* scr_fr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        auto dlg_sz = dialog_size(250, 120);
        lv_obj_t* dlg = lv_obj_create(scr_fr);
        lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
        lv_obj_center(dlg);
        lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(DIVIDER), 0);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_radius(dlg, 0, 0);
        lv_obj_set_style_pad_all(dlg, 8, 0);
        lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);

        lv_obj_t* title = lv_label_create(dlg);
        lv_label_set_text(title, "Factory reset?");
        lv_obj_set_style_text_color(title, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t* msg = lv_label_create(dlg);
        lv_label_set_text(msg, "Erase all data and reboot?\nAll settings, contacts and\nidentity will be lost.");
        lv_obj_set_style_text_color(msg, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(msg, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 22);

        // Cancel
        lv_obj_t* cancel_btn = lv_btn_create(dlg);
        lv_obj_set_size(cancel_btn, 100, 28);
        lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_radius(cancel_btn, 0, 0);
        lv_obj_t* cl = lv_label_create(cancel_btn);
        lv_label_set_text(cl, "Cancel");
        lv_obj_center(cl);
        lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ev) {
            // Defer deletion: deleting the dialog (this button's parent) from
            // inside its own click handler is a use-after-free — LVGL may still
            // dereference the freed object after the callback returns. Matches
            // the async-delete pattern used by every other dialog close here.
            lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev)));
        }, LV_EVENT_CLICKED, nullptr);

        // Confirm (red, dangerous)
        lv_obj_t* confirm_btn = lv_btn_create(dlg);
        lv_obj_set_size(confirm_btn, 100, 28);
        lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
        lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(confirm_btn, 0, 0);
        lv_obj_t* cfl = lv_label_create(confirm_btn);
        lv_label_set_text(cfl, "Reset");
        lv_obj_center(cfl);
        lv_obj_add_event_cb(confirm_btn, [](lv_event_t*) {
            sigurdos::mesh::factoryReset();
        }, LV_EVENT_CLICKED, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    row++;
    {
        const auto& info = sigurdos::build::info();
        snprintf(buf, sizeof(buf), "  Build: %s / %s", info.git_sha, info.build_env);
        lv_obj_t* rb = lv_list_add_btn(list, LV_SYMBOL_HOME, buf);
        lv_obj_set_style_bg_color(rb, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(rb, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(rb, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_add_event_cb(rb, [](lv_event_t* e) {
            show_build_info_dialog(lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_CLICKED, nullptr);
        row++;
    }

    snprintf(buf, sizeof(buf), "  SigurdOS " SIGURDOS_VERSION);
    lv_obj_t* rv = lv_list_add_btn(list, LV_SYMBOL_HOME, buf);
    lv_obj_set_style_bg_color(rv, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(rv, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(rv, lv_color_hex(TEXT_PRIMARY), 0);
    row++;

    // Null row pointers on delete
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        g_date_row = nullptr;
        g_time_row = nullptr;
        g_time_source_row = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    show_screen(scr);
}

} // namespace sigurdos::ui
