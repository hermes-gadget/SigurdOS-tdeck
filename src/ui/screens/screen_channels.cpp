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

#include "../screens.h"
#include "../screens_common.h"
#include "../theme.h"
#include "../responsive.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../mesh/channel_validation.h"
#include "../../mesh/public_channel.h"
#include "../../app/qr_show.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <functional>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ════════════════════════════════════════════════════════
// Channels — channel list with create/join and delete
// ════════════════════════════════════════════════════════
static std::function<void()> g_channels_rebuild = nullptr;  // set by channels_screen_show

static lv_obj_t* channel_create_dialog(lv_obj_t* parent)
{
    auto dlg_sz = dialog_size(260, 140);
    lv_obj_t* dialog = lv_obj_create(parent);
    lv_obj_set_size(dialog, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dialog, 0, 0);
    lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_pad_all(dialog, 8, 0);

    lv_obj_t* title = lv_label_create(dialog);
    lv_label_set_text(title, "Add # Channel");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* name_label = lv_label_create(dialog);
    lv_label_set_text(name_label, "Hashtag:");
    lv_obj_set_style_text_color(name_label, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 4, 28);

    lv_obj_t* name_input = lv_textarea_create(dialog);
    lv_obj_set_size(name_input, dlg_sz.w - 16, 28);
    lv_obj_align(name_input, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_color(name_input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(name_input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(name_input, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(name_input, 0, 0);
    lv_textarea_set_one_line(name_input, true);
    lv_textarea_set_max_length(name_input, 31);
    lv_textarea_set_placeholder_text(name_input, "e.g. #general");
    apply_focus_style(name_input);

    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, name_input);
        lv_group_focus_obj(name_input);
    }

    lv_obj_t* feedback = lv_label_create(dialog);
    lv_obj_set_style_text_color(feedback, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(feedback, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(feedback, LV_ALIGN_BOTTOM_MID, 0, -32);

    lv_obj_t* create_btn = lv_btn_create(dialog);
    lv_obj_set_size(create_btn, 100, 28);
    lv_obj_align(create_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(create_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(create_btn, 0, 0);
    lv_obj_t* cbl = lv_label_create(create_btn);
    lv_label_set_text(cbl, "Add");
    lv_obj_center(cbl);

    lv_obj_add_event_cb(create_btn, [](lv_event_t* e) {
        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
        lv_obj_t* fb  = (lv_obj_t*)lv_event_get_user_data(e);

        uint32_t n = lv_obj_get_child_cnt(dlg);
        lv_obj_t* name_in = nullptr;
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t* child = lv_obj_get_child(dlg, i);
            if (lv_obj_check_type(child, &lv_textarea_class)) {
                if (!name_in) name_in = child;
            }
        }

        const char* name = name_in ? lv_textarea_get_text(name_in) : "";

        if (!name[0]) { if (fb) lv_label_set_text(fb, "Enter a hashtag"); return; }

        // Validate channel name
        const char* val_reason = nullptr;
        if (!sigurdos::mesh::channel_name_valid(name, &val_reason)) {
            if (fb) lv_label_set_text(fb, val_reason);
            return;
        }

        bool ok = sigurdos::mesh::addHashtagChannel(name);
        if (ok) {
            lv_obj_del_async(dlg);
            if (g_channels_rebuild) g_channels_rebuild();
        } else {
            if (fb) lv_label_set_text(fb, "Invalid or full");
        }
    }, LV_EVENT_CLICKED, (void*)feedback);

    return dialog;
}

void channels_screen_show()
{
    lv_obj_t* scr = make_screen_full("Channels");

    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - 36);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    std::function<void()> rebuild = [list, &rebuild]() {
        lv_obj_clean(list);
        char names[8][37];
        int n = sigurdos::mesh::exportChannels(names, 8);
        if (n == 0) {
            auto* item = lv_list_add_btn(list, LV_SYMBOL_WARNING, "No channels joined");
            lv_obj_set_style_bg_color(item, lv_color_hex(BG_TERTIARY), 0);
        } else {
            for (int i = 0; i < n; i++) {
                lv_obj_t* row = lv_obj_create(list);
                lv_obj_set_size(row, LV_PCT(100), 36);
                lv_obj_set_style_bg_color(row,
                    lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_pad_all(row, 0, 0);
                lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

                lv_obj_t* icon = lv_label_create(row);
                lv_label_set_text(icon, LV_SYMBOL_LIST);
                lv_obj_set_style_text_color(icon, lv_color_hex(ACCENT), 0);
                lv_obj_set_style_text_font(icon, emoji_wrapped_montserrat_12, 0);
                lv_obj_set_style_pad_left(icon, 8, 0);

                lv_obj_t* name_l = lv_label_create(row);
                lv_label_set_text(name_l, names[i]);
                lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
                lv_obj_set_style_text_font(name_l, emoji_wrapped_montserrat_12, 0);
                lv_obj_set_flex_grow(name_l, 1);
                lv_obj_set_style_pad_left(name_l, 8, 0);

                // Delete button (hidden if only 1 channel remaining or for built-in Public)
                if (n > 1 && !sigurdos::mesh::isPublicChannelName(names[i])) {
                    lv_obj_t* del_btn = lv_btn_create(row);
                    lv_obj_set_size(del_btn, 28, 24);
                    lv_obj_set_style_bg_color(del_btn, lv_color_hex(ACCENT_RED), 0);
                    lv_obj_set_style_radius(del_btn, 0, 0);
                    lv_obj_set_style_border_width(del_btn, 0, 0);
                    lv_obj_set_style_pad_all(del_btn, 0, 0);
                    auto* dl = lv_label_create(del_btn);
                    lv_label_set_text(dl, LV_SYMBOL_CLOSE);
                    lv_obj_set_style_text_font(dl, emoji_wrapped_montserrat_10, 0);
                    lv_obj_center(dl);
                    lv_obj_set_style_pad_right(del_btn, 4, 0);
                    int ch_idx = i;
                    lv_obj_add_event_cb(del_btn, [](lv_event_t* e) {
                        int idx = (int)(intptr_t)lv_event_get_user_data(e);
                        lv_obj_t* scr = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
                        if (!scr) return;

                        // Read the channel name from the row's name label (child index 1)
                        lv_obj_t* row = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
                        const char* ch_name = "";
                        if (row && lv_obj_get_child_cnt(row) > 1) {
                            lv_obj_t* name_l = lv_obj_get_child(row, 1);
                            if (name_l) ch_name = lv_label_get_text(name_l);
                        }

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
                        snprintf(msg_buf, sizeof(msg_buf), "Delete channel #%s?", ch_name);
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
                            if (g_channels_rebuild) g_channels_rebuild();
                            lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
                        }, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
                    }, LV_EVENT_CLICKED, (void*)(intptr_t)ch_idx);
                }

                // Share via QR button (always shown)
                {
                    lv_obj_t* qr_btn = lv_btn_create(row);
                    lv_obj_set_size(qr_btn, 32, 24);
                    lv_obj_set_style_bg_color(qr_btn, lv_color_hex(ACCENT), 0);
                    lv_obj_set_style_radius(qr_btn, 0, 0);
                    lv_obj_set_style_border_width(qr_btn, 0, 0);
                    lv_obj_set_style_pad_all(qr_btn, 0, 0);
                    auto* ql = lv_label_create(qr_btn);
                    lv_label_set_text(ql, "QR");
                    lv_obj_set_style_text_font(ql, emoji_wrapped_montserrat_10, 0);
                    lv_obj_center(ql);
                    lv_obj_set_style_pad_right(qr_btn, 4, 0);
                    int qr_idx = i;
                    lv_obj_add_event_cb(qr_btn, [](lv_event_t* e) {
                        int idx = (int)(intptr_t)lv_event_get_user_data(e);
                        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                        lv_obj_t* row = lv_obj_get_parent(btn);
                        if (!row) return;
                        lv_obj_t* name_l = lv_obj_get_child(row, 1);
                        if (!name_l) return;
                        const char* ch_name = lv_label_get_text(name_l);
                        if (!ch_name || !ch_name[0]) return;

                        char secret_hex[65] = {0};
                        if (!sigurdos::mesh::getChannelSecretHex(idx, secret_hex, sizeof(secret_hex))) {
                            Serial.println("[qr] Failed to get channel secret");
                            return;
                        }
                        char uri[512];
                        char enc_name[96];
                        sigurdos::mesh::urlEncodeQueryValue(ch_name, enc_name, sizeof(enc_name));
                        snprintf(uri, sizeof(uri), "meshcore://channel/add?name=%s&secret=%s",
                                 enc_name, secret_hex);
                        sigurdos::app::qr_show("Share Channel", uri);
                    }, LV_EVENT_CLICKED, (void*)(intptr_t)qr_idx);
                }
            }
        }
    };

    g_channels_rebuild = rebuild;
    rebuild();

    lv_obj_t* add_btn = lv_btn_create(scr);
    lv_obj_set_size(add_btn, 140, 28);
    lv_obj_align(add_btn, LV_ALIGN_BOTTOM_MID, 0, -(BOT_BAR_H + DIVIDER_H + 4));
    lv_obj_set_style_bg_color(add_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(add_btn, 0, 0);
    lv_obj_t* al = lv_label_create(add_btn);
    lv_label_set_text(al, LV_SYMBOL_PLUS "  Add # Channel");
    lv_obj_set_style_text_font(al, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(al);

    lv_obj_add_event_cb(add_btn, [](lv_event_t* e) {
        lv_obj_t* s = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        channel_create_dialog(s);
    }, LV_EVENT_CLICKED, nullptr);

    // Null the rebuild callback when this screen is destroyed
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        g_channels_rebuild = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    show_screen(scr);
}

} // namespace sigurdos::ui
