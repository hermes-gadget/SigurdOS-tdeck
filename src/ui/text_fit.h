// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <cstddef>

namespace sigurdos::ui {

constexpr std::size_t TEXT_FIT_MAX_FONTS = 8;

// The core fitter deliberately knows nothing about LVGL. A caller supplies
// the fonts in descending visual-size order and a measurement callback that
// uses the renderer's real font metrics.
struct TextFitFontLadder {
    const void* fonts[TEXT_FIT_MAX_FONTS] = {};
    std::size_t count = 0;
};

using TextFitMeasure = int (*)(const char* text, const void* font, void* context);

struct TextFitChoice {
    const void* font = nullptr;
    std::size_t index = 0;
};

// Select the first font whose measured single-line width fits max_width.
// If no font fits, the last (smallest) ladder entry is returned. Empty text
// keeps the largest font because it has no width to constrain.
TextFitChoice text_fit_choose_font(const char* text,
                                   int max_width,
                                   const TextFitFontLadder& ladder,
                                   TextFitMeasure measure,
                                   void* context = nullptr);

} // namespace sigurdos::ui
