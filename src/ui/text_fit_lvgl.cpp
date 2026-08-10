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
    std::uint32_t generation = 0;
    std::uint32_t pending_generation = 0;
    std::uint32_t fitted_text_hash = 0;
    lv_coord_t fitted_max_width = 0;
    bool fit_pending = false;
    bool fitted_inputs_valid = false;
    bool retired = false;
};

// A screen can contain many labels (especially the contact and settings
// screens). Keep this bounded like the rest of the UI's widget collections;
// labels beyond the table still receive synchronous updates, but never queue
// deferred work without a tracked lifetime.
#ifndef SIGURDOS_TEXT_FIT_LABEL_STATE_CAPACITY
constexpr std::size_t LABEL_STATE_CAPACITY = 512;
#else
constexpr std::size_t LABEL_STATE_CAPACITY =
    SIGURDOS_TEXT_FIT_LABEL_STATE_CAPACITY;
#endif
LabelFontState g_label_states[LABEL_STATE_CAPACITY] = {};
std::uint32_t g_next_label_generation = 1;

void apply_deferred_fit(void* user_data);

std::uint32_t next_label_generation()
{
    // Keep zero reserved for empty state records. Matching also includes the
    // state slot, so wrap does not make a retired pending slot reusable.
    if (g_next_label_generation == 0) g_next_label_generation = 1;
    return g_next_label_generation++;
}

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
        // A retired slot with a callback that could not be cancelled remains
        // reserved until that callback drains.
        if (!state.label && !state.fit_pending) {
            state.label = label;
            state.generation = next_label_generation();
            return &state;
        }
    }
    return nullptr;
}

void unregister_label(lv_obj_t* label)
{
    auto* state = find_label_state(label);
    if (!state) return;

    state->label = nullptr;
    state->largest_font = nullptr;
    state->retired = true;
    state->generation = next_label_generation();

    if (!state->fit_pending ||
        lv_async_call_cancel(apply_deferred_fit, state) == LV_RESULT_OK) {
        *state = {};
    }
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

std::uint32_t text_hash(const char* text)
{
    // FNV-1a gives the draw hook a bounded state value instead of retaining a
    // pointer into LVGL's mutable label buffer.
    std::uint32_t hash = 2166136261U;
    const auto* byte = reinterpret_cast<const unsigned char*>(text ? text : "");
    while (*byte) {
        hash ^= *byte++;
        hash *= 16777619U;
    }
    return hash;
}

void record_fitted_inputs(LabelFontState* state,
                          const char* text,
                          lv_coord_t max_width)
{
    if (!state) return;
    state->fitted_text_hash = text_hash(text);
    state->fitted_max_width = max_width;
    state->fitted_inputs_valid = true;
}

bool fitted_inputs_changed(LabelFontState* state,
                           const char* text,
                           lv_coord_t max_width)
{
    return !state || !state->fitted_inputs_valid ||
           state->fitted_text_hash != text_hash(text) ||
           state->fitted_max_width != max_width;
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

void apply_registered_label(LabelFontState* state,
                            std::uint32_t expected_generation)
{
    if (!state || state->retired || !state->label ||
        state->generation != expected_generation) {
        return;
    }

    lv_obj_t* label = state->label;
    const lv_font_t* largest = state->largest_font
        ? state->largest_font
        : lv_obj_get_style_text_font(label, 0);
    const TextFitFontLadder ladder = ladder_for_font(largest);
    const char* text = lv_label_get_text(label);
    const lv_coord_t max_width = label_max_width(label);
    if (ladder.count == 0) {
        record_fitted_inputs(state, text, max_width);
        return;
    }

    const TextFitChoice choice = text_fit_choose_font(
        text, max_width, ladder, measure_label, label);
    const lv_font_t* selected = static_cast<const lv_font_t*>(choice.font);
    if (selected && lv_obj_get_style_text_font(label, 0) != selected) {
        lv_obj_set_style_text_font(label, selected, 0);
    }
    record_fitted_inputs(state, text, max_width);
}

void apply_deferred_fit(void* user_data)
{
    auto* state = static_cast<LabelFontState*>(user_data);
    if (!state || !state->fit_pending) return;

    const std::uint32_t pending_generation = state->pending_generation;
    state->fit_pending = false;
    state->pending_generation = 0;

    if (state->retired || !state->label ||
        state->generation != pending_generation) {
        if (state->retired) *state = {};
        return;
    }

    apply_registered_label(state, pending_generation);
}

void on_label_draw(lv_event_t* event)
{
    // Fitting during a draw event is forbidden: a font change invalidates the
    // display while rendering is in progress, which LVGL asserts on
    // (lv_refr.c: "Invalidate area is not allowed during rendering").
    // Defer the re-fit to the next timer tick instead.
    lv_obj_t* label = static_cast<lv_obj_t*>(lv_event_get_target(event));
    LabelFontState* state = find_label_state(label);
    if (!state || state->retired || state->fit_pending) return;

    const lv_coord_t max_width = label_max_width(label);
    if (!fitted_inputs_changed(state, lv_label_get_text(label), max_width)) {
        return;
    }

    state->fit_pending = true;
    state->pending_generation = state->generation;
    if (lv_async_call(apply_deferred_fit, state) != LV_RESULT_OK) {
        state->fit_pending = false;
        state->pending_generation = 0;
    }
}

} // namespace

void text_fit_label(lv_obj_t* label,
                    const char* text,
                    lv_coord_t max_width,
                    const TextFitFontLadder& ladder)
{
    if (!label) return;
    LabelFontState* state = register_label(label);
    if (state) {
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
    record_fitted_inputs(state, text, max_width);
}

lv_obj_t* text_fit_label_create(lv_obj_t* parent)
{
    lv_obj_t* label = lv_label_create(parent);
    if (!label) return nullptr;

    if (!register_label(label)) return label;
    lv_obj_add_event_cb(label, on_label_draw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(label, on_label_delete, LV_EVENT_DELETE, nullptr);
    return label;
}

void text_fit_set_text(lv_obj_t* label, const char* text)
{
    if (!label) return;
    LabelFontState* state = register_label(label);
    lv_label_set_text(label, text ? text : "");
    if (state) apply_registered_label(state, state->generation);
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
    LabelFontState* state = find_label_state(label);
    if (!state) return;
    apply_registered_label(state, state->generation);
}

} // namespace sigurdos::ui
