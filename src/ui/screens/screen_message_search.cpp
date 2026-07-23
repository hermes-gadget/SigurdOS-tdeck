// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
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
#include "../chat_screen.h"
#include "../theme.h"
#include "../responsive.h"
#include "../navigation.h"
#include "../message_search.h"
#include "../../mesh/message_store.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#if defined(ESP32_PLATFORM) || defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ════════════════════════════════════════════════════════
// Message Search — filter message history across all chats
// ════════════════════════════════════════════════════════

// Cap the number of rendered result rows so a broad query cannot spawn an
// unbounded pile of orphan LVGL widgets (see the Code Audit Checklist).
static constexpr int MS_MAX_RESULTS = 40;
static constexpr int MS_SNIPPET_LEAD = 10;  // context bytes before a text match

static lv_obj_t* g_ms_root = nullptr;
static uint32_t g_ms_generation = 0;
static lv_obj_t* g_ms_input = nullptr;
static lv_obj_t* g_ms_results = nullptr;
static lv_obj_t* g_ms_status = nullptr;

// Scratch buffer for the full history snapshot, allocated in PSRAM on the
// device (it is large — hundreds of records) and freed on screen delete.
static sigurdos::mesh::StoredMessage* g_ms_scratch = nullptr;
static int g_ms_scratch_count = -1;

// Conversation name for each rendered row, keyed by the row's user_data index,
// so a tap can reopen the originating chat without holding a pointer into the
// scratch buffer.
static char g_ms_result_conv[MS_MAX_RESULTS][sigurdos::mesh::SIGURDOS_MSG_CONVERSATION_LEN];

static void ms_free_scratch()
{
    if (!g_ms_scratch) return;
#if defined(ESP32_PLATFORM) || defined(ARDUINO)
    heap_caps_free(g_ms_scratch);
#else
    free(g_ms_scratch);
#endif
    g_ms_scratch = nullptr;
    g_ms_scratch_count = -1;
}

static bool ms_ensure_scratch()
{
    if (g_ms_scratch) return true;
    const size_t bytes =
        sizeof(sigurdos::mesh::StoredMessage) * sigurdos::mesh::MESSAGE_STORE_MAX_RECORDS;
#if defined(ESP32_PLATFORM) || defined(ARDUINO)
    g_ms_scratch = static_cast<sigurdos::mesh::StoredMessage*>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_ms_scratch) {
        // Fall back to internal DRAM if PSRAM is exhausted.
        g_ms_scratch = static_cast<sigurdos::mesh::StoredMessage*>(malloc(bytes));
    }
#else
    g_ms_scratch = static_cast<sigurdos::mesh::StoredMessage*>(malloc(bytes));
#endif
    return g_ms_scratch != nullptr;
}

// Reopen the chat the tapped result belongs to.
static void ms_result_clicked(lv_event_t* e)
{
    const int idx = static_cast<int>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (idx < 0 || idx >= MS_MAX_RESULTS) return;
    const char* conv = g_ms_result_conv[idx];
    if (!conv[0]) return;

    chat_screen_open_conversation(conv);
}

