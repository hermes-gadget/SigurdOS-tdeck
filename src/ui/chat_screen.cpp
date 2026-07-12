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
#include "../hal/tdeck_pins.h"
#include "../hal/battery.h"
#include "../mesh/mesh_wrapper.h"
#include "../mesh/channel_validation.h"
#include "../mesh/public_channel.h"
#include "../mesh/message_store.h"
#include "../hal/prefs.h"
#include "chat_history_store.h"
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
static void render_active_messages();
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
                                   uint32_t timestamp, bool is_self)
{
    if (idx < 0 || idx >= MAX_CHANNELS) return;
    ChannelMessage* msg = ch_buffers[idx].append(sender, text, timestamp, is_self,
                                                 chat_msg_cap(), CHAT_MSGS_MAX,
                                                 CHAT_MSGS_MIN_CAP);
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
                                          uint32_t timestamp, bool is_self, bool acked)
{
    if (idx < 0 || idx >= MAX_CHANNELS) return false;
    ensure_channel_buffer(idx);
    if (loaded_message_exists(idx, sender, text, timestamp, is_self)) {
        if (acked && has_channel_buffer(idx)) {
            for (uint16_t i = 0; i < ch_buffers[idx].count(); i++) {
                ChannelMessage& msg = ch_buffers[idx].at(i);
                uint32_t delta = msg.timestamp > timestamp
                    ? msg.timestamp - timestamp
                    : timestamp - msg.timestamp;
                if (msg.is_self == is_self && delta <= 2 &&
                    strcmp(msg.sender, sender ? sender : "") == 0 &&
                    strcmp(msg.text, text ? text : "") == 0) {
                    msg.acked = true;
                }
            }
        }
        return false;
    }

    append_channel_message(idx, sender, text, timestamp, is_self);
    mark_chat_history_dirty();
    if (acked && has_channel_buffer(idx) && ch_buffers[idx].count() > 0) {
        ch_buffers[idx].markLastAcked();
    }
    return true;
}

static int ensure_loaded_conversation(const char* conversation)
{
    if (!conversation || !conversation[0]) return -1;
    int idx = find_channel_idx(conversation);
    if (idx < 0 && dyn_count < MAX_CHANNELS) {
        idx = dyn_count;
        strncpy(dyn_channels[idx], conversation, sizeof(dyn_channels[idx]) - 1);
        dyn_channels[idx][sizeof(dyn_channels[idx]) - 1] = 0;
        dyn_count++;
    }
    return idx;
}

