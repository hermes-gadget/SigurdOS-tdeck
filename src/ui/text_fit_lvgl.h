// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <lvgl.h>

#include "text_fit.h"

namespace sigurdos::ui {

// Explicit API for callers that need a known box and ladder (and for tests of
// the LVGL boundary). The ordinary UI paths use the automatic wrappers below.
void text_fit_label(lv_obj_t* label,
                    const char* text,
                    lv_coord_t max_width,
                    const TextFitFontLadder& ladder);

lv_obj_t* text_fit_label_create(lv_obj_t* parent);
void text_fit_set_text(lv_obj_t* label, const char* text);
void text_fit_set_font(lv_obj_t* object, const lv_font_t* font, int selector);
void text_fit_apply(lv_obj_t* label);

} // namespace sigurdos::ui

// Keep the existing UI call sites readable while routing every label through
// the central fitter. The implementation translation unit disables these
// aliases before calling the underlying LVGL functions.
#ifndef SIGURDOS_TEXT_FIT_NO_LVGL_MACROS
#define lv_label_create(parent) \
    ::sigurdos::ui::text_fit_label_create((parent))
#define lv_label_set_text(label, text) \
    ::sigurdos::ui::text_fit_set_text((label), (text))
#define lv_obj_set_style_text_font(object, font, selector) \
    ::sigurdos::ui::text_fit_set_font((object), (font), (selector))
#endif
