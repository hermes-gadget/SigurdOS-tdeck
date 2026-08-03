// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "text_fit.h"

namespace sigurdos::ui {

TextFitChoice text_fit_choose_font(const char* text,
                                   int max_width,
                                   const TextFitFontLadder& ladder,
                                   TextFitMeasure measure,
                                   void* context)
{
    TextFitChoice choice;
    const std::size_t count = ladder.count < TEXT_FIT_MAX_FONTS
        ? ladder.count
        : TEXT_FIT_MAX_FONTS;
    if (count == 0) return choice;

    const char* safe_text = text ? text : "";
    const std::size_t last = count - 1;
    if (safe_text[0] == '\0') {
        choice.font = ladder.fonts[0];
        choice.index = 0;
        return choice;
    }
    if (max_width <= 0 || !measure) {
        choice.font = ladder.fonts[last];
        choice.index = last;
        return choice;
    }

    for (std::size_t index = 0; index < count; ++index) {
        const int measured = measure(safe_text, ladder.fonts[index], context);
        if (measured <= max_width) {
            choice.font = ladder.fonts[index];
            choice.index = index;
            return choice;
        }
    }

    choice.font = ladder.fonts[last];
    choice.index = last;
    return choice;
}

} // namespace sigurdos::ui