static void chat_load_companion_messages()
{
    // Scratch buffer for the persisted-message snapshot. Allocate from PSRAM
    // (with internal-DRAM fallback) rather than a static array — at ~15 KB it
    // would otherwise overflow the tight internal dram0_0_seg .bss region.
    constexpr int kRecentCap = 64;
    const size_t bytes = sizeof(sigurdos::mesh::StoredMessage) * kRecentCap;
    sigurdos::mesh::StoredMessage* recent =
        (sigurdos::mesh::StoredMessage*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!recent) {
        recent = (sigurdos::mesh::StoredMessage*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!recent) return;

    int n = sigurdos::mesh::messageStoreLoadRecent(nullptr, recent, kRecentCap);
    for (int i = 0; i < n; i++) {
        const sigurdos::mesh::StoredMessage& msg = recent[i];
        int idx = ensure_loaded_conversation(msg.conversation);
        if (idx < 0 || idx >= MAX_CHANNELS) continue;

        const char* text = msg.text;
        char prefix[40];
        if (msg.is_channel && !msg.is_self && msg.sender[0]) {
            snprintf(prefix, sizeof(prefix), "%s: ", msg.sender);
            size_t plen = strnlen(prefix, sizeof(prefix));
            if (strncmp(msg.text, prefix, plen) == 0) {
                text = msg.text + plen;
            }
        }
        append_loaded_channel_message(idx, msg.sender, text, msg.timestamp,
                                      msg.is_self, msg.acked);
    }
    heap_caps_free(recent);
}

static lv_obj_t* make_chat_list_screen()
{
    lv_obj_t* s = lv_obj_create(nullptr);
    apply_dark_bg(s);

    // Top bar
    lv_obj_t* top = lv_obj_create(s);
    lv_obj_set_size(top, LV_PCT(100), LIST_BAR_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);

    ch_back_btn = lv_btn_create(top);
    lv_obj_set_size(ch_back_btn, 24, LIST_BAR_H - 4);
    lv_obj_align(ch_back_btn, LV_ALIGN_LEFT_MID, 2, 0);
    apply_topbar_icon_btn(ch_back_btn);
    if (can_go_back()) {
        lv_obj_add_event_cb(ch_back_btn, [](lv_event_t*) { go_back(); }, LV_EVENT_CLICKED, nullptr);
    }

    lv_obj_t* back_icon = lv_label_create(ch_back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon,
        lv_color_hex(can_go_back() ? ACCENT : TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(back_icon, emoji_wrapped_montserrat_12, 0);
    lv_obj_center(back_icon);

    // Chat title (centered, replaces old channel hashtag snapshot)
    {
        lv_obj_t* ttl = lv_label_create(top);
        lv_label_set_text(ttl, "Chat");
        lv_obj_set_style_text_color(ttl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(ttl, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(ttl, LV_ALIGN_CENTER, 0, 0);
    }

    // Time snapshot
    {
        uint32_t epoch = sigurdos::mesh::getCurrentTime();
        char t[8];
        if (epoch == 0) snprintf(t, sizeof(t), "--:--");
        else {
            uint32_t sec = epoch % 86400;
            snprintf(t, sizeof(t), "%02d:%02d", (sec/3600)%24, (sec/60)%60);
        }
        lv_obj_t* tl = lv_label_create(top);
        lv_label_set_text(tl, t);
        lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(tl, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Top divider
    lv_obj_t* tdiv = lv_obj_create(s);
    lv_obj_set_size(tdiv, LV_PCT(100), LIST_DIV_H);
    lv_obj_align(tdiv, LV_ALIGN_TOP_MID, 0, LIST_BAR_H);
    lv_obj_set_style_bg_color(tdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(tdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tdiv, 0, 0);

    // Bottom bar
    lv_obj_t* bot = lv_obj_create(s);
    lv_obj_set_size(bot, LV_PCT(100), BOT_BAR_H);
    lv_obj_align(bot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(bot, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bot, 0, 0);
    lv_obj_set_style_border_width(bot, 0, 0);

    lv_obj_t* dev = lv_label_create(bot);
    lv_label_set_text(dev, sigurdos::mesh::getOwnName());
    lv_obj_set_style_text_color(dev, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dev, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(dev, LV_ALIGN_LEFT_MID, 4, 0);

    {
        char batt[8];
        int pct = sigurdos_battery_pct();
        snprintf(batt, sizeof(batt), "%d%%", pct);
        lv_obj_t* bl = lv_label_create(bot);
        lv_label_set_text(bl, batt);
        lv_obj_set_style_text_color(bl, lv_color_hex(pct > 20 ? ACCENT : ACCENT_RED), 0);
        lv_obj_set_style_text_font(bl, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(bl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Bottom divider
    lv_obj_t* bdiv = lv_obj_create(s);
    lv_obj_set_size(bdiv, LV_PCT(100), LIST_DIV_H);
    lv_obj_align(bdiv, LV_ALIGN_TOP_MID, 0, DISPLAY_H - BOT_BAR_H - LIST_DIV_H);
    lv_obj_set_style_bg_color(bdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(bdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bdiv, 0, 0);

    return s;
}

// ════════════════════════════════════════════════════
// Channel-list view
// ════════════════════════════════════════════════════
static void show_channel_list()
{
    // Null messaging-view pointers — they're invalid once we leave
    scr = top_bar = channel_ribbon = msg_list = input_bar = input_field = nullptr;
    ch_list = ch_back_btn = ch_add_btn = nullptr;
    ch_focus = 0;

    refresh_channels();
    ch_list_selected = 0;

    lv_obj_t* s = make_chat_list_screen();

    ch_list = lv_obj_create(s);
    lv_obj_set_size(ch_list, LV_PCT(100), LIST_CONT_H - 32);
    lv_obj_align(ch_list, LV_ALIGN_TOP_MID, 0, LIST_CONT_Y);
    lv_obj_set_user_data(ch_list, (void*)0xCA7C);
    lv_obj_set_style_bg_opa(ch_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ch_list, 0, 0);
    lv_obj_set_style_pad_all(ch_list, 0, 0);
    lv_obj_set_flex_flow(ch_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ch_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ch_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(ch_list, (lv_obj_flag_t)(
        LV_OBJ_FLAG_SCROLL_ELASTIC |
        LV_OBJ_FLAG_SCROLL_MOMENTUM |
        LV_OBJ_FLAG_SCROLL_CHAIN));

    populate_channel_rows(ch_list);
#if defined(SIGURDOS_DEBUG)
    Serial.printf("[chat] populate: dyn_count=%d names=[", dyn_count);
    for (int i = 0; i < dyn_count; i++) {
        Serial.printf("%s%s", i > 0 ? "," : "", dyn_channels[i]);
    }
    Serial.println("]");
#endif

    ch_add_btn = lv_btn_create(s);
    lv_obj_set_size(ch_add_btn, CONTENT_W > 200 ? 180 : CONTENT_W - 20, 28);
    lv_obj_align(ch_add_btn, LV_ALIGN_TOP_MID, 0, LIST_CONT_Y + LIST_CONT_H - 32);
    lv_obj_set_style_bg_color(ch_add_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(ch_add_btn, 0, 0);
    lv_obj_set_style_radius(ch_add_btn, 0, 0);
    lv_obj_t* al = lv_label_create(ch_add_btn);
    lv_label_set_text(al, LV_SYMBOL_PLUS " Add # Channel");
    lv_obj_set_style_text_font(al, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(al);
    lv_obj_add_event_cb(ch_add_btn, [](lv_event_t* e) {
        lv_obj_t* scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        show_add_channel_options(scr);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* old_scr = lv_screen_active();
    lv_scr_load_anim(s, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    if (old_scr && old_scr != s) lv_obj_del_async(old_scr);
}

// ════════════════════════════════════════════════════
// Messaging view — channel ribbon rebuild
// ════════════════════════════════════════════════════
static void rebuild_channel_ribbon()
{
    if (!channel_ribbon) return;
    lv_obj_set_style_bg_color(channel_ribbon, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(channel_ribbon, LV_OPA_COVER, 0);
    lv_obj_clean(channel_ribbon);

    for (int i = 0; i < dyn_count; i++) {
        create_channel_pill(channel_ribbon, i);
    }

    lv_obj_invalidate(channel_ribbon);
    if (top_bar) lv_obj_invalidate(top_bar);
}

// ── Search helpers ────────────────────────────────────
static void run_search(const char* query)
{
    search_match_count = 0;
    search_current_match = -1;
    if (!query || !query[0] || !has_channel_buffer(active_channel)) return;

    int n = ch_buffers[active_channel].count();
    // Lowercase query for case-insensitive matching
    char q_lower[32];
    size_t qlen = 0;
    for (; query[qlen] && qlen < sizeof(q_lower) - 1; qlen++)
        q_lower[qlen] = (query[qlen] >= 'A' && query[qlen] <= 'Z') ?
                        (query[qlen] + 32) : query[qlen];
    q_lower[qlen] = '\0';

    for (int i = 0; i < n && search_match_count < 200; i++) {
        bool found = false;
        // Search message text
        const char* text = ch_buffers[active_channel].at(i).text;
        if (text[0]) {
            for (const char* p = text; *p; p++) {
                bool match = true;
                for (size_t j = 0; j < qlen; j++) {
                    char c = p[j];
                    if (c == '\0') { match = false; break; }
                    if (c >= 'A' && c <= 'Z') c += 32;
                    if (c != q_lower[j]) { match = false; break; }
                }
                if (match) { found = true; break; }
            }
        }
        // Also search sender name
        if (!found) {
            const char* sender = ch_buffers[active_channel].at(i).sender;
            if (sender[0]) {
                for (const char* p = sender; *p; p++) {
                    bool match = true;
                    for (size_t j = 0; j < qlen; j++) {
                        char c = p[j];
                        if (c == '\0') { match = false; break; }
                        if (c >= 'A' && c <= 'Z') c += 32;
                        if (c != q_lower[j]) { match = false; break; }
                    }
                    if (match) { found = true; break; }
                }
            }
        }
        if (found) {
            search_matches[search_match_count++] = i;
        }
    }
    if (search_match_count > 0) search_current_match = 0;
}

static void show_search_bar()
{
    if (!scr) return;
    search_active = true;
    search_query[0] = '\0';

    if (!search_bar) {
        // Create search bar — positioned below top bar divider
        search_bar = lv_obj_create(scr);
        lv_obj_set_size(search_bar, LV_PCT(100), 30);
        lv_obj_align(search_bar, LV_ALIGN_TOP_MID, 0, TOP_H + DIVIDER_H);
        lv_obj_set_style_bg_color(search_bar, lv_color_hex(BG_SECONDARY), 0);
        lv_obj_set_style_bg_opa(search_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(search_bar, 0, 0);
        lv_obj_set_style_pad_all(search_bar, 3, 0);
        lv_obj_set_scrollbar_mode(search_bar, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(search_bar, LV_DIR_NONE);

        // Search icon label
        lv_obj_t* icon = lv_label_create(search_bar);
        lv_label_set_text(icon, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(icon, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(icon, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 6, 0);

        // Search text input
        search_input = lv_textarea_create(search_bar);
        lv_obj_set_size(search_input, 180, 24);
        lv_obj_align(search_input, LV_ALIGN_LEFT_MID, 24, 0);
        lv_obj_set_style_bg_color(search_input, lv_color_hex(BG_INPUT), 0);
        lv_obj_set_style_text_color(search_input, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(search_input, emoji_wrapped_montserrat_10, 0);
        lv_obj_set_style_border_width(search_input, 1, 0);
        lv_obj_set_style_border_color(search_input, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_radius(search_input, 0, 0);
        lv_obj_set_style_pad_all(search_input, 3, 0);
        lv_textarea_set_one_line(search_input, true);
        lv_textarea_set_max_length(search_input, 30);
        lv_textarea_set_placeholder_text(search_input, "Search messages...");
        lv_obj_remove_flag(search_input, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_style_outline_width(search_input, 0, LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(search_input, 0, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_EDITED));

        // Close button (×)
        lv_obj_t* close_btn = lv_btn_create(search_bar);
        lv_obj_set_size(close_btn, 24, 24);
        lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_border_width(close_btn, 0, 0);
        lv_obj_set_style_radius(close_btn, 0, 0);
        lv_obj_t* close_lbl = lv_label_create(close_btn);
        lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_font(close_lbl, emoji_wrapped_montserrat_10, 0);
        lv_obj_center(close_lbl);
        lv_obj_add_event_cb(close_btn, [](lv_event_t*) { hide_search(); }, LV_EVENT_CLICKED, nullptr);

        // On value change — re-run search
        lv_obj_add_event_cb(search_input, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
            const char* text = lv_textarea_get_text(search_input);
            strncpy(search_query, text ? text : "", sizeof(search_query) - 1);
            search_query[sizeof(search_query) - 1] = '\0';
            run_search(search_query);
            render_active_messages();
        }, LV_EVENT_ALL, nullptr);

        // On Enter (LV_EVENT_READY) — also re-run search
        lv_obj_add_event_cb(search_input, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_READY) return;
            const char* text = lv_textarea_get_text(search_input);
            strncpy(search_query, text ? text : "", sizeof(search_query) - 1);
            search_query[sizeof(search_query) - 1] = '\0';
            run_search(search_query);
            render_active_messages();
        }, LV_EVENT_ALL, nullptr);
    }

    lv_obj_remove_flag(search_bar, LV_OBJ_FLAG_HIDDEN);
    if (search_input) {
        lv_textarea_set_text(search_input, "");
    }
    // Resize message list to accommodate search bar overlay
    if (msg_list) {
        lv_obj_set_y(msg_list, TOP_H + DIVIDER_H + 30);
        lv_obj_set_height(msg_list, MSG_LIST_H - 31);
    }
    lv_group_t* g = lv_group_get_default();
    if (g && search_input) lv_group_focus_obj(search_input);
    run_search("");
    render_active_messages();
}

static void hide_search()
{
    search_active = false;
    search_query[0] = '\0';
    search_match_count = 0;
    search_current_match = -1;
    if (search_bar) {
        lv_obj_add_flag(search_bar, LV_OBJ_FLAG_HIDDEN);
    }
    // Restore message list position
    if (msg_list) {
        lv_obj_set_y(msg_list, MSG_LIST_Y);
        lv_obj_set_height(msg_list, MSG_LIST_H);
    }
    render_active_messages();
}

// ════════════════════════════════════════════════════
// Messaging view — top bar (← back + channel pills)
// ════════════════════════════════════════════════════
static void create_top_bar()
{
    top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, LV_PCT(100), TOP_H);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    disable_scroll(top_bar);

    // ← back button → return to channel list
    lv_obj_t* back = lv_btn_create(top_bar);
    lv_obj_set_size(back, 24, TOP_H - 4);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
    apply_topbar_icon_btn(back);
    lv_obj_t* bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(bl, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(bl, emoji_wrapped_montserrat_12, 0);
    lv_obj_center(bl);
    disable_scroll(bl);
    lv_obj_add_event_cb(back, [](lv_event_t*) {
        show_channel_list();
    }, LV_EVENT_CLICKED, nullptr);

    // Horizontal scrollable channel ribbon — exact width for no warp (matches home grid uniform sizing)
    int ribbon_w = CONTENT_W - 28 - 44 - 28; // back button + margins + time + search btn
    channel_ribbon = lv_obj_create(top_bar);
    lv_obj_set_size(channel_ribbon, ribbon_w, TOP_H - 4);
    lv_obj_align(channel_ribbon, LV_ALIGN_LEFT_MID, 28, 0);
    lv_obj_set_style_bg_color(channel_ribbon, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(channel_ribbon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(channel_ribbon, 0, 0);
    lv_obj_set_style_pad_all(channel_ribbon, 0, 0);
    lv_obj_set_flex_flow(channel_ribbon, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(channel_ribbon, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(channel_ribbon, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(channel_ribbon, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(channel_ribbon, (lv_obj_flag_t)(
        LV_OBJ_FLAG_SCROLL_ELASTIC |
        LV_OBJ_FLAG_SCROLL_MOMENTUM |
        LV_OBJ_FLAG_SCROLL_CHAIN |
        LV_OBJ_FLAG_SCROLL_ON_FOCUS |
        LV_OBJ_FLAG_SCROLL_WITH_ARROW));

    // 24h time (right side)
    {
        uint32_t epoch = sigurdos::mesh::getCurrentTime();
        char t[8];
        if (epoch == 0) snprintf(t, sizeof(t), "--:--");
        else {
            uint32_t sec = epoch % 86400;
            snprintf(t, sizeof(t), "%02d:%02d", (sec/3600)%24, (sec/60)%60);
        }
        lv_obj_t* tl = lv_label_create(top_bar);
        lv_label_set_text(tl, t);
        lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(tl, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // Search button (left of time label)
    lv_obj_t* search_btn = lv_btn_create(top_bar);
    lv_obj_set_size(search_btn, 24, TOP_H - 4);
    lv_obj_align(search_btn, LV_ALIGN_RIGHT_MID, -28, 0);
    lv_obj_set_style_bg_color(search_btn, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_border_width(search_btn, 0, 0);
    lv_obj_set_style_radius(search_btn, 0, 0);
    {
        lv_obj_t* sl = lv_label_create(search_btn);
        lv_label_set_text(sl, "S");
        lv_obj_set_style_text_color(sl, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(sl, emoji_wrapped_montserrat_12, 0);
        lv_obj_center(sl);
    }
    lv_obj_add_event_cb(search_btn, [](lv_event_t*) {
        if (search_active) {
            hide_search();
        } else {
            show_search_bar();
        }
    }, LV_EVENT_CLICKED, nullptr);

    for (int i = 0; i < dyn_count; i++) {
        create_channel_pill(channel_ribbon, i);
    }

    // Divider
    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, TOP_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
}

// ════════════════════════════════════════════════════
// Message bubble — Discord style
// ════════════════════════════════════════════════════
static lv_obj_t* create_bubble(lv_obj_t* parent, const char* sender,
                                const char* text, uint32_t timestamp,
                                bool is_self, bool acked)
{
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, BUBBLE_PAD / 2, 0);
    disable_scroll(container);

    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    if (is_self) {
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }

    lv_obj_t* bubble = lv_obj_create(container);
    lv_obj_set_width(bubble, LV_PCT(78));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, 0, 0);
    lv_obj_set_style_pad_all(bubble, 6, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    disable_scroll(bubble);

    if (is_self) {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_color(bubble, lv_color_hex(MSG_INCOMING), 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    }

    // Sender name + timestamp row
    lv_obj_t* header = lv_obj_create(bubble);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    disable_scroll(header);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* name = lv_label_create(header);
    lv_label_set_text(name, sender);
    lv_obj_set_style_text_color(name,
        is_self ? lv_color_hex(0xffffff) : lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(name, emoji_wrapped_montserrat_10, 0);

    char time_buf[10];
    if (is_self && acked) {
        uint32_t t = timestamp % 86400;
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d \xe2\x9c\x85",
                 (t / 3600) % 24, (t / 60) % 60);
    } else {
        format_time(time_buf, sizeof(time_buf), timestamp);
    }
    lv_obj_t* ts = lv_label_create(header);
    lv_label_set_text(ts, time_buf);
    lv_obj_set_style_text_color(ts,
        is_self ? lv_color_hex(0xffffff) : lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(ts, emoji_wrapped_montserrat_10, 0);

    lv_obj_t* msg_text = lv_label_create(bubble);
    lv_label_set_text(msg_text, text);
    lv_obj_set_style_text_color(msg_text,
        is_self ? lv_color_hex(0xffffff) : lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(msg_text, emoji_wrapped_montserrat_12, 0);
    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_text, LV_PCT(100));

    return container;
}

// ════════════════════════════════════════════════════
// Message list (vertical scroll)
// ════════════════════════════════════════════════════
static void create_message_list()
{
    msg_list = lv_obj_create(scr);
    lv_obj_set_size(msg_list, LV_PCT(100), MSG_LIST_H);
    lv_obj_align(msg_list, LV_ALIGN_TOP_MID, 0, MSG_LIST_Y);
    lv_obj_set_style_bg_color(msg_list, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_style_bg_opa(msg_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(msg_list, 0, 0);
    lv_obj_set_style_pad_all(msg_list, 2, 0);
    lv_obj_set_style_pad_bottom(msg_list, 0, 0);
    lv_obj_set_flex_flow(msg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(msg_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(msg_list, LV_SCROLLBAR_MODE_OFF);
    // Remove elastic/momentum/chain/focus/arrow flags so LVGL
    // clamps scrolling naturally at content boundaries.
    lv_obj_remove_flag(msg_list, (lv_obj_flag_t)(
        LV_OBJ_FLAG_SCROLL_ELASTIC |
        LV_OBJ_FLAG_SCROLL_MOMENTUM |
        LV_OBJ_FLAG_SCROLL_CHAIN |
        LV_OBJ_FLAG_SCROLL_ON_FOCUS |
        LV_OBJ_FLAG_SCROLL_WITH_ARROW));
    // No scroll snap — messages stay at bottom naturally.
    // Overscroll is prevented by the flag removals above,
    // and the trackball handler also clamps scroll deltas.
}

static void render_active_messages()
{
    if (!msg_list || active_channel < 0 || active_channel >= MAX_CHANNELS) return;

    const uint16_t cap = chat_msg_cap();
    trim_channel_history(active_channel, cap);
    if (!has_channel_buffer(active_channel)) return;

    lv_obj_clean(msg_list);

    // ── Search mode: render only matching messages ──
    if (search_active && search_query[0]) {
        if (search_match_count > 0) {
            for (int i = 0; i < search_match_count; i++) {
                int idx = search_matches[i];
                if (idx < 0 || idx >= ch_buffers[active_channel].count()) continue;
                ChannelMessage& msg = ch_buffers[active_channel].at(idx);
                lv_obj_t* bubble = create_bubble(msg_list, msg.sender, msg.text,
                                                  msg.timestamp, msg.is_self, msg.acked);
                // Highlight the current search match
                if (i == search_current_match && bubble) {
                    lv_obj_t* first_child = lv_obj_get_child(bubble, 0);
                    if (first_child) {
                        lv_obj_set_style_border_width(first_child, 2, 0);
                        lv_obj_set_style_border_color(first_child, lv_color_hex(ACCENT), 0);
                        lv_obj_set_style_border_opa(first_child, LV_OPA_COVER, 0);
                    }
                }
            }
        } else {
            // No matches — show "No results" message
            lv_obj_t* no_results = lv_label_create(msg_list);
            lv_label_set_text(no_results, "No matching messages");
            lv_obj_set_style_text_color(no_results, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(no_results, emoji_wrapped_montserrat_12, 0);
            lv_obj_center(no_results);
        }
        // Scroll to current match
        if (search_current_match >= 0 && search_current_match < search_match_count) {
            lv_obj_t* child = lv_obj_get_child(msg_list, search_current_match);
            if (child) lv_obj_scroll_to_view(child, LV_ANIM_OFF);
        }
        return;
    }

    // ── Normal mode: render all messages ──
    for (uint16_t i = 0; i < ch_buffers[active_channel].count(); i++) {
        ChannelMessage& msg = ch_buffers[active_channel].at(i);

        // Check ACK status for self-sent DM messages
        if (msg.is_self && !msg.acked) {
            if (strncmp(dyn_channels[active_channel], "DM: ", 4) == 0) {
                const char* dest = dyn_channels[active_channel] + 4;
                if (sigurdos::mesh::isMessageAcked(dest, msg.timestamp)) {
                    msg.acked = true;
                }
            }
        }

        create_bubble(msg_list, msg.sender, msg.text, msg.timestamp, msg.is_self, msg.acked);
    }

    uint32_t count = lv_obj_get_child_cnt(msg_list);
    if (count > 0) {
        lv_obj_t* last = lv_obj_get_child(msg_list, count - 1);
        if (last) lv_obj_scroll_to_view(last, LV_ANIM_OFF);
    }
}

// ════════════════════════════════════════════════════
// Emoji picker
// ════════════════════════════════════════════════════
static const char* emoji_picker_items[] = {
    // Faces
    "\xF0\x9F\x98\x80", "\xF0\x9F\x98\x81", "\xF0\x9F\x98\x82", "\xF0\x9F\x98\x83",
    "\xF0\x9F\xA4\xA3", "\xF0\x9F\x98\x8A", "\xF0\x9F\x98\x8D", "\xF0\x9F\x98\x8E",
    "\xF0\x9F\xA4\x94", "\xF0\x9F\x98\x8F", "\xF0\x9F\x98\xAE", "\xF0\x9F\x98\xA2",
    "\xF0\x9F\x98\xAD", "\xF0\x9F\x98\xA4", "\xF0\x9F\x98\xA1", "\xF0\x9F\xA5\xB0",
    // Hands
    "\xF0\x9F\x91\x8D", "\xF0\x9F\x91\x8E", "\xF0\x9F\x91\x8C", "\xE2\x9C\x8C",
    "\xF0\x9F\x91\x8F", "\xF0\x9F\x99\x8C", "\xF0\x9F\x99\x8F", "\xF0\x9F\x92\xAA",
    "\xF0\x9F\xA4\x9D", "\xF0\x9F\x91\x8B",
    // Hearts
    "\xE2\x9D\xA4", "\xF0\x9F\xA7\xA1", "\xF0\x9F\x92\x9B", "\xF0\x9F\x92\x9A",
    "\xF0\x9F\x92\x99", "\xF0\x9F\x92\x9C", "\xF0\x9F\x96\xA4", "\xF0\x9F\x92\x95",
    "\xF0\x9F\x92\x9E", "\xF0\x9F\x92\x93",
    // Objects/Symbols
    "\xF0\x9F\x94\xA5", "\xF0\x9F\x8E\x89", "\xF0\x9F\x8E\x8A", "\xE2\x9C\x85",
    "\xE2\x9D\x8C", "\xF0\x9F\x92\xAF", "\xE2\xAD\x90", "\xF0\x9F\x9A\x80",
    "\xF0\x9F\x8E\x88", "\xF0\x9F\x92\xA1", "\xF0\x9F\x94\x94", "\xF0\x9F\x8E\xAF",
    "\xF0\x9F\x94\x8B", "\xE2\x9A\x99", "\xF0\x9F\x93\xA1", "\xF0\x9F\x8C\x8D"
};
static constexpr int EMOJI_COUNT = sizeof(emoji_picker_items) / sizeof(emoji_picker_items[0]);

static void show_emoji_picker(lv_obj_t* parent)
{
    auto dlg_sz = dialog_size(296, 200);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 4, 0);

    // Close button
    lv_obj_t* close_btn = lv_btn_create(dlg);
    lv_obj_set_size(close_btn, 24, 20);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(close_btn, 0, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "X");
    lv_obj_set_style_text_font(close_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        lv_obj_t* d = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
        if (d) lv_obj_del_async(d);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Emoji");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // Scrollable grid container
    lv_obj_t* grid = lv_obj_create(dlg);
    lv_obj_set_size(grid, dlg_sz.w - 8, dlg_sz.h - 32);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 4, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(grid, (lv_obj_flag_t)(
        LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

    for (int i = 0; i < EMOJI_COUNT; i++) {
        lv_obj_t* btn = lv_btn_create(grid);
        lv_obj_set_size(btn, 28, 26);
        lv_obj_set_style_bg_color(btn, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 2, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, emoji_picker_items[i]);
        lv_obj_set_style_text_font(lbl, &emoji_font, 0);
        lv_obj_center(lbl);

        const char* emoji_text = emoji_picker_items[i];
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            const char* em = (const char*)lv_event_get_user_data(e);
            if (input_field) {
                lv_textarea_add_text(input_field, em);
            }
            lv_obj_t* d = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
            if (d) d = lv_obj_get_parent(d);
            if (d) lv_obj_del_async(d);
        }, LV_EVENT_CLICKED, (void*)emoji_text);
    }
}

// ════════════════════════════════════════════════════
// Send helper
// ════════════════════════════════════════════════════
static void do_send()
{
    const char* raw = lv_textarea_get_text(input_field);
    if (!raw || !raw[0]) return;

    // Input is now enforced to ≤ 149 bytes at the UI level (see byte-counter
    // handler in create_input_bar), so no truncation is needed before sending.
    char text[150];
    size_t len = strnlen(raw, sizeof(text) - 1);
    memcpy(text, raw, len);
    text[len] = '\0';

    const char* chan = dyn_channels[active_channel];
    bool is_dm = (strncmp(chan, "DM: ", 4) == 0);
    const char* dest = is_dm ? (chan + 4) : chan;

    const ChatPrivateScopeState* scope = get_chat_private_scope(chan);
    const uint8_t* scope_key = scope ? scope->key : nullptr;

    bool sent = false;
    uint32_t ts = sigurdos::mesh::getCurrentTime();
    if (is_dm) {
        uint32_t send_ts = sigurdos::mesh::sendMessageWithScopeKey(dest, text, scope_key);
        sent = (send_ts != 0);
        if (sent) ts = send_ts;  // use the timestamp the mesh layer tracked the ACK with
    } else {
        sent = sigurdos::mesh::sendChannelMessageWithScopeKey(dest, text, scope_key);
    }

    int sent_channel = active_channel;
    // Always show the message locally, but mark it if send failed
    char display_text[200];
    if (sent) {
        snprintf(display_text, sizeof(display_text), "%s", text);
    } else {
        snprintf(display_text, sizeof(display_text), "%s [FAILED]", text);
    }
    append_channel_message(sent_channel, sigurdos::mesh::getOwnName(), display_text, ts, true);
    mark_chat_history_dirty();
    mark_channel_used(sent_channel);
    render_active_messages();
    lv_textarea_set_text(input_field, "");

    lv_obj_t* last = lv_obj_get_child(msg_list, lv_obj_get_child_cnt(msg_list) - 1);
    if (last) lv_obj_scroll_to_view(last, LV_ANIM_OFF);
}

// ════════════════════════════════════════════════════
// Input bar
// ════════════════════════════════════════════════════
static void create_input_bar()
{
    int input_y = DISPLAY_H - BOT_BAR_H - INPUT_H - DIVIDER_H;

    input_bar = lv_obj_create(scr);
    lv_obj_set_size(input_bar, LV_PCT(100), INPUT_H);
    lv_obj_align(input_bar, LV_ALIGN_TOP_MID, 0, input_y + DIVIDER_H);
    lv_obj_set_style_bg_color(input_bar, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(input_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(input_bar, 4, 0);
    lv_obj_set_style_border_width(input_bar, 0, 0);
    disable_scroll(input_bar);

    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, input_y);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    input_field = lv_textarea_create(input_bar);
    int field_w = CONTENT_W - 90; // textarea + emoji btn(30) + send btn(52) + margins
    lv_obj_set_size(input_field, field_w, INPUT_H - 8);
    lv_obj_align(input_field, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(input_field, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_bg_opa(input_field, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(input_field, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(input_field, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_border_width(input_field, 1, 0);
    lv_obj_set_style_border_color(input_field, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(input_field, 0, 0);
    lv_obj_set_style_pad_all(input_field, 4, 0);
    lv_textarea_set_one_line(input_field, true);
    lv_textarea_set_placeholder_text(input_field, "Message #channel");
    lv_textarea_set_max_length(input_field, MAX_MSG_BYTES);
    lv_obj_remove_flag(input_field, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_outline_width(input_field, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(input_field, 0, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_EDITED));
    apply_focus_style(input_field);

    // Byte counter: small overlay showing remaining bytes (mesh limit = 149)
    lv_obj_set_style_pad_right(input_field, 28, 0);
    byte_counter = lv_label_create(input_field);
    lv_label_set_text(byte_counter, "149");
    lv_obj_set_style_text_font(byte_counter, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_text_color(byte_counter, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_align(byte_counter, LV_ALIGN_RIGHT_MID, -2, 0);

    // Emoji button — opens emoji picker dialog
    lv_obj_t* emoji_btn = lv_btn_create(input_bar);
    lv_obj_set_size(emoji_btn, 30, INPUT_H - 8);
    lv_obj_align(emoji_btn, LV_ALIGN_RIGHT_MID, -60, 0);
    lv_obj_set_style_bg_color(emoji_btn, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_bg_opa(emoji_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(emoji_btn, 0, 0);
    lv_obj_set_style_border_width(emoji_btn, 0, 0);
    {
        lv_obj_t* el = lv_label_create(emoji_btn);
        lv_label_set_text(el, "\xF0\x9F\x98\x80"); // 😀 emoji
        lv_obj_set_style_text_font(el, &emoji_font, 0);
        lv_obj_center(el);
    }
    lv_obj_add_event_cb(emoji_btn, [](lv_event_t*) {
        lv_obj_t* scr = lv_obj_get_screen(input_bar);
        if (scr) show_emoji_picker(scr);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* send_btn = lv_btn_create(input_bar);
    lv_obj_set_size(send_btn, 52, INPUT_H - 8);
    lv_obj_align(send_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_opa(send_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(send_btn, 0, 0);
    lv_obj_set_style_border_width(send_btn, 0, 0);

    lv_obj_t* send_label = lv_label_create(send_btn);
    lv_label_set_text(send_label, "Send");
    lv_obj_set_style_text_font(send_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_text_color(send_label, lv_color_hex(0xffffff), 0);
    lv_obj_center(send_label);

    lv_obj_add_event_cb(send_btn, [](lv_event_t*) { do_send(); },
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(input_field, [](lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_READY) {
            do_send();
        } else if (code == LV_EVENT_VALUE_CHANGED) {
            const char* raw = lv_textarea_get_text(input_field);
            size_t byte_len = strlen(raw);
            if (byte_len > MAX_MSG_BYTES) {
                // Truncate to 149 bytes (UTF-8 safe)
                size_t trunc_len = sigurdos::utf8_truncate_bytes(raw, MAX_MSG_BYTES);
                char buf[MAX_MSG_BYTES + 1];
                memcpy(buf, raw, trunc_len);
                buf[trunc_len] = '\0';
                lv_textarea_set_text(input_field, buf);
                // Counter will be updated by the recursive VALUE_CHANGED
            } else {
                // Update remaining-bytes counter
                if (byte_counter) {
                    int remaining = (int)MAX_MSG_BYTES - (int)byte_len;
                    char cb[8];
                    snprintf(cb, sizeof(cb), "%d", remaining);
                    lv_label_set_text(byte_counter, cb);
                }
            }
        }
    }, LV_EVENT_ALL, nullptr);
}

// ════════════════════════════════════════════════════
// Bottom status bar
// ════════════════════════════════════════════════════
static void create_bottom_bar()
{
    lv_obj_t* bot = lv_obj_create(scr);
    lv_obj_set_size(bot, LV_PCT(100), BOT_BAR_H);
    lv_obj_align(bot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(bot, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bot, 0, 0);
    lv_obj_set_style_border_width(bot, 0, 0);

    lv_obj_t* dev = lv_label_create(bot);
    lv_label_set_text(dev, sigurdos::mesh::getOwnName());
    lv_obj_set_style_text_color(dev, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dev, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(dev, LV_ALIGN_LEFT_MID, 4, 0);

    char batt_buf[8];
    int pct = sigurdos_battery_pct();
    snprintf(batt_buf, sizeof(batt_buf), "%d%%", pct);
    lv_obj_t* bl = lv_label_create(bot);
    lv_label_set_text(bl, batt_buf);
    lv_obj_set_style_text_color(bl, lv_color_hex(pct > 20 ? ACCENT : ACCENT_RED), 0);
    lv_obj_set_style_text_font(bl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(bl, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, LV_PCT(100), DIVIDER_H);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, DISPLAY_H - BOT_BAR_H - DIVIDER_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
}

// ════════════════════════════════════════════════════
// Messaging view (opened by tapping a channel)
// ════════════════════════════════════════════════════
static void open_channel_messaging(int idx)
{
    active_channel = idx;

    scr = lv_obj_create(nullptr);
    apply_dark_bg(scr);
    disable_scroll(scr);

    ch_list = nullptr;

    // When the messaging screen is auto-deleted (e.g. user navigates
    // away without pressing back), null all global widget pointers
    // so chat_screen_add_msg() doesn't dereference freed memory.
    // NOTE: ch_list is NOT nulled here — show_channel_list() may have
    // already set it to a new list before this delete callback fires.
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        scr = top_bar = channel_ribbon = msg_list = input_bar = input_field = nullptr;
        search_bar = nullptr;
        search_input = nullptr;
        search_active = false;
        search_match_count = 0;
        search_current_match = -1;
    }, LV_EVENT_DELETE, nullptr);

    // Reset search state
    search_active = false;
    search_query[0] = '\0';
    search_match_count = 0;
    search_current_match = -1;
    search_bar = nullptr;
    search_input = nullptr;

    create_top_bar();

    // For DM channels, show the contact's per-node signal bars in the top bar
    if (idx >= 0 && idx < dyn_count && dyn_channels[idx] &&
        strncmp(dyn_channels[idx], "DM: ", 4) == 0) {
        const char* contact_name = dyn_channels[idx] + 4;
        sigurdos::mesh::ContactInfo contact_info{};
        if (sigurdos::mesh::getContactByName(contact_name, &contact_info)) {
            lv_obj_t* sig = create_signal_dots(top_bar, contact_info.rssi);
            lv_obj_align(sig, LV_ALIGN_RIGHT_MID, -30, 0);
        }
    }

    create_message_list();
    render_active_messages();
    create_input_bar();
    create_bottom_bar();

    lv_group_t* g = lv_group_get_default();
    if (g && input_field) {
        lv_group_add_obj(g, input_field);
        lv_group_focus_obj(input_field);
    }

    lv_obj_t* old_scr = lv_screen_active();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    if (old_scr && old_scr != scr) lv_obj_del_async(old_scr);
}

static void refresh_chat_list_view(lv_obj_t* scr) {
    refresh_channels();
    uint32_t n = lv_obj_get_child_cnt(scr);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* c = lv_obj_get_child(scr, i);
        if (lv_obj_get_user_data(c) == (void*)0xCA7C) {
            lv_obj_clean(c);
            populate_channel_rows(c);
            return;
        }
    }
}

static void show_add_channel_options(lv_obj_t* parent) {
    // 190px height gives consistent 8px gaps between all elements
    auto dlg_sz = dialog_size(260, 190);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Add Channel");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* nl = lv_label_create(dlg);
    lv_label_set_text(nl, "Name:");
    lv_obj_set_style_text_color(nl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_align(nl, LV_ALIGN_TOP_LEFT, 4, 24);

    lv_obj_t* ni = lv_textarea_create(dlg);
    lv_obj_set_size(ni, dlg_sz.w - 16, 26);
    lv_obj_align(ni, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_bg_color(ni, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(ni, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(ni, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(ni, 0, 0);
    lv_textarea_set_one_line(ni, true);
    lv_textarea_set_max_length(ni, MAX_NAME_LEN);
    lv_textarea_set_placeholder_text(ni, "e.g. #general");
    apply_focus_style(ni);

    lv_obj_t* pl = lv_label_create(dlg);
    lv_label_set_text(pl, "PSK (optional):");
    lv_obj_set_style_text_color(pl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_align(pl, LV_ALIGN_TOP_LEFT, 4, 72);

    lv_obj_t* pi = lv_textarea_create(dlg);
    lv_obj_set_size(pi, dlg_sz.w - 16, 26);
    lv_obj_align(pi, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_style_bg_color(pi, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(pi, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(pi, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(pi, 0, 0);
    lv_textarea_set_one_line(pi, true);
    lv_textarea_set_max_length(pi, 44); // base64 PSK keys are 24 bytes -> 32 base64 chars
    lv_textarea_set_placeholder_text(pi, "base64 key (blank = public)");
    apply_focus_style(pi);

    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, ni);
        lv_group_add_obj(g, pi);
        lv_group_focus_obj(ni);
    }

    lv_obj_t* fb = lv_label_create(dlg);
    lv_obj_set_style_text_color(fb, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(fb, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(fb, LV_ALIGN_BOTTOM_MID, 0, -32);

    lv_obj_t* add = lv_btn_create(dlg);
    lv_obj_set_size(add, 100, 28);
    lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(add, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(add, 0, 0);
    lv_obj_t* al = lv_label_create(add);
    lv_label_set_text(al, "Add");
    lv_obj_center(al);

    auto submit = [](lv_event_t* e) {
        lv_obj_t* d = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
        lv_obj_t* sc = lv_obj_get_screen(d);
        lv_obj_t* feedback = (lv_obj_t*)lv_event_get_user_data(e);

        lv_obj_t* inputs[2] = {nullptr, nullptr};
        int idx = 0;
        for (uint32_t j = 0; j < lv_obj_get_child_cnt(d) && idx < 2; j++) {
            lv_obj_t* child = lv_obj_get_child(d, j);
            if (lv_obj_check_type(child, &lv_textarea_class)) {
                inputs[idx++] = child;
            }
        }

        const char* nm  = inputs[0] ? lv_textarea_get_text(inputs[0]) : "";
        const char* psk = inputs[1] ? lv_textarea_get_text(inputs[1]) : "";

        if (!nm[0]) { if (feedback) lv_label_set_text(feedback, "Enter channel name"); return; }

        // Validate channel name before passing to mesh
        const char* val_reason = nullptr;
        if (!sigurdos::mesh::channel_name_valid(nm, &val_reason)) {
            if (feedback) lv_label_set_text(feedback, val_reason);
            return;
        }

        bool ok;
        if (psk[0]) {
            // PSK provided — add as encrypted channel
            ok = sigurdos::mesh::addChannel(nm, psk);
        } else {
            // No PSK — add as public hashtag channel
            ok = sigurdos::mesh::addHashtagChannel(nm);
        }

        if (ok) {
            lv_obj_del_async(d);
            refresh_chat_list_view(sc);
        } else {
            if (feedback) lv_label_set_text(feedback, "Invalid or full");
        }
    };

    lv_obj_add_event_cb(add, submit, LV_EVENT_CLICKED, (void*)fb);
    // ENTER key on name input: if PSK empty, submit; otherwise focus PSK
    lv_obj_add_event_cb(ni, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_READY) return;
        lv_obj_t* input = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* d = lv_obj_get_parent(input);
        lv_obj_t* feedback = (lv_obj_t*)lv_event_get_user_data(e);

        // Find PSK input
        lv_obj_t* psk_input = nullptr;
        for (uint32_t j = 0; j < lv_obj_get_child_cnt(d); j++) {
            lv_obj_t* child = lv_obj_get_child(d, j);
            if (lv_obj_check_type(child, &lv_textarea_class) && child != input) {
                psk_input = child;
                break;
            }
        }

        const char* psk = psk_input ? lv_textarea_get_text(psk_input) : "";
        if (psk && psk[0]) {
            // PSK field has content — focus it for editing
            if (psk_input && lv_group_get_default())
                lv_group_focus_obj(psk_input);
        } else {
            // No PSK — submit directly
            lv_obj_t* sc = lv_obj_get_screen(d);
            const char* nm = lv_textarea_get_text(input);
            if (!nm || !nm[0]) { if (feedback) lv_label_set_text(feedback, "Enter channel name"); return; }
            // Validate channel name before passing to mesh
            const char* val_reason2 = nullptr;
            if (!sigurdos::mesh::channel_name_valid(nm, &val_reason2)) {
                if (feedback) lv_label_set_text(feedback, val_reason2);
                return;
            }
            bool ok = sigurdos::mesh::addHashtagChannel(nm);
            if (ok) {
                lv_obj_del_async(d);
                refresh_chat_list_view(sc);
            } else {
                if (feedback) lv_label_set_text(feedback, "Invalid or full");
            }
        }
    }, LV_EVENT_ALL, (void*)fb);
    // ENTER key on PSK input: submit
    lv_obj_add_event_cb(pi, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_READY) return;
        lv_obj_t* input = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* d = lv_obj_get_parent(input);
        lv_obj_t* sc = lv_obj_get_screen(d);
        lv_obj_t* feedback = (lv_obj_t*)lv_event_get_user_data(e);

        lv_obj_t* inputs[2] = {nullptr, nullptr};
        int idx = 0;
        for (uint32_t j = 0; j < lv_obj_get_child_cnt(d) && idx < 2; j++) {
            lv_obj_t* child = lv_obj_get_child(d, j);
            if (lv_obj_check_type(child, &lv_textarea_class)) {
                inputs[idx++] = child;
            }
        }

        const char* nm  = inputs[0] ? lv_textarea_get_text(inputs[0]) : "";
        const char* psk = inputs[1] ? lv_textarea_get_text(inputs[1]) : "";
        if (!nm || !nm[0]) { if (feedback) lv_label_set_text(feedback, "Enter channel name"); return; }

        // Validate channel name before passing to mesh
        const char* val_reason3 = nullptr;
        if (!sigurdos::mesh::channel_name_valid(nm, &val_reason3)) {
            if (feedback) lv_label_set_text(feedback, val_reason3);
            return;
        }

        bool ok;
        if (psk && psk[0]) {
            ok = sigurdos::mesh::addChannel(nm, psk);
        } else {
            ok = sigurdos::mesh::addHashtagChannel(nm);
        }
        if (ok) {
            lv_obj_del_async(d);
            refresh_chat_list_view(sc);
        } else {
            if (feedback) lv_label_set_text(feedback, "Invalid or full");
        }
    }, LV_EVENT_ALL, (void*)fb);
}

// ── Channel quick-action menu (keyboard shortcut emits 0x0C) ───────────────
// Small popup over the messaging view for per-chat private scope entry
// plus normal chat actions. The validation/key derivation lives in
// channel_menu.{h,cpp}; this block only renders and stores per-chat state.

static void show_scope_picker();

static const char* channel_action_icon(ChannelAction a) {
    switch (a) {
    case ChannelAction::ChooseScope:  return LV_SYMBOL_GPS;
    case ChannelAction::MarkRead:     return LV_SYMBOL_EYE_OPEN;
    case ChannelAction::LeaveChannel: return LV_SYMBOL_TRASH;
    default:                          return LV_SYMBOL_RIGHT;
    }
}

static void channel_menu_action_cb(lv_event_t* e) {
    ChannelAction action = (ChannelAction)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t* btn  = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* list = lv_obj_get_parent(btn);
    lv_obj_t* dlg  = list ? lv_obj_get_parent(list) : nullptr;
    lv_obj_t* feedback = list ? (lv_obj_t*)lv_obj_get_user_data(list) : nullptr;

    if (active_channel < 0 || active_channel >= dyn_count) {
        if (dlg) lv_obj_del_async(dlg);
        return;
    }
    const char* channel = dyn_channels[active_channel];
    const int   idx     = active_channel;

    if (action == ChannelAction::ChooseScope) {
        if (dlg) lv_obj_del_async(dlg);
        show_scope_picker();
        return;
    }

    if (action == ChannelAction::MarkRead) {
        ch_meta[idx].unread = 0;
        rebuild_channel_ribbon();
        if (feedback) {
            lv_label_set_text(feedback, "Marked all read");
            lv_obj_set_style_text_color(feedback, lv_color_hex(ACCENT_GREEN), 0);
        }
        return;
    }

    if (action == ChannelAction::LeaveChannel) {
        channel_menu_perform(action, channel, idx);
        clear_chat_private_scope(channel);
        show_channel_list();
        return;
    }
}

void chat_screen_show_channel_menu()
{
    if (!input_field || !lv_obj_is_valid(input_field)) return;
    if (active_channel < 0 || active_channel >= dyn_count) return;

    const char* channel = dyn_channels[active_channel];
    if (!channel || !channel[0]) return;

    ChannelMenuItem items[8];
    int n = channel_menu_build(channel, items, 8);
    if (n <= 0) return;

    lv_obj_t* parent = lv_scr_act();

    auto dlg_sz = dialog_size(240, 176);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);
    disable_scroll(dlg);

    channel_menu = dlg;
    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        if (channel_menu == (lv_obj_t*)lv_event_get_target(e)) channel_menu = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, channel);
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* scope_lbl = lv_label_create(dlg);
    char scope_buf[48];
    const ChatPrivateScopeState* scope = get_chat_private_scope(channel);
    if (scope) snprintf(scope_buf, sizeof(scope_buf), "Private: %s", scope->name);
    else       snprintf(scope_buf, sizeof(scope_buf), "Private: none");
    lv_label_set_text(scope_lbl, scope_buf);
    lv_obj_set_style_text_color(scope_lbl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(scope_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(scope_lbl, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t* list = lv_obj_create(dlg);
    lv_obj_set_size(list, dlg_sz.w - 16, dlg_sz.h - 90);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* feedback = lv_label_create(dlg);
    lv_label_set_text(feedback, "");
    lv_obj_set_style_text_color(feedback, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(feedback, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(feedback, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_user_data(list, feedback);

    lv_group_t* g = lv_group_get_default();

    for (int i = 0; i < n; i++) {
        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 28);
        bool destructive = items[i].action == ChannelAction::LeaveChannel;
        lv_obj_set_style_bg_color(btn,
            lv_color_hex(destructive ? ACCENT_RED : BG_INPUT), 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_left(btn, 6, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        char row[64];
        snprintf(row, sizeof(row), "%s  %s",
                 channel_action_icon(items[i].action), items[i].label);
        lv_label_set_text(lbl, row);
        lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_add_event_cb(btn, channel_menu_action_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)items[i].action);
        if (g) {
            lv_group_add_obj(g, btn);
            if (i == 0) lv_group_focus_obj(btn);
        }
    }

    lv_obj_t* close = lv_btn_create(dlg);
    lv_obj_set_size(close, 80, 26);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(close, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_radius(close, 0, 0);
    lv_obj_set_style_border_width(close, 0, 0);
    lv_obj_t* cl = lv_label_create(close);
    lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_font(cl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(close, [](lv_event_t* e) {
        lv_obj_t* b = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_del_async(lv_obj_get_parent(b));
        if (input_field && lv_obj_is_valid(input_field) && lv_group_get_default())
            lv_group_focus_obj(input_field);
    }, LV_EVENT_CLICKED, nullptr);
    if (g) lv_group_add_obj(g, close);
}

// ── Private scope editor ───────────────────────────────────

static lv_obj_t* g_scope_subtitle = nullptr;

static const char* current_scope_channel()
{
    return (active_channel >= 0 && active_channel < dyn_count) ? dyn_channels[active_channel] : "";
}

static void update_private_scope_subtitle()
{
    if (!g_scope_subtitle || !lv_obj_is_valid(g_scope_subtitle)) return;
    const ChatPrivateScopeState* scope = get_chat_private_scope(current_scope_channel());
    char sb[48];
    if (scope) snprintf(sb, sizeof(sb), "This chat: %s", scope->name);
    else       snprintf(sb, sizeof(sb), "This chat: none");
    lv_label_set_text(g_scope_subtitle, sb);
    lv_obj_set_style_text_color(g_scope_subtitle, lv_color_hex(TEXT_SECONDARY), 0);
}

static void scope_custom_apply(lv_obj_t* ta) {
    if (!ta) return;
    const char* channel = current_scope_channel();
    if (!channel || !channel[0]) return;

    const char* text = lv_textarea_get_text(ta);
    const char* reason = nullptr;
    char name[31];
    uint8_t key[16];
    if (!private_scope_prepare(text, name, sizeof(name), key, &reason)) {
        if (g_scope_subtitle && lv_obj_is_valid(g_scope_subtitle)) {
            lv_label_set_text(g_scope_subtitle, reason ? reason : "Invalid scope");
            lv_obj_set_style_text_color(g_scope_subtitle, lv_color_hex(ACCENT_RED), 0);
        }
        return;
    }

    if (name[0]) {
        if (!set_chat_private_scope(channel, name, key)) {
            if (g_scope_subtitle && lv_obj_is_valid(g_scope_subtitle)) {
                lv_label_set_text(g_scope_subtitle, "Scope table full");
                lv_obj_set_style_text_color(g_scope_subtitle, lv_color_hex(ACCENT_RED), 0);
            }
            return;
        }
    } else {
        clear_chat_private_scope(channel);
    }

    lv_textarea_set_text(ta, "");
    update_private_scope_subtitle();
}

static void show_scope_picker() {
    if (!input_field || !lv_obj_is_valid(input_field)) return;
    if (!current_scope_channel()[0]) return;
    lv_obj_t* parent = lv_scr_act();

    auto dlg_sz = dialog_size(250, 150);
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_border_color(dlg, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);
    disable_scroll(dlg);

    channel_menu = dlg;
    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        if (channel_menu == (lv_obj_t*)lv_event_get_target(e)) channel_menu = nullptr;
        g_scope_subtitle = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Private scope");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    g_scope_subtitle = lv_label_create(dlg);
    lv_obj_set_style_text_color(g_scope_subtitle, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(g_scope_subtitle, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(g_scope_subtitle, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t* ta = lv_textarea_create(dlg);
    lv_obj_set_size(ta, dlg_sz.w - 16 - 48, 24);
    lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 0, 38);
    lv_obj_set_style_bg_color(ta, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(ta, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 30);
    lv_textarea_set_placeholder_text(ta, "private scope");
    apply_focus_style(ta);

    lv_obj_t* set_btn = lv_btn_create(dlg);
    lv_obj_set_size(set_btn, 44, 24);
    lv_obj_align(set_btn, LV_ALIGN_TOP_RIGHT, 0, 38);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(set_btn, 0, 0);
    lv_obj_set_style_border_width(set_btn, 0, 0);
    lv_obj_t* sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_set_style_text_font(sl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(sl);
    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        scope_custom_apply((lv_obj_t*)lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, (void*)ta);

    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_READY)
            scope_custom_apply((lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_ALL, nullptr);

    lv_obj_t* clear = lv_btn_create(dlg);
    lv_obj_set_size(clear, 74, 24);
    lv_obj_align(clear, LV_ALIGN_BOTTOM_LEFT, 10, -4);
    lv_obj_set_style_bg_color(clear, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_radius(clear, 0, 0);
    lv_obj_set_style_border_width(clear, 0, 0);
    lv_obj_t* clr = lv_label_create(clear);
    lv_label_set_text(clr, "Clear");
    lv_obj_set_style_text_font(clr, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(clr);
    lv_obj_add_event_cb(clear, [](lv_event_t*) {
        clear_chat_private_scope(current_scope_channel());
        update_private_scope_subtitle();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* close = lv_btn_create(dlg);
    lv_obj_set_size(close, 74, 24);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -10, -4);
    lv_obj_set_style_bg_color(close, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_radius(close, 0, 0);
    lv_obj_set_style_border_width(close, 0, 0);
    lv_obj_t* cl = lv_label_create(close);
    lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_font(cl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(close, [](lv_event_t* e) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
        if (input_field && lv_obj_is_valid(input_field) && lv_group_get_default())
            lv_group_focus_obj(input_field);
    }, LV_EVENT_CLICKED, nullptr);

    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, ta);
        lv_group_add_obj(g, set_btn);
        lv_group_add_obj(g, clear);
        lv_group_add_obj(g, close);
        lv_group_focus_obj(ta);
    }

    update_private_scope_subtitle();
}

void chat_screen_set_filter(int mode) {
    chat_filter_mode = mode;
}

bool chat_screen_overlay_active() {
    return channel_menu != nullptr && lv_obj_is_valid(channel_menu);
}

void chat_screen_show()
{
    screens_clear_back_btn();
    screens_clear_wifi_icon();
    // Skip channel list when DM is being opened directly —
    // open_channel_messaging() will create the messaging screen instead.
    if (g_skip_channel_list) {
        g_skip_channel_list = false;
        // Reset unread badge counter when the user opens chat
        sigurdos::mesh::resetUnreadMessageCount();
        return;
    }
    show_channel_list();
    // Reset unread badge counter when the user opens chat
    sigurdos::mesh::resetUnreadMessageCount();
}

void chat_screen_open_dm(const char* contact_name)
{
    if (!contact_name || !contact_name[0]) return;

    // Signal chat_screen_show() to skip the channel-list screen
    // so we go directly to the messaging view without a wasteful
    // intermediate lv_scr_load_anim that causes a crash when
    // open_channel_messaging() triggers a second screen load.
    g_skip_channel_list = true;
    navigate_to(Screen::Chat);
    refresh_channels();

    // Buffer must fit "DM: " (4) + max contact name (31) + null (1) = 36
    char dm_name[CHANNEL_NAME_CAP];
    snprintf(dm_name, sizeof(dm_name), "DM: %s", contact_name);

    int idx = find_channel_idx(dm_name);
    if (idx < 0 && dyn_count < MAX_CHANNELS) {
        idx = dyn_count;
        strncpy(dyn_channels[idx], dm_name, sizeof(dyn_channels[idx]) - 1);
        dyn_channels[idx][sizeof(dyn_channels[idx]) - 1] = '\0';
        dyn_count++;
    }

    if (idx >= 0 && idx < MAX_CHANNELS) {
        open_channel_messaging(idx);
    }
}

void chat_screen_add_msg(const char* channel, const char* sender, const char* text, bool is_self)
{
    chat_screen_add_msg_at(channel, sender, text, 0, is_self);
}

void chat_screen_add_msg_at(const char* channel, const char* sender, const char* text,
                            uint32_t timestamp, bool is_self)
{
    const uint32_t fallback_now = timestamp ? 0 : sigurdos::mesh::getCurrentTime();
    const uint32_t message_time = chat_screen_resolve_message_timestamp(
        timestamp, fallback_now);

    // Map DM messages (empty channel) to "DM: <sender>" conversation
    char dm_buf[CHANNEL_NAME_CAP];
    if (!channel || !channel[0]) {
        snprintf(dm_buf, sizeof(dm_buf), "DM: %s", sender);
        channel = dm_buf;
    }

    int idx = find_channel_idx(channel);
    if (idx < 0) {
        if (dyn_count < MAX_CHANNELS) {
            idx = dyn_count;
            strncpy(dyn_channels[idx], channel, sizeof(dyn_channels[idx]) - 1);
            dyn_channels[idx][sizeof(dyn_channels[idx]) - 1] = '\0';
            dyn_count++;
        } else {
            return;
        }
    }
    if (idx >= MAX_CHANNELS) return;

    append_channel_message(idx, sender, text, message_time, is_self);
    mark_chat_history_dirty();

    bool visible = msg_list && idx == active_channel && current_screen() == Screen::Chat;
    if (!is_self && !visible) ch_meta[idx].unread++;
    if (!visible) return;

    // Check if user is at the bottom BEFORE adding the new bubble
    bool at_bottom = (lv_obj_get_scroll_bottom(msg_list) <= 4);

    create_bubble(msg_list, sender, text, message_time, is_self, false);

    const uint16_t cap = chat_msg_cap();
    if (lv_obj_get_child_cnt(msg_list) > cap)
        lv_obj_del_async(lv_obj_get_child(msg_list, 0));

    // Only auto-scroll if user was already at the bottom
    if (at_bottom) {
        lv_obj_t* last = lv_obj_get_child(msg_list, lv_obj_get_child_cnt(msg_list) - 1);
        if (last) lv_obj_scroll_to_view(last, LV_ANIM_OFF);
    }
}

// ════════════════════════════════════════════════════
// Periodic ACK refresh
// ════════════════════════════════════════════════════
void chat_screen_refresh_acks()
{
    if (!msg_list) return;
    static int last_ack_counter = 0;
    int cur = sigurdos::mesh::getAckCounter();
    if (cur != last_ack_counter) {
        last_ack_counter = cur;
        render_active_messages();
    }
}

// ════════════════════════════════════════════════════
// Trackball handler
// ════════════════════════════════════════════════════
bool chat_screen_handle_trackball(SigurdOSTrackballEvent event)
{
    // While the channel menu overlay is open, let trackball events fall
    // through to the LVGL group so its buttons stay focus-navigable.
    if (channel_menu && lv_obj_is_valid(channel_menu)) return false;

    if (msg_list) {
        // ── Search mode: Up/Down cycles through matches, Left dismisses search ──
        if (search_active && search_query[0] && search_match_count > 0) {
            switch (event) {
            case SigurdOSTrackballEvent::Up:
            case SigurdOSTrackballEvent::Down: {
                if (event == SigurdOSTrackballEvent::Up) {
                    search_current_match = (search_current_match <= 0) ?
                        search_match_count - 1 : search_current_match - 1;
                } else {
                    search_current_match = (search_current_match >= search_match_count - 1) ?
                        0 : search_current_match + 1;
                }
                render_active_messages();
                lv_obj_t* child = lv_obj_get_child(msg_list, search_current_match);
                if (child) lv_obj_scroll_to_view(child, LV_ANIM_OFF);
                return true;
            }
            case SigurdOSTrackballEvent::Left:
                hide_search();
                show_channel_list();
                return true;
            default:
                return true;
            }
        }

        switch (event) {
        case SigurdOSTrackballEvent::Up: {
            // Let LVGL clamp at top (no elastic = hard stop at 0)
            lv_coord_t sy = lv_obj_get_scroll_y(msg_list);
            lv_coord_t new_y = sy > 44 ? sy - 44 : 0;
            lv_obj_scroll_to_y(msg_list, new_y, LV_ANIM_OFF);
            return true;
        }
        case SigurdOSTrackballEvent::Down: {
            // Let LVGL clamp at bottom (no elastic = hard stop at max)
            lv_coord_t sy = lv_obj_get_scroll_y(msg_list);
            lv_obj_scroll_to_y(msg_list, sy + 44, LV_ANIM_OFF);
            return true;
        }
        case SigurdOSTrackballEvent::Left:
            show_channel_list();
            return true;
        case SigurdOSTrackballEvent::Right:
            if (input_field && lv_obj_is_valid(input_field)) {
                lv_group_t* g = lv_group_get_default();
                if (g) lv_group_focus_obj(input_field);
            }
            return true;
        case SigurdOSTrackballEvent::Click:
            if (input_field && lv_obj_is_valid(input_field)) {
                lv_group_t* g = lv_group_get_default();
                if (g) lv_group_focus_obj(input_field);
            }
            return true;
        default:
            return false;
        }
    }

    if (ch_list) {
        switch (event) {
        case SigurdOSTrackballEvent::Up:
        case SigurdOSTrackballEvent::Down: {
            // Any vertical motion returns focus to the channel list
            if (ch_focus != 0) {
                clear_ch_focus_buttons();
                ch_focus = 0;
                if (ch_list_selected >= 0 && ch_list_selected < dyn_count) {
                    lv_obj_t* row = lv_obj_get_child(ch_list, ch_list_selected);
                    if (row) apply_ch_row_selection(row, true);
                }
            }
            int old = ch_list_selected;
            if (event == SigurdOSTrackballEvent::Up)
                ch_list_selected = ch_list_selected > 0 ? ch_list_selected - 1 : dyn_count - 1;
            else
                ch_list_selected = ch_list_selected < dyn_count - 1 ? ch_list_selected + 1 : 0;
            if (ch_list_selected != old) {
                lv_obj_t* old_row = lv_obj_get_child(ch_list, old);
                if (old_row) apply_ch_row_selection(old_row, false);
                lv_obj_t* new_row = lv_obj_get_child(ch_list, ch_list_selected);
                if (new_row) {
                    apply_ch_row_selection(new_row, true);
                    lv_obj_scroll_to_view(new_row, LV_ANIM_ON);
                }
            }
            return true;
        }
        case SigurdOSTrackballEvent::Left:
            if (ch_focus == 1) {
                go_back();
            } else if (ch_focus == 0) {
                clear_ch_row_selection();
                ch_focus = 1;
                if (ch_back_btn) {
                    lv_obj_set_style_border_width(ch_back_btn, 2, 0);
                    lv_obj_set_style_border_color(ch_back_btn, lv_color_hex(ACCENT), 0);
                }
            } else {
                // ch_focus == 2, return to list
                clear_ch_focus_buttons();
                ch_focus = 0;
                if (ch_list_selected >= 0) {
                    lv_obj_t* row = lv_obj_get_child(ch_list, ch_list_selected);
                    if (row) apply_ch_row_selection(row, true);
                }
            }
            return true;
        case SigurdOSTrackballEvent::Right:
            if (ch_focus == 2) {
                if (ch_add_btn) lv_obj_set_style_border_width(ch_add_btn, 0, 0);
                ch_focus = 0;
                if (ch_list_selected >= 0) {
                    lv_obj_t* row = lv_obj_get_child(ch_list, ch_list_selected);
                    if (row) apply_ch_row_selection(row, true);
                }
            } else if (ch_focus == 0) {
                clear_ch_row_selection();
                ch_focus = 2;
                if (ch_add_btn) {
                    lv_obj_set_style_border_width(ch_add_btn, 2, 0);
                    lv_obj_set_style_border_color(ch_add_btn, lv_color_hex(ACCENT), 0);
                }
            } else {
                // ch_focus == 1, return to list
                clear_ch_focus_buttons();
                ch_focus = 0;
                if (ch_list_selected >= 0) {
                    lv_obj_t* row = lv_obj_get_child(ch_list, ch_list_selected);
                    if (row) apply_ch_row_selection(row, true);
                }
            }
            return true;
        case SigurdOSTrackballEvent::Click:
            if (ch_focus == 1) {
                go_back();
            } else if (ch_focus == 2 && ch_add_btn) {
                lv_obj_send_event(ch_add_btn, LV_EVENT_CLICKED, nullptr);
            } else if (ch_list_selected >= 0 && ch_list_selected < dyn_count) {
                ch_meta[ch_list_selected].unread = 0;
                open_channel_messaging(ch_list_selected);
            }
            return true;
        default:
            return true;
        }
    }

    return false;
}

lv_obj_t* chat_screen_get_input_field()
{
    return input_field;
}

const char* chat_screen_get_active_channel_name()
{
    if (active_channel >= 0 && active_channel < dyn_count) {
        return dyn_channels[active_channel];
    }
    return "";
}

// ════════════════════════════════════════════════════
// Message persistence
// ════════════════════════════════════════════════════

struct ChatHistorySaveCtx {
    int channel_indices[CHAT_HISTORY_MAX_CHANNELS];
    uint16_t message_cap;
};

static bool read_history_channel(int stored_index, char* name_out,
                                 size_t name_len, uint8_t* message_count_out,
                                 void* raw)
{
    ChatHistorySaveCtx* ctx = static_cast<ChatHistorySaveCtx*>(raw);
    if (!ctx || !name_out || name_len == 0 || !message_count_out ||
        stored_index < 0 || stored_index >= (int)CHAT_HISTORY_MAX_CHANNELS) {
        return false;
    }
    const int channel = ctx->channel_indices[stored_index];
    if (channel < 0 || channel >= dyn_count || !has_channel_buffer(channel)) {
        return false;
    }
    std::strncpy(name_out, dyn_channels[channel], name_len - 1);
    name_out[name_len - 1] = '\0';
    const uint16_t count = ch_buffers[channel].count() > ctx->message_cap
        ? ctx->message_cap : ch_buffers[channel].count();
    *message_count_out = (uint8_t)count;
    return true;
}

static bool read_history_message(int stored_index, int message_index,
                                 PersistedChatMessage* out, void* raw)
{
    ChatHistorySaveCtx* ctx = static_cast<ChatHistorySaveCtx*>(raw);
    if (!ctx || !out || stored_index < 0 ||
        stored_index >= (int)CHAT_HISTORY_MAX_CHANNELS) return false;
    const int channel = ctx->channel_indices[stored_index];
    if (channel < 0 || channel >= dyn_count || !has_channel_buffer(channel) ||
        message_index < 0 || message_index >= ch_buffers[channel].count()) return false;

    const ChannelMessage& source = ch_buffers[channel].at(message_index);
    std::strncpy(out->sender, source.sender, sizeof(out->sender) - 1);
    std::strncpy(out->text, source.text, sizeof(out->text) - 1);
    out->timestamp = source.timestamp;
    out->is_self = source.is_self;
    return true;
}

static bool write_history_message(const char* channel,
                                  const PersistedChatMessage& message,
                                  void*)
{
    int idx = find_channel_idx(channel);
    // DM pseudo-channels are not returned by exportChannels(), so restore them
    // on demand when their first persisted message is read.
    if (idx < 0 && std::strncmp(channel, "DM: ", 4) == 0 &&
        dyn_count < MAX_CHANNELS) {
        idx = dyn_count;
        std::strncpy(dyn_channels[idx], channel, sizeof(dyn_channels[idx]) - 1);
        dyn_channels[idx][sizeof(dyn_channels[idx]) - 1] = '\0';
        ++dyn_count;
    }
    if (idx < 0 || idx >= MAX_CHANNELS) return true;
    append_channel_message(idx, message.sender, message.text,
                           message.timestamp, message.is_self);
    return true;
}

void chat_save_messages()
{
    ChatHistorySaveCtx ctx{{}, chat_msg_cap()};
    int stored_count = 0;
    for (int channel = 0; channel < dyn_count &&
         stored_count < (int)CHAT_HISTORY_MAX_CHANNELS; ++channel) {
        if (ch_buffers[channel].count() > 0 && has_channel_buffer(channel)) {
            ctx.channel_indices[stored_count++] = channel;
        }
    }
    if (chatHistorySave(stored_count, read_history_channel,
                        read_history_message, &ctx)) {
        chat_checkpoint.saved();
    }
}

void chat_save_messages_if_due(uint32_t now)
{
    if (chat_checkpoint.isDue(now)) {
        chat_save_messages();
    }
}

void chat_load_messages()
{
    if (dyn_count == 0) {
        dyn_count = sigurdos::mesh::exportChannels(dyn_channels, MAX_CHANNELS);
        if (dyn_count == 0 && sigurdos::mesh::joinPublicChannel()) {
            dyn_count = sigurdos::mesh::exportChannels(dyn_channels, MAX_CHANNELS);
        }
        if (dyn_count == 0) {
            strncpy(dyn_channels[0], "#general", sizeof(dyn_channels[0]) - 1);
            dyn_channels[0][sizeof(dyn_channels[0]) - 1] = '\0';
            dyn_count = 1;
        }
    }

    chatHistoryLoad(write_history_message, nullptr);
    chat_load_companion_messages();
}

uint16_t chat_screen_get_message_cap()
{
    return chat_msg_cap();
}

void chat_screen_set_message_cap(uint16_t cap)
{
    const uint16_t clamped = chat_screen_normalize_message_cap(cap);

    sigurdos::NodePrefs np = sigurdos::prefs_get();
    np.chat_msg_cap = clamped;
    sigurdos::prefs_set(np);

    for (int i = 0; i < MAX_CHANNELS; i++) {
        trim_channel_history(i, clamped);
    }
}

} // namespace sigurdos::ui
