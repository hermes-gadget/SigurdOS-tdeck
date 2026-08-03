// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#define SIGURDOS_TEXT_FIT_NO_LVGL_MACROS
#include "text_fit_lvgl.h"

#include "../fonts/emoji_font.h"

#include <algorithm>
#include <cstdint>

namespace sigurdos::ui {
namespace {

struct LabelFontState {
    lv_obj_t* label = nullptr;
    const lv_font_t* largest_font = nullptr;
};

// A screen can contain many labels (especially the contact and settings
// screens). Keep this bounded like the rest of the UI's widget collections;
// labels beyond the table still receive a one-shot fit using their current
// font, but normal screen sizes retain their requested-font state.
constexpr std::size_t LABEL_STATE_CAPACITY = 512;
LabelFontState g_label_states[LABEL_STATE_CAPACITY] = {};

LabelFontState* find_label_state(lv_obj_t* label)
{
    for (auto& state : g_label_states) {
        if (state.label == label) return &state;
    }
    return nullptr;
}

LabelFontState* register_label(lv_obj_t* label)
{
    if (!label) return nullptr;
    if (auto* existing = find_label_state(label)) return existing;

    for (auto& state : g_label_states) {
        if (!state.label) {
            state.label = label;
            return &state;
        }
    }
    return nullptr;
}

void unregister_label(lv_obj_t* label)
{
    if (auto* state = find_label_state(label)) *state = {};
}

void on_label_delete(lv_event_t* event)
{
    unregister_label(static_cast<lv_obj_t*>(lv_event_get_target(event)));
}

int content_width(lv_obj_t* object)
{
    if (!object) return 0;
    int width = lv_obj_get_width(object);
    width -= lv_obj_get_style_pad_left(object, 0);
    width -= lv_obj_get_style_pad_right(object, 0);
    const int border = lv_obj_get_style_border_width(object, 0);
    width -= border * 2;
    return std::max(width, 1);
}

lv_coord_t label_max_width(lv_obj_t* label)
{
    lv_obj_t* parent = lv_obj_get_parent(label);
    const int32_t style_width = lv_obj_get_style_width(label, 0);
    if (!parent || style_width != LV_SIZE_CONTENT) {
        return static_cast<lv_coord_t>(content_width(label));
    }

    const int parent_width = content_width(parent);
    // A LV_SIZE_CONTENT label reports its current rendered width, which would
    // make a previously downscaled label keep the smaller font forever. Its
    // parent is the stable clipping boundary for content-sized labels.
    return static_cast<lv_coord_t>(parent_width);
}

void append(TextFitFontLadder& ladder, const lv_font_t* font)
{
    if (!font || ladder.count >= TEXT_FIT_MAX_FONTS) return;
    ladder.fonts[ladder.count++] = font;
}

TextFitFontLadder ladder_for_font(const lv_font_t* largest)
{
    TextFitFontLadder ladder;
    const lv_font_t* current = largest;

    if (current == emoji_wrapped_montserrat_24) {
        append(ladder, emoji_wrapped_montserrat_24);
        append(ladder, emoji_wrapped_montserrat_16);
        append(ladder, emoji_wrapped_montserrat_14);
        append(ladder, emoji_wrapped_montserrat_12);
        append(ladder, emoji_wrapped_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    } else if (current == emoji_wrapped_montserrat_16) {
        append(ladder, emoji_wrapped_montserrat_16);
        append(ladder, emoji_wrapped_montserrat_14);
        append(ladder, emoji_wrapped_montserrat_12);
        append(ladder, emoji_wrapped_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    } else if (current == emoji_wrapped_montserrat_14) {
        append(ladder, emoji_wrapped_montserrat_14);
        append(ladder, emoji_wrapped_montserrat_12);
        append(ladder, emoji_wrapped_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    } else if (current == emoji_wrapped_montserrat_12) {
        append(ladder, emoji_wrapped_montserrat_12);
        append(ladder, emoji_wrapped_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    } else if (current == emoji_wrapped_montserrat_10) {
        append(ladder, emoji_wrapped_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    } else if (current == &lv_font_montserrat_24) {
        append(ladder, &lv_font_montserrat_24);
        append(ladder, &lv_font_montserrat_16);
        append(ladder, &lv_font_montserrat_14);
        append(ladder, &lv_font_montserrat_12);
        append(ladder, &lv_font_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    } else if (current == &lv_font_montserrat_16) {
        append(ladder, &lv_font_montserrat_16);
        append(ladder, &lv_font_montserrat_14);
        append(ladder, &lv_font_montserrat_12);
        append(ladder, &lv_font_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    } else if (current == &lv_font_montserrat_14) {
        append(ladder, &lv_font_montserrat_14);
        append(ladder, &lv_font_montserrat_12);
        append(ladder, &lv_font_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    } else if (current == &lv_font_montserrat_12) {
        append(ladder, &lv_font_montserrat_12);
        append(ladder, &lv_font_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    } else if (current == &lv_font_montserrat_10) {
        append(ladder, &lv_font_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    } else if (current) {
        // Unknown/custom fonts keep their normal appearance when they fit.
        // The wrapped 10px font provides a safe UI fallback if a long label
        // is otherwise unconstrained.
        append(ladder, current);
        append(ladder, emoji_wrapped_montserrat_10);
        append(ladder, emoji_wrapped_montserrat_8);
    }

    return ladder;
}

int measure_label(const char* text, const void* font, void* context)
{
    lv_point_t size{};
    const lv_obj_t* label = static_cast<const lv_obj_t*>(context);
    const int32_t letter_space = label
        ? lv_obj_get_style_text_letter_space(label, 0)
        : 0;
    // LVGL 9 renamed the v8 lv_txt_get_size API to lv_text_get_size. Both
    // paths measure through LVGL's actual font/fallback renderer.
    lv_text_get_size(&size,
                     text ? text : "",
                     static_cast<const lv_font_t*>(font),
                     letter_space,
                     0,
                     LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    return static_cast<int>(size.x);
}

void on_label_draw(lv_event_t* event)
{
    // Fitting during a draw event is forbidden: a font change invalidates the
    // display while rendering is in progress, which LVGL asserts on
    // (lv_refr.c: "Invalidate area is not allowed during rendering").
    // Defer the re-fit to the next timer tick instead.
    lv_obj_t* label = static_cast<lv_obj_t*>(lv_event_get_target(event));
    lv_async_call([](void* user_data) {
        text_fit_apply(static_cast<lv_obj_t*>(user_data));
    }, label);
}

} // namespace

void text_fit_label(lv_obj_t* label,
                    const char* text,
                    lv_coord_t max_width,
                    const TextFitFontLadder& ladder)
{
    if (!label) return;
    if (auto* state = register_label(label)) {
        state->largest_font = ladder.count > 0
            ? static_cast<const lv_font_t*>(ladder.fonts[0])
            : nullptr;
    }
    lv_label_set_text(label, text ? text : "");
    const TextFitChoice choice = text_fit_choose_font(
        text, max_width, ladder, measure_label, label);
    if (choice.font) {
        lv_obj_set_style_text_font(
            label, static_cast<const lv_font_t*>(choice.font), 0);
    }
}

lv_obj_t* text_fit_label_create(lv_obj_t* parent)
{
    lv_obj_t* label = lv_label_create(parent);
    if (!label) return nullptr;

    register_label(label);
    lv_obj_add_event_cb(label, on_label_draw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(label, on_label_delete, LV_EVENT_DELETE, nullptr);
    return label;
}

void text_fit_set_text(lv_obj_t* label, const char* text)
{
    if (!label) return;
    register_label(label);
    lv_label_set_text(label, text ? text : "");
    text_fit_apply(label);
}

void text_fit_set_font(lv_obj_t* object, const lv_font_t* font, int selector)
{
    if (!object) return;
    if (auto* state = find_label_state(object)) state->largest_font = font;
    lv_obj_set_style_text_font(object, font, selector);
    if (find_label_state(object)) text_fit_apply(object);
}

void text_fit_apply(lv_obj_t* label)
{
    if (!label) return;
    const LabelFontState* state = find_label_state(label);
    const lv_font_t* largest = state && state->largest_font
        ? state->largest_font
        : lv_obj_get_style_text_font(label, 0);
    const TextFitFontLadder ladder = ladder_for_font(largest);
    if (ladder.count == 0) return;

    const char* text = lv_label_get_text(label);
    const TextFitChoice choice = text_fit_choose_font(
        text, label_max_width(label), ladder, measure_label, label);
    const lv_font_t* selected = static_cast<const lv_font_t*>(choice.font);
    if (selected && lv_obj_get_style_text_font(label, 0) != selected) {
        lv_obj_set_style_text_font(label, selected, 0);
    }
}

} // namespace sigurdos::ui
