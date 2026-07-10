// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// QR code display screen for SigurdOS.
// Uses ricmoo's QRCode library (MIT) to generate and render QR codes.

#include "qr_show.h"
#include "mesh/mesh_wrapper.h"
#include "ui/theme.h"
#include "ui/responsive.h"
#include "ui/navigation.h"
#include "hal/battery.h"
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

using sigurdos::responsive::TOP_BAR_H;
using sigurdos::responsive::BOT_BAR_H;
using sigurdos::responsive::DIVIDER_H;
using sigurdos::responsive::CONTENT_W;
using sigurdos::responsive::CONTENT_H;
using sigurdos::responsive::DISPLAY_W;
using sigurdos::responsive::DISPLAY_H;

// ricmoo QRCode library — MIT license, pure C99 implementation.
#include "qrcode.h"

namespace sigurdos {
namespace app {

using namespace sigurdos::theme;

namespace {

void qr_show_error(lv_obj_t* scr, const char* message)
{
    lv_obj_t* err = lv_label_create(scr);
    lv_label_set_text(err, message);
    lv_obj_set_style_text_color(err, lv_color_hex(ACCENT_RED), 0);
    lv_obj_align(err, LV_ALIGN_CENTER, 0, 0);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
}

} // namespace

void qr_show(const char* title, const char* data)
{
    // ── Create full-screen ─────────────────────────────────
    lv_obj_t* scr = lv_obj_create(nullptr);
    apply_dark_bg(scr);

    // ── Top bar ────────────────────────────────────────────
    lv_obj_t* top = lv_obj_create(scr);
    lv_obj_set_size(top, LV_PCT(100), TOP_BAR_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);

    // Back button
    lv_obj_t* back = lv_btn_create(top);
    lv_obj_set_size(back, 24, TOP_BAR_H - 4);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
    apply_topbar_icon_btn(back);
    lv_obj_add_event_cb(back, [](lv_event_t*) { sigurdos::ui::go_back(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_12, 0);
    lv_obj_center(back_icon);

    // Screen title
    if (title && title[0]) {
        lv_obj_t* ttl = lv_label_create(top);
        lv_label_set_text(ttl, title);
        lv_obj_set_style_text_color(ttl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(ttl, &lv_font_montserrat_10, 0);
        lv_obj_align(ttl, LV_ALIGN_CENTER, 0, 0);
    }

    // Time (24h snapshot)
    {
        uint32_t epoch = sigurdos::mesh::getCurrentTime();
        char t[8];
        if (epoch == 0) {
            snprintf(t, sizeof(t), "--:--");
        } else {
            uint32_t sec = epoch % 86400;
            snprintf(t, sizeof(t), "%02d:%02d", (sec / 3600) % 24, (sec / 60) % 60);
        }
        lv_obj_t* tl = lv_label_create(top);
        lv_label_set_text(tl, t);
        lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_12, 0);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Signal dots (right of top bar)
    {
        int rssi = sigurdos::mesh::getLastRSSI();
        lv_obj_t* sig = create_signal_dots(top, rssi);
        lv_obj_align(sig, LV_ALIGN_RIGHT_MID, -54, 0);
    }

    // Top divider
    lv_obj_t* tdiv = lv_obj_create(scr);
    lv_obj_set_size(tdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(tdiv, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);
    lv_obj_set_style_bg_color(tdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(tdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tdiv, 0, 0);

    // ── Generate QR code ───────────────────────────────────
    // Use an explicitly-sized module buffer for the configured version.
    uint8_t qr_modules[SIGURDOS_QR_MODULE_BUFFER_BYTES];
    QRCode qr;

    if (!data || !data[0] ||
        qrcode_initText(&qr, qr_modules, SIGURDOS_QR_VERSION, ECC_MEDIUM, data) != 0) {
        // Show error if QR generation fails
        // Still show screen so user can go back
        qr_show_error(scr, "QR generation failed");
        return;
    }

    // ── Determine scale factor ─────────────────────────────
    // Pick the largest integer scale that keeps the QR code within the content
    // area, leaving a small margin.
    QrCanvasLayout qr_layout = sigurdos_qr_canvas_layout(qr.size, CONTENT_W, CONTENT_H);
    if (!qr_layout.fits) {
        qr_show_error(scr, "QR too large");
        return;
    }

    int scale = qr_layout.scale;
    int canvas_size = qr_layout.canvas_size;

    // ── Create canvas for QR rendering ─────────────────────
    // 180*180*2 = 64,800 bytes — far too large to keep permanently in internal
    // DRAM. Allocate once from PSRAM and reuse: only one QR screen exists at a
    // time (screens auto-delete on navigation), so a persistent buffer is safe.
    // Fall back to internal RAM only if PSRAM is unavailable.
    static constexpr size_t kCanvasBytes =
        (size_t)SIGURDOS_QR_CANVAS_MAX_PX * SIGURDOS_QR_CANVAS_MAX_PX * 2;
    static uint8_t* cbuf = nullptr;
    if (!cbuf) {
        cbuf = (uint8_t*)heap_caps_malloc(kCanvasBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!cbuf) {
            cbuf = (uint8_t*)heap_caps_malloc(kCanvasBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (!cbuf) {
        qr_show_error(scr, "QR: out of memory");
        return;
    }

    lv_obj_t* canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(canvas, cbuf, canvas_size, canvas_size, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(canvas, lv_color_hex(BG_PRIMARY), LV_OPA_COVER);

    // Draw QR code modules onto canvas
    lv_color_t fg = lv_color_hex(TEXT_PRIMARY);
    for (int y = 0; y < qr.size; y++) {
        for (int x = 0; x < qr.size; x++) {
            if (qrcode_getModule(&qr, x, y)) {
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        lv_canvas_set_px(canvas, x * scale + dx, y * scale + dy, fg, LV_OPA_COVER);
                    }
                }
            }
        }
    }

    // The QR canvas draws as a white square on dark bg; add a 2px border
    // around it for visual separation from the background.
    lv_obj_set_style_border_width(canvas, PIXEL_BORDER, 0);
    lv_obj_set_style_border_color(canvas, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_pad_all(canvas, 0, 0);
    lv_obj_set_style_radius(canvas, 0, 0);

    // Center the QR code canvas on screen (content area center ≈ screen center)
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);

    // ── Bottom bar ─────────────────────────────────────────
    lv_obj_t* bot = lv_obj_create(scr);
    lv_obj_set_size(bot, LV_PCT(100), BOT_BAR_H);
    lv_obj_align(bot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(bot, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bot, 0, 0);
    lv_obj_set_style_border_width(bot, 0, 0);

    // Device name (left)
    lv_obj_t* dev = lv_label_create(bot);
    lv_label_set_text(dev, sigurdos::mesh::getOwnName());
    lv_obj_set_style_text_color(dev, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dev, &lv_font_montserrat_10, 0);
    lv_obj_align(dev, LV_ALIGN_LEFT_MID, 4, 0);

    // Battery percentage (right)
    {
        char batt[8];
        int pct = sigurdos_battery_pct();
        snprintf(batt, sizeof(batt), "%d%%", pct);
        lv_obj_t* bl = lv_label_create(bot);
        lv_label_set_text(bl, batt);
        lv_obj_set_style_text_color(bl,
            lv_color_hex(pct > 20 ? ACCENT : ACCENT_RED), 0);
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_10, 0);
        lv_obj_align(bl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Bottom divider
    lv_obj_t* bdiv = lv_obj_create(scr);
    lv_obj_set_size(bdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(bdiv, LV_ALIGN_TOP_MID, 0, DISPLAY_H - BOT_BAR_H - DIVIDER_H);
    lv_obj_set_style_bg_color(bdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(bdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bdiv, 0, 0);

    // ── Show the screen ────────────────────────────────────
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
}

} // namespace app
} // namespace sigurdos
