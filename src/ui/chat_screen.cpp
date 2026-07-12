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


#include "chat_screen.h"
#include "chat_message_buffer.h"
#include "channel_menu.h"
#include "navigation.h"
#include "screens.h"
#include "screens_common.h"
#include "theme.h"
#include "responsive.h"
#include "list_window.h"
#include "notifications.h"
#include "../hal/tdeck_pins.h"
#include "../hal/battery.h"
#include "../mesh/mesh_wrapper.h"
#include "../mesh/channel_validation.h"
#include "../mesh/public_channel.h"
#include "../mesh/message_store.h"
#include "../hal/prefs.h"
#include "chat_history_store.h"
#include "chat_store_migration.h"
#include "message_detail.h"
#include "../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstring>
#include <cstdio>
#include <esp_heap_caps.h>
#include "utils/utf8_util.h"

namespace sigurdos::ui {

using namespace theme;

static constexpr lv_obj_flag_t no_scroll_flags()
{
    return (lv_obj_flag_t)(
        LV_OBJ_FLAG_SCROLLABLE |
        LV_OBJ_FLAG_SCROLL_ELASTIC |
        LV_OBJ_FLAG_SCROLL_MOMENTUM |
        LV_OBJ_FLAG_SCROLL_CHAIN |
        LV_OBJ_FLAG_SCROLL_ON_FOCUS |
        LV_OBJ_FLAG_SCROLL_WITH_ARROW);
}

static void disable_scroll(lv_obj_t* obj)
{
    lv_obj_remove_flag(obj, no_scroll_flags());
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void stabilize_topbar_pill(lv_obj_t* obj)
{
    disable_scroll(obj);
    lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(obj, 0, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_EDITED));
}

// ── Chat screen widget pointers ────────────────────────────
static lv_obj_t* scr            = nullptr;
static lv_obj_t* top_bar        = nullptr;
static lv_obj_t* channel_ribbon = nullptr;
static lv_obj_t* msg_list       = nullptr;
static lv_obj_t* input_bar      = nullptr;
static lv_obj_t* input_field    = nullptr;
static lv_obj_t* byte_counter   = nullptr;
// Channel menu overlay (null when closed). While open, trackball
// events fall through to the LVGL group so its buttons stay navigable.
static lv_obj_t* channel_menu   = nullptr;

// ── Search state ───────────────────────────────────────
static bool     search_active        = false;
static char     search_query[32]     = "";
static int      search_matches[200]  = {0};
static int      search_match_count   = 0;
static int      search_current_match = -1;
static lv_obj_t* search_bar          = nullptr;
static lv_obj_t* search_input        = nullptr;

// Channel-list view widgets
static lv_obj_t* ch_list            = nullptr;
static lv_obj_t* ch_back_btn        = nullptr;
static lv_obj_t* ch_add_btn         = nullptr;
static int       ch_focus           = 0;   // 0=list, 1=back, 2=add
static int       ch_list_selected   = 0;

// ── Messaging-view layout ──────────────────────────────────
using responsive::TOP_BAR_H;
using responsive::BOT_BAR_H;
using responsive::DIVIDER_H;
using responsive::DISPLAY_H;
using responsive::DISPLAY_W;
using responsive::CONTENT_W;
using responsive::dialog_size;
static constexpr int TOP_H      = TOP_BAR_H;
static constexpr int INPUT_H    = 35;
// BOT_BAR_H, DIVIDER_H used directly from responsive namespace
static constexpr int BUBBLE_PAD = 6;
static constexpr uint16_t CHAT_MSGS_MAX         = CHAT_SCREEN_MESSAGE_CAP_MAX;
static constexpr uint16_t CHAT_MSGS_DEFAULT_CAP = CHAT_SCREEN_MESSAGE_CAP_DEFAULT;
static constexpr uint16_t CHAT_MSGS_MIN_CAP     = CHAT_SCREEN_MESSAGE_CAP_MIN;
static constexpr int CHAT_RENDER_WINDOW = CHAT_LIST_RENDER_CAPACITY;
static constexpr int MAX_MSG_BYTES = 149; // max text bytes for mesh payload (MAX_PAYLOAD - 1)
static constexpr int MAX_NAME_LEN  = 31;  // max chars for channel/contact names (buffer - null)
static constexpr int MSG_LIST_Y    = TOP_H + DIVIDER_H;
static constexpr int MSG_LIST_H = DISPLAY_H - TOP_H - DIVIDER_H - INPUT_H - DIVIDER_H - BOT_BAR_H;

// ── Channel-list layout (matches screens.cpp constants) ────
static constexpr int LIST_BAR_H  = 22;
static constexpr int LIST_DIV_H  = 1;
static constexpr int LIST_CONT_Y = LIST_BAR_H + LIST_DIV_H;   // 23
static constexpr int LIST_CONT_H = DISPLAY_H - LIST_CONT_Y - LIST_DIV_H - BOT_BAR_H; // 196
static constexpr int LIST_ROW_H  = 44;

// ── Channel state ──────────────────────────────────────────
static constexpr int MAX_CHANNELS = 16;
// Row width of the channel-name table: "DM: " (4) + contact name (31) + null
// = 36 → 37 for safety. Every buffer that mirrors a dyn_channels entry MUST use
// this constant — a stride mismatch silently corrupts the channel-state snapshot
// taken in refresh_channels() (see issue #686).
static constexpr int CHANNEL_NAME_CAP = 37;
static char  dyn_channels[MAX_CHANNELS][CHANNEL_NAME_CAP];
static int   dyn_count      = 0;
static bool  g_skip_channel_list = false;   // Set true to bypass show_channel_list in chat_screen_show
static int   active_channel = 0;
static int   chat_render_channel = -1;
static int   chat_render_offset = 0; // entries newer than the visible window
static bool  chat_render_scroll_bottom = true;
static lv_obj_t* chat_older_btn = nullptr;
static lv_obj_t* chat_newer_btn = nullptr;
static lv_obj_t* chat_no_results = nullptr;
static lv_obj_t* chat_bubble_pool[CHAT_RENDER_WINDOW] = {};
static ChatHistoryCheckpoint chat_checkpoint;

static void mark_chat_history_dirty()
{
    chat_checkpoint.markDirty(millis());
}
// ── Channel filter mode ────────────────────────────────────
// 0 = show all, 1 = channels only, 2 = DMs only
static int   chat_filter_mode = 0;

// ── Per-channel metadata ───────────────────────────────────
struct ChannelMeta {
    char     preview[64];
    uint32_t timestamp;
    int      unread;
};
static ChannelMeta ch_meta[MAX_CHANNELS];

// ChannelMessage and the per-channel buffer mechanics live in
// chat_message_buffer.h (issue #820) so they are testable off-target.
static ChatMessageBuffer ch_buffers[MAX_CHANNELS];


struct ChatPrivateScopeState {
    char conversation[32];
    char name[31];
    uint8_t key[16];
    bool has_scope;
};
static ChatPrivateScopeState ch_private_scopes[MAX_CHANNELS];

static ChatPrivateScopeState* find_chat_private_scope(const char* conversation, bool create)
{
    if (!conversation || !conversation[0]) return nullptr;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (ch_private_scopes[i].conversation[0] &&
            strcmp(ch_private_scopes[i].conversation, conversation) == 0) {
            return &ch_private_scopes[i];
        }
    }
    if (!create) return nullptr;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (!ch_private_scopes[i].conversation[0]) {
            strncpy(ch_private_scopes[i].conversation, conversation,
                    sizeof(ch_private_scopes[i].conversation) - 1);
            ch_private_scopes[i].conversation[sizeof(ch_private_scopes[i].conversation) - 1] = '\0';
            return &ch_private_scopes[i];
        }
    }
    return nullptr;
}

