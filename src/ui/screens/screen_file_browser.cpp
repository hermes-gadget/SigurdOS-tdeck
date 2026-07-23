// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "../screens.h"
#include "../screens_common.h"
#include "../file_browser_model.h"
#include "../responsive.h"
#include "../theme.h"
#include "../../fonts/emoji_font.h"
#include "../../hal/sdcard.h"

#include <lvgl.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

namespace sigurdos::ui {

using namespace responsive;
using namespace theme;

namespace {

constexpr size_t MAX_VISIBLE_ENTRIES = 32;
constexpr size_t PREVIEW_BYTES = 768;

SigurdosSdDirEntry g_entries[MAX_VISIBLE_ENTRIES];
size_t g_entry_count = 0;
int g_selected_index = -1;
char g_current_path[SIGURDOS_SD_MAX_PATH_LEN + 1] = "/";
lv_obj_t* g_screen = nullptr;
lv_obj_t* g_list = nullptr;
lv_obj_t* g_path_label = nullptr;
lv_obj_t* g_selected_row = nullptr;
lv_obj_t* g_open_button = nullptr;
lv_obj_t* g_copy_button = nullptr;
lv_obj_t* g_delete_button = nullptr;
lv_obj_t* g_feedback = nullptr;

void renderDirectory();

void setActionsEnabled(bool enabled)
{
    lv_obj_t* buttons[] = {g_open_button, g_copy_button, g_delete_button};
    for (lv_obj_t* button : buttons) {
        if (!button) continue;
        if (enabled) {
            lv_obj_clear_state(button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
    }
}

void showFeedback(const char* message, bool success)
{
    if (!g_screen) return;
    if (g_feedback) lv_obj_del(g_feedback);
    g_feedback = lv_label_create(g_screen);
    lv_label_set_text(g_feedback, message);
    lv_obj_set_style_text_font(g_feedback, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_text_color(g_feedback, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_color(
        g_feedback, lv_color_hex(success ? ACCENT_GREEN : ACCENT_RED), 0);
    lv_obj_set_style_bg_opa(g_feedback, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_feedback, 4, 0);
    lv_obj_set_style_radius(g_feedback, 0, 0);
    lv_obj_align(g_feedback, LV_ALIGN_BOTTOM_MID, 0, -32);
}

bool selectedPath(char* out, size_t out_size)
{
    return g_selected_index >= 0 &&
        static_cast<size_t>(g_selected_index) < g_entry_count &&
        !g_entries[g_selected_index].is_directory &&
        file_browser::joinPath(
            g_current_path, g_entries[g_selected_index].name, out, out_size);
}

void formatDate(std::time_t modified, char* out, size_t out_size)
{
    if (!out || out_size == 0) return;
    struct tm calendar {};
    if (modified <= 0 || !localtime_r(&modified, &calendar)) {
        std::snprintf(out, out_size, "---- -- --");
        return;
    }
    std::strftime(out, out_size, "%Y-%m-%d", &calendar);
}

void closeDialog(lv_event_t* event)
{
    lv_obj_del_async(static_cast<lv_obj_t*>(lv_event_get_user_data(event)));
}

void showPreview()
{
    char path[SIGURDOS_SD_MAX_PATH_LEN + 1];
    if (!selectedPath(path, sizeof(path))) return;

    uint8_t raw[PREVIEW_BYTES];
    const size_t read = sigurdos_sdcard_read(path, raw, sizeof(raw));
    char preview[PREVIEW_BYTES + 1];
    size_t controls = 0;
    for (size_t index = 0; index < read; ++index) {
        const unsigned char value = raw[index];
        if (value == '\n' || value == '\r' || value == '\t' || std::isprint(value)) {
            preview[index] = static_cast<char>(value);
        } else {
            preview[index] = '.';
            controls++;
        }
    }
    preview[read] = '\0';
    if (read == 0) {
        std::snprintf(preview, sizeof(preview), "(empty file)");
    } else if (controls > read / 4) {
        std::snprintf(preview, sizeof(preview),
                      "Binary preview unavailable.\n\n%s", path);
    }

    const auto size = dialog_size(286, 174);
    lv_obj_t* dialog = lv_obj_create(g_screen);
    lv_obj_set_size(dialog, size.w, size.h);
    lv_obj_center(dialog);
    apply_pixel_card_accent(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_pad_all(dialog, 7, 0);

    lv_obj_t* title = lv_label_create(dialog);
    lv_label_set_text(title, g_entries[g_selected_index].name);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, size.w - 58);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 2, 1);

    lv_obj_t* close = lv_btn_create(dialog);
    lv_obj_set_size(close, 30, 22);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -1, -2);
    apply_pixel_btn_outline(close);
    lv_obj_t* close_label = lv_label_create(close);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close, closeDialog, LV_EVENT_CLICKED, dialog);

    lv_obj_t* preview_box = lv_obj_create(dialog);
    lv_obj_set_size(preview_box, size.w - 14, size.h - 42);
    lv_obj_align(preview_box, LV_ALIGN_BOTTOM_MID, 0, 0);
    apply_pixel_input(preview_box);
    lv_obj_set_scroll_dir(preview_box, LV_DIR_VER);
    lv_obj_t* body = lv_label_create(preview_box);
    lv_obj_set_width(body, size.w - 30);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, preview);
    lv_obj_set_style_text_font(body, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 0);
}

void copySelected()
{
    char source[SIGURDOS_SD_MAX_PATH_LEN + 1];
    if (!selectedPath(source, sizeof(source))) return;

    char copy_name[SIGURDOS_SD_MAX_NAME_LEN + 1];
    char destination[SIGURDOS_SD_MAX_PATH_LEN + 1];
    bool available = false;
    for (unsigned sequence = 1; sequence <= 99; ++sequence) {
        if (!file_browser::copyName(
                g_entries[g_selected_index].name, sequence,
                copy_name, sizeof(copy_name)) ||
            !file_browser::joinPath(
                g_current_path, copy_name, destination, sizeof(destination))) {
            break;
        }
        if (!sigurdos_sdcard_exists(destination)) {
            available = true;
            break;
        }
    }

    if (!available || !sigurdos_sdcard_copy_file(source, destination)) {
        showFeedback("Copy failed", false);
        return;
    }
    renderDirectory();
    showFeedback("File copied", true);
}

struct DeleteContext {
    char path[SIGURDOS_SD_MAX_PATH_LEN + 1];
    lv_obj_t* dialog;
};

void confirmDelete(lv_event_t* event)
{
    auto* context = static_cast<DeleteContext*>(lv_event_get_user_data(event));
    if (!context) return;
    const bool removed = sigurdos_sdcard_delete_file(context->path);
    lv_obj_del_async(context->dialog);
    renderDirectory();
    showFeedback(removed ? "File deleted" : "Delete failed", removed);
}

void requestDelete()
{
    auto* context = new(std::nothrow) DeleteContext{};
    if (!context || !selectedPath(context->path, sizeof(context->path))) {
        delete context;
        showFeedback("Delete unavailable", false);
        return;
    }

    const auto size = dialog_size(250, 110);
    lv_obj_t* dialog = lv_obj_create(g_screen);
    context->dialog = dialog;
    lv_obj_set_size(dialog, size.w, size.h);
    lv_obj_center(dialog);
    apply_pixel_card_accent(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(BG_SECONDARY), 0);

    lv_obj_t* message = lv_label_create(dialog);
    lv_label_set_text(message, "Delete selected file?");
    lv_obj_set_style_text_color(message, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(message, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* cancel = lv_btn_create(dialog);
    lv_obj_set_size(cancel, 86, 26);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 10, -8);
    apply_pixel_btn_outline(cancel);
    lv_obj_t* cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, closeDialog, LV_EVENT_CLICKED, dialog);

    lv_obj_t* remove = lv_btn_create(dialog);
    lv_obj_set_size(remove, 86, 26);
    lv_obj_align(remove, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
    apply_pixel_btn(remove);
    lv_obj_set_style_bg_color(remove, lv_color_hex(ACCENT_RED), 0);
    lv_obj_t* remove_label = lv_label_create(remove);
    lv_label_set_text(remove_label, "Delete");
    lv_obj_center(remove_label);
    lv_obj_add_event_cb(remove, confirmDelete, LV_EVENT_CLICKED, context);
    lv_obj_add_event_cb(dialog, [](lv_event_t* event) {
        delete static_cast<DeleteContext*>(lv_event_get_user_data(event));
    }, LV_EVENT_DELETE, context);
}

void selectEntry(size_t index, lv_obj_t* row)
{
    if (index >= g_entry_count) return;
    const SigurdosSdDirEntry& entry = g_entries[index];
    if (entry.is_directory) {
        char next_path[SIGURDOS_SD_MAX_PATH_LEN + 1];
        if (file_browser::joinPath(
                g_current_path, entry.name, next_path, sizeof(next_path))) {
            std::snprintf(g_current_path, sizeof(g_current_path), "%s", next_path);
            renderDirectory();
        } else {
            showFeedback("Path is too long", false);
        }
        return;
    }

    if (g_selected_row) {
        lv_obj_set_style_border_width(g_selected_row, 0, 0);
    }
    g_selected_index = static_cast<int>(index);
    g_selected_row = row;
    lv_obj_set_style_border_color(row, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(row, PIXEL_BORDER, 0);
    setActionsEnabled(true);
}

void addEntryRow(size_t index)
{
    const SigurdosSdDirEntry& entry = g_entries[index];
    char size_text[20] = "<DIR>";
    char date_text[16];
    if (!entry.is_directory) {
        sigurdos_sdcard_format_size(entry.size_bytes, size_text, sizeof(size_text));
    }
    formatDate(entry.modified_time, date_text, sizeof(date_text));

    lv_obj_t* row = lv_list_add_btn(
        g_list, entry.is_directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, entry.name);
    lv_obj_set_height(row, 38);
    lv_obj_set_style_bg_color(
        row, lv_color_hex(index % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(row, lv_color_hex(TEXT_PRIMARY), 0);

    char metadata[40];
    std::snprintf(metadata, sizeof(metadata), "%s\n%s", size_text, date_text);
    lv_obj_t* meta = lv_label_create(row);
    lv_label_set_text(meta, metadata);
    lv_obj_set_style_text_align(meta, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(meta, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_text_color(meta, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_align(meta, LV_ALIGN_RIGHT_MID, -3, 0);

    lv_obj_add_event_cb(row, [](lv_event_t* event) {
        const size_t item = static_cast<size_t>(
            reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
        selectEntry(item, static_cast<lv_obj_t*>(lv_event_get_target(event)));
    }, LV_EVENT_CLICKED, reinterpret_cast<void*>(index));
}

void renderDirectory()
{
    if (!g_list || !g_path_label) return;
    lv_obj_clean(g_list);
    if (g_feedback) {
        lv_obj_del(g_feedback);
        g_feedback = nullptr;
    }
    g_selected_index = -1;
    g_selected_row = nullptr;
    setActionsEnabled(false);
    lv_label_set_text(g_path_label, g_current_path);

    if (!sigurdos_sdcard_mounted()) {
        lv_obj_t* label = lv_label_create(g_list);
        lv_label_set_text(label, "SD card is not mounted.\nReturn to System and retry.");
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_center(label);
        return;
    }

    bool truncated = false;
    if (!sigurdos_sdcard_list(
            g_current_path, g_entries, MAX_VISIBLE_ENTRIES,
            &g_entry_count, &truncated)) {
        g_entry_count = 0;
        lv_obj_t* label = lv_label_create(g_list);
        lv_label_set_text(label, "Unable to read directory");
        lv_obj_set_style_text_color(label, lv_color_hex(ACCENT_RED), 0);
        lv_obj_center(label);
        return;
    }

    if (g_entry_count == 0) {
        lv_obj_t* label = lv_label_create(g_list);
        lv_label_set_text(label, "(empty directory)");
        lv_obj_set_style_text_color(label, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_center(label);
    } else {
        for (size_t index = 0; index < g_entry_count; ++index) addEntryRow(index);
    }
    if (truncated) showFeedback("Showing first 32 entries", false);
}

lv_obj_t* addActionButton(lv_obj_t* parent, const char* text, int x, lv_event_cb_t callback)
{
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button, 86, 24);
    lv_obj_align(button, LV_ALIGN_CENTER, x, 0);
    apply_pixel_btn_outline(button);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    return button;
}

} // namespace

void file_browser_screen_show()
{
    std::snprintf(g_current_path, sizeof(g_current_path), "/");
    g_screen = make_screen_full("SD Files");

    lv_obj_t* path_bar = lv_obj_create(g_screen);
    lv_obj_set_size(path_bar, LV_PCT(100), 24);
    lv_obj_align(path_bar, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(path_bar, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(path_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(path_bar, 0, 0);
    lv_obj_set_style_radius(path_bar, 0, 0);
    lv_obj_set_style_pad_all(path_bar, 0, 0);

    lv_obj_t* up = lv_btn_create(path_bar);
    lv_obj_set_size(up, 34, 22);
    lv_obj_align(up, LV_ALIGN_LEFT_MID, 1, 0);
    apply_topbar_icon_btn(up);
    lv_obj_t* up_label = lv_label_create(up);
    lv_label_set_text(up_label, LV_SYMBOL_UP);
    lv_obj_center(up_label);
    lv_obj_add_event_cb(up, [](lv_event_t*) {
        char parent[SIGURDOS_SD_MAX_PATH_LEN + 1];
        if (file_browser::parentPath(g_current_path, parent, sizeof(parent))) {
            std::snprintf(g_current_path, sizeof(g_current_path), "%s", parent);
            renderDirectory();
        }
    }, LV_EVENT_CLICKED, nullptr);

    g_path_label = lv_label_create(path_bar);
    lv_obj_set_width(g_path_label, LV_HOR_RES - 44);
    lv_label_set_long_mode(g_path_label, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_text_font(g_path_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_text_color(g_path_label, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_align(g_path_label, LV_ALIGN_LEFT_MID, 40, 0);

    g_list = lv_list_create(g_screen);
    lv_obj_set_size(g_list, LV_PCT(100), CONTENT_H - 54);
    lv_obj_align(g_list, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 24);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_list, 0, 0);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* actions = lv_obj_create(g_screen);
    lv_obj_set_size(actions, LV_PCT(100), 30);
    lv_obj_align(actions, LV_ALIGN_TOP_MID, 0, CONTENT_Y + CONTENT_H - 30);
    lv_obj_set_style_bg_color(actions, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(actions, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_radius(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);

    g_open_button = addActionButton(actions, LV_SYMBOL_EYE_OPEN " Open", -96,
                                    [](lv_event_t*) { showPreview(); });
    g_copy_button = addActionButton(actions, LV_SYMBOL_COPY " Copy", 0,
                                    [](lv_event_t*) { copySelected(); });
    g_delete_button = addActionButton(actions, LV_SYMBOL_TRASH " Delete", 96,
                                      [](lv_event_t*) { requestDelete(); });

    lv_obj_add_event_cb(g_screen, [](lv_event_t*) {
        g_screen = nullptr;
        g_list = nullptr;
        g_path_label = nullptr;
        g_selected_row = nullptr;
        g_open_button = nullptr;
        g_copy_button = nullptr;
        g_delete_button = nullptr;
        g_feedback = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    renderDirectory();
    show_screen(g_screen);
}

} // namespace sigurdos::ui