// Append one result row: a context line (conversation + sender + time) and a
// snippet line with the matched substring highlighted in the accent colour.
static void ms_add_result_row(int row_idx,
                              const sigurdos::mesh::StoredMessage& msg,
                              const char* query)
{
    lv_obj_t* row = lv_obj_create(g_ms_results);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row,
        lv_color_hex(row_idx % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 6, 0);
    lv_obj_set_style_pad_ver(row, 4, 0);
    lv_obj_set_style_pad_row(row, 2, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    // Remember which conversation this row opens on tap.
    std::snprintf(g_ms_result_conv[row_idx], sizeof(g_ms_result_conv[row_idx]),
                  "%s", msg.conversation);
    lv_obj_add_event_cb(row, ms_result_clicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(row_idx)));

    // ── Context line: #channel / DM · sender · HH:MM ──
    char ctx[96];
    char tbuf[8];
    if (msg.timestamp == 0) {
        std::snprintf(tbuf, sizeof(tbuf), "--:--");
    } else {
        const uint32_t sec = msg.timestamp % 86400;
        std::snprintf(tbuf, sizeof(tbuf), "%02u:%02u",
                      (sec / 3600) % 24, (sec / 60) % 60);
    }
    const char* conv = msg.conversation[0] ? msg.conversation : "(unknown)";
    const char* sender = msg.is_self ? "You"
                       : (msg.sender[0] ? msg.sender : "");
    if (sender[0]) {
        std::snprintf(ctx, sizeof(ctx), "%s  \xC2\xB7  %s  \xC2\xB7  %s",
                      conv, sender, tbuf);
    } else {
        std::snprintf(ctx, sizeof(ctx), "%s  \xC2\xB7  %s", conv, tbuf);
    }
    lv_obj_t* ctx_lbl = lv_label_create(row);
    lv_label_set_text(ctx_lbl, ctx);
    lv_label_set_long_mode(ctx_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ctx_lbl, LV_PCT(100));
    lv_obj_set_style_text_color(ctx_lbl, lv_color_hex(CHANNEL_HASH), 0);
    lv_obj_set_style_text_font(ctx_lbl, emoji_wrapped_montserrat_10, 0);

    // ── Snippet line: before / [match] / after ──
    int win = 0, match_off = 0, match_len = 0;
    const bool text_hit =
        message_search_snippet_window(msg.text, query, MS_SNIPPET_LEAD,
                                      &win, &match_off, &match_len);

    lv_obj_t* snip = lv_obj_create(row);
    lv_obj_set_width(snip, LV_PCT(100));
    lv_obj_set_height(snip, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(snip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(snip, 0, 0);
    lv_obj_set_style_pad_all(snip, 0, 0);
    lv_obj_set_style_pad_column(snip, 0, 0);
    lv_obj_set_scrollbar_mode(snip, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(snip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(snip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(snip, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto make_part = [&](const char* s, uint32_t color, bool grow) {
        lv_obj_t* l = lv_label_create(snip);
        lv_label_set_text(l, s);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
        lv_obj_set_style_text_font(l, emoji_wrapped_montserrat_12, 0);
        if (grow) lv_obj_set_flex_grow(l, 1);
        else lv_obj_set_style_max_width(l, LV_PCT(80), 0);
        return l;
    };

    if (text_hit) {
        char before[80];
        char matched[48];
        int blen = match_off;
        if (blen > (int)sizeof(before) - 1) blen = sizeof(before) - 1;
        std::memcpy(before, msg.text + win, blen);
        before[blen] = '\0';

        int mlen = match_len;
        if (mlen > (int)sizeof(matched) - 1) mlen = sizeof(matched) - 1;
        std::memcpy(matched, msg.text + win + match_off, mlen);
        matched[mlen] = '\0';

        const char* after = msg.text + win + match_off + match_len;

        make_part(before, TEXT_PRIMARY, false);
        lv_obj_t* mlbl = make_part(matched, ACCENT_FOREGROUND, false);
        lv_obj_set_style_bg_color(mlbl, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_bg_opa(mlbl, LV_OPA_COVER, 0);
        make_part(after, TEXT_PRIMARY, true);
    } else {
        // Match was in the sender/conversation, not the body — show plain text.
        make_part(msg.text[0] ? msg.text : "(no text)", TEXT_SECONDARY, true);
    }
}

static void ms_run_search(const char* query)
{
    if (!g_ms_results || !g_ms_status) return;
    lv_obj_clean(g_ms_results);

    if (!query || !query[0]) {
        lv_label_set_text(g_ms_status, "Type to search your messages");
        return;
    }
    if (!ms_ensure_scratch()) {
        lv_label_set_text(g_ms_status, "Out of memory");
        return;
    }

    if (g_ms_scratch_count < 0) {
        g_ms_scratch_count = sigurdos::mesh::messageStoreLoadAll(
            g_ms_scratch,
            static_cast<int>(sigurdos::mesh::MESSAGE_STORE_MAX_RECORDS));
    }

    int shown = 0;
    int total = 0;
    // Iterate newest-first so the most recent matches lead the list.
    for (int i = g_ms_scratch_count - 1; i >= 0; i--) {
        const sigurdos::mesh::StoredMessage& m = g_ms_scratch[i];
        if (!message_search_matches(m.text, query)) {
            continue;
        }
        total++;
        if (shown < MS_MAX_RESULTS) {
            ms_add_result_row(shown, m, query);
            shown++;
        }
    }

    char status[64];
    if (total == 0) {
        std::snprintf(status, sizeof(status), "No matches");
    } else if (total > shown) {
        std::snprintf(status, sizeof(status), "%d matches (showing %d)",
                      total, shown);
    } else {
        std::snprintf(status, sizeof(status), "%d match%s",
                      total, total == 1 ? "" : "es");
    }
    lv_label_set_text(g_ms_status, status);
}

static void ms_input_changed(lv_event_t* e)
{
    lv_obj_t* ta = static_cast<lv_obj_t*>(lv_event_get_target(e));
    ms_run_search(lv_textarea_get_text(ta));
}

void message_search_screen_show()
{
    lv_obj_t* scr = make_screen_full("Search");
    g_ms_root = scr;
    const uint32_t generation = ++g_ms_generation;

    // Search input directly under the top bar.
    lv_obj_t* input = lv_textarea_create(scr);
    g_ms_input = input;
    lv_obj_set_size(input, CONTENT_W - 12, 26);
    lv_obj_align(input, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 2);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, 30);
    lv_textarea_set_placeholder_text(input, "Search messages...");
    apply_pixel_input(input);
    lv_obj_set_style_text_font(input, emoji_wrapped_montserrat_12, 0);
    lv_obj_add_event_cb(input, ms_input_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    // Status / result-count line.
    lv_obj_t* status = lv_label_create(scr);
    g_ms_status = status;
    lv_label_set_text(status, "Type to search your messages");
    lv_obj_set_style_text_color(status, lv_color_hex(TEXT_MUTED), 0);
    lv_obj_set_style_text_font(status, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 8, CONTENT_Y + 32);

    // Scrollable results list.
    lv_obj_t* list = lv_obj_create(scr);
    g_ms_results = list;
    lv_obj_set_size(list, CONTENT_W, CONTENT_H - 48);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 46);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    // Focus the input so the hardware keyboard types straight into the query.
    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, input);
        lv_group_focus_obj(input);
    }

    // Free the scratch buffer and null globals when the screen is destroyed.
    lv_obj_add_event_cb(scr, [](lv_event_t* e) {
        lv_obj_t* deleted = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const uint32_t deleted_generation = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
        if (deleted != g_ms_root || deleted_generation != g_ms_generation) return;
        g_ms_root = nullptr;
        g_ms_input = g_ms_results = g_ms_status = nullptr;
        ms_free_scratch();
    }, LV_EVENT_DELETE,
       reinterpret_cast<void*>(static_cast<uintptr_t>(generation)));

    show_screen(scr);
}

} // namespace sigurdos::ui