static const ChatPrivateScopeState* get_chat_private_scope(const char* conversation)
{
    ChatPrivateScopeState* scope = find_chat_private_scope(conversation, false);
    return (scope && scope->has_scope) ? scope : nullptr;
}

static bool set_chat_private_scope(const char* conversation, const char* name, const uint8_t key[16])
{
    if (!conversation || !conversation[0] || !name || !name[0] || !key) return false;
    ChatPrivateScopeState* scope = find_chat_private_scope(conversation, true);
    if (!scope) return false;
    strncpy(scope->name, name, sizeof(scope->name) - 1);
    scope->name[sizeof(scope->name) - 1] = '\0';
    memcpy(scope->key, key, sizeof(scope->key));
    scope->has_scope = true;
    return true;
}

static void clear_chat_private_scope(const char* conversation)
{
    ChatPrivateScopeState* scope = find_chat_private_scope(conversation, false);
    if (!scope) return;
    scope->conversation[0] = '\0';
    scope->name[0] = '\0';
    memset(scope->key, 0, sizeof(scope->key));
    scope->has_scope = false;
}

static void ensure_channel_buffer(int idx)
{
    if (idx < 0 || idx >= MAX_CHANNELS) return;
    ch_buffers[idx].ensure(CHAT_MSGS_MAX, CHAT_MSGS_MIN_CAP);
}

