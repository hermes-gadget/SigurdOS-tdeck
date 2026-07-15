#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// QR code display screen for SigurdOS.
// Uses ricmoo's QRCode library (MIT) to render QR codes on a full-screen LVGL canvas.

#include <cstddef>
#include <cstring>

namespace sigurdos {
namespace app {

static constexpr int SIGURDOS_QR_LAYOUT_MARGIN_PX = 20;
static constexpr int SIGURDOS_QR_MAX_SCALE = 6;
static constexpr int SIGURDOS_QR_CANVAS_MAX_PX = 180;
static constexpr int SIGURDOS_QR_VERSION = 10;
static constexpr std::size_t SIGURDOS_QR_MAX_PAYLOAD_BYTES = 213;
static constexpr int SIGURDOS_QR_QUIET_ZONE_MODULES = 4;

inline bool sigurdos_qr_payload_fits(const char* data) {
    return data && std::strlen(data) <= SIGURDOS_QR_MAX_PAYLOAD_BYTES;
}

inline constexpr int sigurdos_qr_module_count(int version) {
    return (version <= 0) ? 0 : (4 * version) + 17;
}

inline constexpr int sigurdos_qr_module_buffer_bytes(int version) {
    return (version <= 0) ? 0 :
        ((sigurdos_qr_module_count(version) * sigurdos_qr_module_count(version)) + 7) / 8;
}

static constexpr int SIGURDOS_QR_MODULE_BUFFER_BYTES =
    sigurdos_qr_module_buffer_bytes(SIGURDOS_QR_VERSION);

struct QrCanvasLayout {
    int scale;
    int canvas_size;
    bool fits;
};

inline QrCanvasLayout sigurdos_qr_canvas_layout(
    int qr_size,
    int content_w,
    int content_h,
    int margin = SIGURDOS_QR_LAYOUT_MARGIN_PX,
    int max_scale = SIGURDOS_QR_MAX_SCALE,
    int max_canvas_px = SIGURDOS_QR_CANVAS_MAX_PX) {
    QrCanvasLayout layout{0, 0, false};
    if (qr_size <= 0 || content_w <= margin || content_h <= margin ||
        max_scale <= 0 || max_canvas_px <= 0) {
        return layout;
    }

    const int avail_w = content_w - margin;
    const int avail_h = content_h - margin;
    const long long rendered_modules = static_cast<long long>(qr_size) +
        2LL * SIGURDOS_QR_QUIET_ZONE_MODULES;
    for (int scale = 1; scale <= max_scale; ++scale) {
        const long long canvas_size64 = rendered_modules * scale;
        if (canvas_size64 > max_canvas_px) {
            continue;
        }
        const int canvas_size = static_cast<int>(canvas_size64);
        if (canvas_size <= avail_w && canvas_size <= avail_h &&
            canvas_size <= max_canvas_px) {
            layout.scale = scale;
            layout.canvas_size = canvas_size;
            layout.fits = true;
        }
    }

    return layout;
}

template <typename ModuleGetter, typename PixelSetter>
inline void sigurdos_qr_render_dark_modules(int qr_size, int scale,
                                             ModuleGetter module_is_dark,
                                             PixelSetter set_dark_pixel) {
    if (qr_size <= 0 || scale <= 0) return;

    const int origin = SIGURDOS_QR_QUIET_ZONE_MODULES * scale;
    for (int y = 0; y < qr_size; ++y) {
        for (int x = 0; x < qr_size; ++x) {
            if (!module_is_dark(x, y)) continue;
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    set_dark_pixel(origin + x * scale + dx,
                                   origin + y * scale + dy);
                }
            }
        }
    }
}

template <typename T, typename FreeFn>
inline bool sigurdos_qr_release_canvas_buffer(T* buffer, FreeFn free_fn) {
    if (!buffer) return false;
    free_fn(buffer);
    return true;
}

/// Show a full-screen QR code displaying the given data string.
/// The screen has a themed top bar with a back button and a bottom bar.
/// The QR code is auto-sized and centered in the content area.
/// @param title  Text shown in the top bar (e.g. "Contact Key" or "Channel Secret")
/// @param data   Data string to encode as a QR code (text, URI, hex key, etc.)
void qr_show(const char* title, const char* data);

} // namespace app
} // namespace sigurdos