static bool has_channel_buffer(int idx)
{
    return idx >= 0 && idx < MAX_CHANNELS && ch_buffers[idx].allocated();
}

static uint16_t chat_msg_cap()
{
    return chat_screen_normalize_message_cap(sigurdos::prefs_get().chat_msg_cap);
}

static void trim_channel_history(int idx, uint16_t cap)
{
    if (idx < 0 || idx >= MAX_CHANNELS || cap == 0) {
        return;
    }
    // Keeps the legacy ensure side effect: callers gate on
    // has_channel_buffer() right after trimming.
    ensure_channel_buffer(idx);
    ch_buffers[idx].trim(cap);
}

// ── Forward declarations ───────────────────────────────────
static void show_channel_list();
static void open_channel_messaging(int idx);
static void rebuild_channel_ribbon();
static void show_add_channel_options(lv_obj_t* parent);
static void render_active_messages(bool scroll_to_bottom = true);
static void show_emoji_picker(lv_obj_t* parent);
static void show_search_bar();
static void hide_search();

static int channel_pill_width(const char* name)
{
    const size_t len = name ? strnlen(name, 31) : 0;
    int width = 20 + (int)len * 7;
    if (width < 48) width = 48;
    if (width > 112) width = 112;
    return width;
}

static lv_obj_t* create_channel_pill(lv_obj_t* parent, int idx)
{
    const bool selected = idx == active_channel;
    const int width = channel_pill_width(dyn_channels[idx]);

    lv_obj_t* pill = lv_obj_create(parent);
    lv_obj_set_size(pill, width, TOP_H - 8);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(pill, 0, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_set_style_shadow_width(pill, 0, 0);
    lv_obj_set_style_outline_width(pill, 0, 0);
    stabilize_topbar_pill(pill);

    const lv_color_t bg = lv_color_hex(selected ? ACCENT : BG_TERTIARY);
    lv_obj_set_style_bg_color(pill, bg, 0);
    lv_obj_set_style_bg_color(pill, bg, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(pill, bg, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_STATE_FOCUSED);

    lv_obj_t* label = lv_label_create(pill);
    lv_label_set_text(label, dyn_channels[idx]);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width - 8);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_text_color(label,
        selected ? lv_color_hex(0xffffff) : lv_color_hex(CHANNEL_HASH), 0);
    disable_scroll(label);
    lv_obj_center(label);

    lv_obj_add_event_cb(pill, [](lv_event_t* e) {
        int ch = (int)(intptr_t)lv_event_get_user_data(e);
        active_channel = ch;
        ch_meta[ch].unread = 0;
        rebuild_channel_ribbon();
        render_active_messages();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    return pill;
}

// ════════════════════════════════════════════════════
// Dynamic channels — pulled from mesh, sorted by MRU
// ════════════════════════════════════════════════════
static void refresh_channels()
{
    // Cache old channel state so we can clean up + rebuild without flicker
    // NOTE: static to avoid ~1.9KB stack allocation in LVGL event handler context
    static char old_names[MAX_CHANNELS][CHANNEL_NAME_CAP] = {{0}};
    static ChannelMeta old_meta[MAX_CHANNELS] = {};
    static ChatMessageBuffer old_bufs[MAX_CHANNELS];
    // The flat memcpy below relies on old_names having the same row stride as
    // dyn_channels; assert it so the two can never silently diverge again (#686).
    static_assert(sizeof(old_names) == sizeof(dyn_channels),
                  "old_names must mirror dyn_channels row stride");
    int old_count = dyn_count;
    char active_name[CHANNEL_NAME_CAP] = "";

    // Remember the name of the currently active channel so we can
    // find it again after remapping.
    if (active_channel >= 0 && active_channel < old_count) {
        strncpy(active_name, dyn_channels[active_channel], sizeof(active_name) - 1);
        active_name[sizeof(active_name) - 1] = '\0';
    }

    memcpy(old_names, dyn_channels, sizeof(old_names));
    memcpy(old_meta, ch_meta, sizeof(old_meta));
    for (int i = 0; i < MAX_CHANNELS; i++) {
        old_bufs[i].takeFrom(ch_buffers[i]);
        memset(&ch_meta[i], 0, sizeof(ch_meta[i]));
    }

    // ── Get fresh channel list from mesh ─────────────────
    dyn_count = sigurdos::mesh::exportChannels(dyn_channels, MAX_CHANNELS);
    if (dyn_count == 0) {
        if (sigurdos::mesh::joinPublicChannel()) {
            dyn_count = sigurdos::mesh::exportChannels(dyn_channels, MAX_CHANNELS);
        }
        if (dyn_count == 0) {
            strncpy(dyn_channels[0], "#general", sizeof(dyn_channels[0]) - 1);
            dyn_channels[0][sizeof(dyn_channels[0]) - 1] = '\0';
            dyn_count = 1;
        }
    }

    // ── Apply channel filter ─────────────────────────────
    if (chat_filter_mode == 1) {
        // Channels only: keep real group channels, including PSK-backed Public.
#if defined(SIGURDOS_DEBUG)
        Serial.printf("[chat] filter: channels only, before=%d\n", dyn_count);
#endif
        int keep = 0;
        for (int i = 0; i < dyn_count; i++) {
            if (chat_screen_filter_accepts_channel(chat_filter_mode, dyn_channels[i])) {
                if (keep < i) {
                    strncpy(dyn_channels[keep], dyn_channels[i], sizeof(dyn_channels[keep]) - 1);
                    dyn_channels[keep][sizeof(dyn_channels[keep]) - 1] = '\0';
                }
                keep++;
            }
        }
        dyn_count = keep;
#if defined(SIGURDOS_DEBUG)
        Serial.printf("[chat] filter: channels only, after=%d\n", dyn_count);
#endif
    } else if (chat_filter_mode == 2) {
        // DMs only: keep entries starting with "DM:"
#if defined(SIGURDOS_DEBUG)
        Serial.printf("[chat] filter: DMs only, before=%d\n", dyn_count);
#endif
        int keep = 0;
        for (int i = 0; i < dyn_count; i++) {
            if (chat_screen_filter_accepts_channel(chat_filter_mode, dyn_channels[i])) {
                if (keep < i) {
                    strncpy(dyn_channels[keep], dyn_channels[i], sizeof(dyn_channels[keep]) - 1);
                    dyn_channels[keep][sizeof(dyn_channels[keep]) - 1] = '\0';
                }
                keep++;
            }
        }
        dyn_count = keep;
#if defined(SIGURDOS_DEBUG)
        Serial.printf("[chat] filter: DMs only, after=%d\n", dyn_count);
#endif
    }
    // mode 0: no filter, show all

    // ── Fallback: if filter removed everything, add a default ─
    if (dyn_count == 0) {
        if (chat_filter_mode == 1) {
            // Channels filter: add a synthetic fallback if mesh is unavailable.
            strncpy(dyn_channels[0], "#general", sizeof(dyn_channels[0]) - 1);
            dyn_channels[0][sizeof(dyn_channels[0]) - 1] = '\0';
            dyn_count = 1;
        }
        // DMs filter with no DMs: leave empty (user can start one from Contacts)
    }

    // ── Remap: transfer message buffers by matching names ─
    // For each new channel from the mesh, look for a matching
    // old channel by name and carry its buffer + metadata forward.
    for (int new_idx = 0; new_idx < dyn_count; new_idx++) {
        for (int old_idx = 0; old_idx < old_count; old_idx++) {
            if (old_names[old_idx][0] != '\0' &&
                strcmp(dyn_channels[new_idx], old_names[old_idx]) == 0) {
                ch_buffers[new_idx].takeFrom(old_bufs[old_idx]);  // claimed
                ch_meta[new_idx] = old_meta[old_idx];
                old_names[old_idx][0] = '\0'; // mark consumed
                break;
            }
        }
    }

    // ── Restore DM entries ───────────────────────────────
    // DM conversations are synthetic entries (prefixed "DM:")
    // that aren't part of the mesh channel export. Re-append
    // any that existed before the refresh so they persist.
    // Skip when filtering to channels only (mode 1).
    if (chat_filter_mode != 1) {
        for (int old_idx = 0; old_idx < old_count; old_idx++) {
            if (old_names[old_idx][0] == '\0') continue;
            if (strncmp(old_names[old_idx], "DM:", 3) != 0) continue;
            if (dyn_count >= MAX_CHANNELS) {
                // No room — free the orphaned buffer
                old_bufs[old_idx].release();
                continue;
            }
            int new_idx = dyn_count++;
            strncpy(dyn_channels[new_idx], old_names[old_idx], sizeof(dyn_channels[new_idx]) - 1);
            dyn_channels[new_idx][sizeof(dyn_channels[new_idx]) - 1] = '\0';
            ch_buffers[new_idx].takeFrom(old_bufs[old_idx]);  // claimed
            ch_meta[new_idx] = old_meta[old_idx];
        }
    }

    // ── Free orphaned buffers ────────────────────────────
    // Channels that were in the old list but are no longer
    // present (gone from mesh, not a DM) have their memory
    // released here.
    for (int i = 0; i < old_count; i++) {
        old_bufs[i].release();
    }

    // ── Update active_channel by name, not by index ──────
    active_channel = 0;
    if (active_name[0]) {
        for (int i = 0; i < dyn_count; i++) {
            if (strcmp(dyn_channels[i], active_name) == 0) {
                active_channel = i;
                break;
            }
        }
    }
    if (active_channel >= dyn_count) active_channel = 0;
}

static void mark_channel_used(int idx)
{
    if (idx >= 0 && idx < dyn_count) active_channel = idx;
}

// ════════════════════════════════════════════════════
// Timestamp helper
// ════════════════════════════════════════════════════
static void format_time(char* buf, size_t sz, uint32_t epoch)
{
    if (epoch == 0) { snprintf(buf, sz, "--:--"); return; }
    uint32_t t = epoch % 86400;
    snprintf(buf, sz, "%02d:%02d", (t / 3600) % 24, (t / 60) % 60);
}

static void apply_ch_row_selection(lv_obj_t* row, bool selected)
{
    if (selected) {
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_border_width(row, 0, 0);
    }
}

static void clear_ch_row_selection()
{
    if (ch_list_selected >= 0 && ch_list_selected < dyn_count && ch_list) {
        lv_obj_t* row = lv_obj_get_child(ch_list, ch_list_selected);
        if (row) apply_ch_row_selection(row, false);
    }
}

static void clear_ch_focus_buttons()
{
    if (ch_back_btn) lv_obj_set_style_border_width(ch_back_btn, 1, 0);
    if (ch_add_btn) lv_obj_set_style_border_width(ch_add_btn, 0, 0);
}

// ── Forward declarations ──────────────────────────────
static void refresh_chat_list_view(lv_obj_t* scr);

static void populate_channel_rows(lv_obj_t* list) {
    for (int i = 0; i < dyn_count; i++) {
        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_PCT(100), LIST_ROW_H);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void*)(intptr_t)i);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(row, LV_DIR_NONE);
        apply_ch_row_selection(row, i == ch_list_selected);

        lv_obj_t* avatar = lv_obj_create(row);
        lv_obj_set_size(avatar, 32, 32);
        lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_set_style_bg_color(avatar, lv_color_hex(0x5865F2), 0);
        lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(avatar, 0, 0);
        lv_obj_set_style_border_width(avatar, 0, 0);
        lv_obj_clear_flag(avatar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_scrollbar_mode(avatar, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t* hash = lv_label_create(avatar);
        lv_label_set_text(hash, "#");
        lv_obj_set_style_text_color(hash, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(hash, emoji_wrapped_montserrat_12, 0);
        lv_obj_center(hash);

        lv_obj_t* name_lbl = lv_label_create(row);
        lv_label_set_text(name_lbl, dyn_channels[i]);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(name_lbl, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 46, 6);

        if (ch_meta[i].timestamp > 0) {
            char tbuf[8];
            format_time(tbuf, sizeof(tbuf), ch_meta[i].timestamp);
            lv_obj_t* ts = lv_label_create(row);
            lv_label_set_text(ts, tbuf);
            lv_obj_set_style_text_color(ts, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(ts, emoji_wrapped_montserrat_10, 0);
            // Account for delete button width (28px + gap) when visible
            bool has_del = dyn_count > 1;
            int ts_off = -4;
            if (ch_meta[i].unread > 0) ts_off -= 22;  // unread badge
            if (has_del)               ts_off -= 32;  // delete button (28px + gap)
            lv_obj_align(ts, LV_ALIGN_TOP_RIGHT, ts_off, 8);
        }

        lv_obj_t* prev = lv_label_create(row);
        lv_label_set_text(prev,
            ch_meta[i].preview[0] ? ch_meta[i].preview : "No messages yet");
        lv_obj_set_style_text_color(prev, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(prev, emoji_wrapped_montserrat_10, 0);
        lv_label_set_long_mode(prev, LV_LABEL_LONG_DOT);
        // Available width: start at x=46, leave room for right-side elements
        // Timestamp ~60px, unread badge ~24px, delete btn ~32px, padding ~6px
        int preview_w = CONTENT_W - 70;
        if (ch_meta[i].timestamp > 0) preview_w -= 60;
        if (ch_meta[i].unread > 0)    preview_w -= 24;
        if (dyn_count > 1)            preview_w -= 32;  // delete button (28px + gap)
        if (preview_w < 10) preview_w = 10;  // safe floor for narrow displays
        lv_obj_set_width(prev, preview_w);
        lv_obj_align(prev, LV_ALIGN_TOP_LEFT, 46, 26);

        if (ch_meta[i].unread > 0) {
            lv_obj_t* badge = lv_obj_create(row);
            lv_obj_set_size(badge, 18, 18);
            int badge_off = dyn_count > 1 ? -36 : -4;
            lv_obj_align(badge, LV_ALIGN_RIGHT_MID, badge_off, 0);
            lv_obj_set_style_bg_color(badge, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(badge, 0, 0);
            lv_obj_set_style_border_width(badge, 0, 0);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

            char cnt_buf[16];
            int cnt = ch_meta[i].unread;
            if (cnt > 9) snprintf(cnt_buf, sizeof(cnt_buf), "9+");
            else         snprintf(cnt_buf, sizeof(cnt_buf), "%d", cnt);
            lv_obj_t* cnt_lbl = lv_label_create(badge);
            lv_label_set_text(cnt_lbl, cnt_buf);
            lv_obj_set_style_text_color(cnt_lbl, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_text_font(cnt_lbl, emoji_wrapped_montserrat_10, 0);
            lv_obj_center(cnt_lbl);
        }

        int ch_idx = i;

        // Delete button (hidden when only 1 channel, or for synthetic DM entries)
        if (dyn_count > 1 &&
            !chat_screen_is_dm_name(dyn_channels[i]) &&
            !sigurdos::mesh::isPublicChannelName(dyn_channels[i])) {
            lv_obj_t* del_btn = lv_btn_create(row);
            lv_obj_set_size(del_btn, 28, 24);
            lv_obj_set_style_bg_color(del_btn, lv_color_hex(ACCENT_RED), 0);
            lv_obj_set_style_radius(del_btn, 0, 0);
            lv_obj_set_style_border_width(del_btn, 0, 0);
            lv_obj_set_style_pad_all(del_btn, 0, 0);
            lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, -4, 0);
            lv_obj_add_flag(del_btn, LV_OBJ_FLAG_CLICKABLE);
            auto* dl = lv_label_create(del_btn);
            lv_label_set_text(dl, LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_font(dl, emoji_wrapped_montserrat_10, 0);
            lv_obj_center(dl);
            lv_obj_add_event_cb(del_btn, [](lv_event_t* e) {
                int idx = (int)(intptr_t)lv_event_get_user_data(e);
                lv_obj_t* scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
                if (!scr) return;
                auto dlg_sz = dialog_size(220, 100);
                lv_obj_t* dlg = lv_obj_create(scr);
                lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
                lv_obj_center(dlg);
                lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
                lv_obj_set_style_radius(dlg, 0, 0);
                lv_obj_set_style_border_width(dlg, 0, 0);
                lv_obj_set_style_pad_all(dlg, 8, 0);

                lv_obj_t* title = lv_label_create(dlg);
                lv_label_set_text(title, "Delete channel?");
                lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
                lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
                lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

                lv_obj_t* msg = lv_label_create(dlg);
                char msg_buf[64];
                snprintf(msg_buf, sizeof(msg_buf), "Delete channel #%s?", dyn_channels[idx]);
                lv_label_set_text(msg, msg_buf);
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
                lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ce) {
                    lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
                }, LV_EVENT_CLICKED, nullptr);

                lv_obj_t* confirm_btn = lv_btn_create(dlg);
                lv_obj_set_size(confirm_btn, 64, 24);
                lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
                lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
                lv_obj_set_style_radius(confirm_btn, 0, 0);
                lv_obj_t* cfl_lb = lv_label_create(confirm_btn);
                lv_label_set_text(cfl_lb, "Delete");
                lv_obj_center(cfl_lb);
                lv_obj_add_event_cb(confirm_btn, [](lv_event_t* ce) {
                    int idx2 = (int)(intptr_t)lv_event_get_user_data(ce);
                    sigurdos::mesh::removeChannel(idx2);
                    lv_obj_t* s2 = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(ce));
                    if (s2) refresh_chat_list_view(s2);
                    lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
                }, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
            }, LV_EVENT_CLICKED, (void*)(intptr_t)ch_idx);
        }

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            ch_meta[idx].unread = 0;
            open_channel_messaging(idx);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)ch_idx);
    }
}

static int find_channel_idx(const char* channel)
{
    if (!channel || !channel[0]) return -1;  // reject empty/null — don't silently route to active channel
    for (int i = 0; i < dyn_count; i++) {
        if (strcmp(dyn_channels[i], channel) == 0) return i;
    }
    return -1;
}

static void update_channel_meta(int idx, const char* text, uint32_t timestamp)
{
    if (idx < 0 || idx >= MAX_CHANNELS) return;
    // Truncate preview to fit the display: ~25 chars + "..." works in all row layouts
    const char* src = text ? text : "";
    size_t slen = strlen(src);
    constexpr size_t MAX_PREVIEW_CHARS = 25;
    if (slen > MAX_PREVIEW_CHARS) {
        size_t trunc = sigurdos::utf8_truncate_bytes(src, MAX_PREVIEW_CHARS);
        memcpy(ch_meta[idx].preview, src, trunc);
        memcpy(ch_meta[idx].preview + trunc, "...", 4);
    } else {
        memcpy(ch_meta[idx].preview, src, slen + 1);
    }
    ch_meta[idx].timestamp = timestamp;
}

static void append_channel_message(int idx, const char* sender, const char* text,
                                   uint32_t timestamp, bool is_self,
                                   uint32_t store_id = 0)
{
    if (idx < 0 || idx >= MAX_CHANNELS) return;
    ChannelMessage* msg = ch_buffers[idx].append(sender, text, timestamp, is_self,
                                                 chat_msg_cap(), CHAT_MSGS_MAX,
                                                 CHAT_MSGS_MIN_CAP, store_id);
    if (!msg) return;
    update_channel_meta(idx, msg->text, timestamp);
}

static bool loaded_message_exists(int idx, const char* sender, const char* text,
                                  uint32_t timestamp, bool is_self)
{
    if (idx < 0 || idx >= MAX_CHANNELS || !has_channel_buffer(idx)) return false;
    const char* safe_sender = sender ? sender : "";
    const char* safe_text = text ? text : "";
    for (uint16_t i = 0; i < ch_buffers[idx].count(); i++) {
        const ChannelMessage& msg = ch_buffers[idx].at(i);
        uint32_t delta = msg.timestamp > timestamp
            ? msg.timestamp - timestamp
            : timestamp - msg.timestamp;
        if (msg.is_self == is_self && delta <= 2 &&
            strcmp(msg.sender, safe_sender) == 0 &&
            strcmp(msg.text, safe_text) == 0) {
            return true;
        }
    }
    return false;
}

static bool append_loaded_channel_message(int idx, const char* sender, const char* text,
                                          uint32_t timestamp, bool is_self, bool acked,
                                          bool confirmation_lost)
                                          uint32_t store_id)