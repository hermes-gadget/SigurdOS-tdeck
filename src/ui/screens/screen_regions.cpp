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
#include "../../mesh/regions.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <functional>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ════════════════════════════════════════════════════════
// Regions — flood scope region manager
// ════════════════════════════════════════════════════════
static std::function<void()> g_regions_rebuild = nullptr;

// Async wrapper — defers rebuild to next LVGL tick so callbacks
// don't operate on deleted widgets (use-after-free guard).
static void regions_rebuild_async(void*) {
    if (g_regions_rebuild) g_regions_rebuild();
}

static void regions_add_dialog(lv_obj_t* parent_scr) {
    auto dlg_sz = dialog_size(280, 180);
    lv_obj_t* dlg = lv_obj_create(parent_scr);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Add Region");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // Name field
    lv_obj_t* name_ta = lv_textarea_create(dlg);
    lv_obj_set_size(name_ta, dlg_sz.w - 16, 22);
    lv_obj_align(name_ta, LV_ALIGN_TOP_MID, 0, 28);
    lv_textarea_set_one_line(name_ta, true);
    lv_textarea_set_max_length(name_ta, 30);  // RegionEntry::name minus null
    lv_textarea_set_placeholder_text(name_ta, "#name or $name");
    lv_obj_set_style_bg_color(name_ta, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(name_ta, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(name_ta, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(name_ta, 2, 0);
    lv_obj_set_style_border_color(name_ta, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_radius(name_ta, 0, 0);
    apply_focus_style(name_ta);

    // Key field (hidden by default, shown for $private names)
    lv_obj_t* key_ta = lv_textarea_create(dlg);
    lv_obj_set_size(key_ta, dlg_sz.w - 16, 22);
    lv_obj_align(key_ta, LV_ALIGN_TOP_MID, 0, 56);
    lv_textarea_set_one_line(key_ta, true);
    lv_textarea_set_max_length(key_ta, 24);  // 16-byte key in base64
    lv_textarea_set_accepted_chars(key_ta,
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=");
    lv_textarea_set_placeholder_text(key_ta, "24-char base64 key");
    lv_obj_set_style_bg_color(key_ta, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(key_ta, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(key_ta, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(key_ta, 2, 0);
    lv_obj_set_style_border_color(key_ta, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_radius(key_ta, 0, 0);
    apply_focus_style(key_ta);
    lv_obj_add_flag(key_ta, LV_OBJ_FLAG_HIDDEN);

    // Show key field when name starts with '$'
    lv_obj_add_event_cb(name_ta, [](lv_event_t* e) {
        lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* dlg = lv_obj_get_parent(ta);
        if (!dlg) return;
        // key_ta is the second textarea child of the dialog
        lv_obj_t* kta = nullptr;
        uint32_t cnt = lv_obj_get_child_cnt(dlg);
        int found = 0;
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t* child = lv_obj_get_child(dlg, i);
            if (lv_obj_check_type(child, &lv_textarea_class)) {
                if (found == 1) { kta = child; break; }
                found++;
            }
        }
        if (!kta) return;
        const char* text = lv_textarea_get_text(ta);
        if (text && text[0] == '$') {
            lv_obj_clear_flag(kta, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(kta, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Error label
    lv_obj_t* err_lbl = lv_label_create(dlg);
    lv_obj_set_style_text_color(err_lbl, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(err_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_width(err_lbl, dlg_sz.w - 16);
    lv_obj_align(err_lbl, LV_ALIGN_TOP_MID, 0, 82);

    // Apply button
    lv_obj_t* apply_btn = lv_btn_create(dlg);
    lv_obj_set_size(apply_btn, 80, 24);
    lv_obj_align(apply_btn, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    lv_obj_set_style_bg_color(apply_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(apply_btn, 0, 0);
    lv_obj_t* al = lv_label_create(apply_btn);
    lv_label_set_text(al, "Apply");
    lv_obj_center(al);

    struct RegionDlgCtx { lv_obj_t* name_ta; lv_obj_t* key_ta; lv_obj_t* err_lbl; lv_obj_t* dlg; };
    RegionDlgCtx* ctx = new RegionDlgCtx{name_ta, key_ta, err_lbl, dlg};

    lv_obj_add_event_cb(apply_btn, [](lv_event_t* e) {
        RegionDlgCtx* c = (RegionDlgCtx*)lv_event_get_user_data(e);
        const char* name = lv_textarea_get_text(c->name_ta);
        if (!name || !name[0]) {
            lv_label_set_text(c->err_lbl, "Name is required");
            return;
        }
        const char* key = nullptr;
        char key_buf[33] = {0};
        if (name[0] == '$') {
            const char* ktext = lv_textarea_get_text(c->key_ta);
            if (!ktext || strlen(ktext) < 1) {
                lv_label_set_text(c->err_lbl, "Key is required for $private");
                return;
            }
            snprintf(key_buf, sizeof(key_buf), "%s", ktext);
            key = key_buf;
        }
        lv_label_set_text(c->err_lbl, "");
        bool ok = sigurdos::mesh::addRegion(name, key);
        if (!ok) {
            lv_label_set_text(c->err_lbl, "Failed to add region");
            return;
        }
        // Refresh the list
        lv_async_call(regions_rebuild_async, nullptr);
        // Re-read active region to update subtitle
        const char* active = sigurdos::mesh::getActiveRegion();
        lv_obj_t* scr = lv_obj_get_screen(c->dlg);
        if (scr) {
            uint32_t cnt2 = lv_obj_get_child_cnt(scr);
            for (uint32_t i = 0; i < cnt2; i++) {
                lv_obj_t* ch = lv_obj_get_child(scr, i);
                if (lv_obj_check_type(ch, &lv_label_class)) {
                    const char* txt = lv_label_get_text(ch);
                    if (txt && strncmp(txt, "Active:", 7) == 0) {
                        char abuf[64];
                        if (!active || active[0] == '\0') {
                            snprintf(abuf, sizeof(abuf), "Active: Public (unscoped)");
                        } else {
                            snprintf(abuf, sizeof(abuf), "Active: %s", active);
                        }
                        lv_label_set_text(ch, abuf);
                        break;
                    }
                }
            }
        }
        lv_obj_del_async(c->dlg);
    }, LV_EVENT_CLICKED, ctx);

    // Clean up dialog context when LVGL actually destroys the dialog
    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        RegionDlgCtx* c = (RegionDlgCtx*)lv_event_get_user_data(e);
        delete c;
    }, LV_EVENT_DELETE, ctx);

    // Cancel button
    lv_obj_t* cancel_btn = lv_btn_create(dlg);
    lv_obj_set_size(cancel_btn, 72, 24);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_radius(cancel_btn, 0, 0);
    lv_obj_t* cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ce) {
        RegionDlgCtx* c = (RegionDlgCtx*)lv_event_get_user_data(ce);
        lv_obj_del_async(c->dlg);
    }, LV_EVENT_CLICKED, ctx);

    // Focus name field
    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, name_ta);
        lv_group_focus_obj(name_ta);
    }
}

void regions_screen_show()
{
    lv_obj_t* scr = make_screen_full("Regions");

    // Active region subtitle
    const char* active = sigurdos::mesh::getActiveRegion();
    if (!active) active = "";

    lv_obj_t* active_lbl = lv_label_create(scr);
    if (active[0] == '\0') {
        lv_label_set_text(active_lbl, "Active: Public (unscoped)");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Active: %s", active);
        lv_label_set_text(active_lbl, buf);
    }
    lv_obj_set_style_text_color(active_lbl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(active_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(active_lbl, LV_ALIGN_TOP_LEFT, 8, CONTENT_Y + 4);

    // List container for regions
    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - 44);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 20);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    // Rebuild function
    std::function<void()> rebuild = [list, scr]() {
        lv_obj_clean(list);

        // "Public (unscoped)" entry — tap to set wildcard active
        {
            lv_obj_t* row = lv_obj_create(list);
            lv_obj_set_size(row, LV_PCT(100), 30);
            lv_obj_set_style_bg_color(row, lv_color_hex(BG_TERTIARY), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* icon = lv_label_create(row);
            const char* active_rgn = sigurdos::mesh::getActiveRegion();
            if (active_rgn && active_rgn[0] == '\0') {
                lv_label_set_text(icon, LV_SYMBOL_OK);
                lv_obj_set_style_text_color(icon, lv_color_hex(ACCENT_GREEN), 0);
            } else {
                lv_label_set_text(icon, " ");
                lv_obj_set_style_text_color(icon, lv_color_hex(TEXT_MUTED), 0);
            }
            lv_obj_set_style_text_font(icon, emoji_wrapped_montserrat_12, 0);
            lv_obj_set_style_pad_left(icon, 8, 0);

            lv_obj_t* name_l = lv_label_create(row);
            lv_label_set_text(name_l, "Public (unscoped)");
            lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(name_l, emoji_wrapped_montserrat_12, 0);
            lv_obj_set_flex_grow(name_l, 1);
            lv_obj_set_style_pad_left(name_l, 8, 0);

            lv_obj_add_event_cb(row, [](lv_event_t* e) {
                sigurdos::mesh::setActiveRegion("");
                lv_async_call(regions_rebuild_async, nullptr);
                // Update active label on parent screen
                lv_obj_t* r = (lv_obj_t*)lv_event_get_target(e);
                lv_obj_t* s = lv_obj_get_screen(r);
                if (s) {
                    uint32_t cnt = lv_obj_get_child_cnt(s);
                    for (uint32_t i = 0; i < cnt; i++) {
                        lv_obj_t* ch = lv_obj_get_child(s, i);
                        if (lv_obj_check_type(ch, &lv_label_class)) {
                            const char* txt = lv_label_get_text(ch);
                            if (txt && strncmp(txt, "Active:", 7) == 0) {
                                lv_label_set_text(ch, "Active: Public (unscoped)");
                                break;
                            }
                        }
                    }
                }
            }, LV_EVENT_CLICKED, nullptr);
        }

        // List saved regions
        sigurdos::mesh::RegionInfo regions[32];
        int n = sigurdos::mesh::listRegions(regions, 32);
        const char* active_rgn = sigurdos::mesh::getActiveRegion();
        if (!active_rgn) active_rgn = "";

        for (int i = 0; i < n; i++) {
            lv_obj_t* row = lv_obj_create(list);
            lv_obj_set_size(row, LV_PCT(100), 30);
            lv_obj_set_style_bg_color(row,
                lv_color_hex(i % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // Checkmark if active
            lv_obj_t* icon = lv_label_create(row);
            bool is_active = (strcmp(regions[i].name, active_rgn) == 0);
            if (is_active) {
                lv_label_set_text(icon, LV_SYMBOL_OK);
                lv_obj_set_style_text_color(icon, lv_color_hex(ACCENT_GREEN), 0);
            } else {
                lv_label_set_text(icon, " ");
                lv_obj_set_style_text_color(icon, lv_color_hex(TEXT_MUTED), 0);
            }
            lv_obj_set_style_text_font(icon, emoji_wrapped_montserrat_12, 0);
            lv_obj_set_style_pad_left(icon, 8, 0);

            // Region name
            lv_obj_t* name_l = lv_label_create(row);
            lv_label_set_text(name_l, regions[i].name);
            lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(name_l, emoji_wrapped_montserrat_12, 0);
            lv_obj_set_flex_grow(name_l, 1);
            lv_obj_set_style_pad_left(name_l, 8, 0);

            // Tap to set as active
            lv_obj_add_event_cb(row, [](lv_event_t* e) {
                lv_obj_t* r = (lv_obj_t*)lv_event_get_target(e);
                lv_obj_t* nl = lv_obj_get_child(r, 1);
                if (!nl) return;
                const char* rname = lv_label_get_text(nl);
                if (!rname || !rname[0]) return;

                sigurdos::mesh::setActiveRegion(rname);

                lv_async_call(regions_rebuild_async, nullptr);

                // Update active label
                lv_obj_t* s = lv_obj_get_screen(r);
                if (s) {
                    uint32_t cnt = lv_obj_get_child_cnt(s);
                    for (uint32_t i2 = 0; i2 < cnt; i2++) {
                        lv_obj_t* ch = lv_obj_get_child(s, i2);
                        if (lv_obj_check_type(ch, &lv_label_class)) {
                            const char* txt = lv_label_get_text(ch);
                            if (txt && strncmp(txt, "Active:", 7) == 0) {
                                char abuf[64];
                                snprintf(abuf, sizeof(abuf), "Active: %s", rname);
                                lv_label_set_text(ch, abuf);
                                break;
                            }
                        }
                    }
                }
            }, LV_EVENT_CLICKED, nullptr);

            // Delete button
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

            lv_obj_add_event_cb(del_btn, [](lv_event_t* e) {
                lv_obj_t* row2 = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
                if (!row2) return;
                // Get name from row's name label (child index 1)
                lv_obj_t* nl = lv_obj_get_child(row2, 1);
                if (!nl) return;
                const char* rname = lv_label_get_text(nl);
                if (!rname || !rname[0]) return;

                sigurdos::mesh::removeRegion(rname);
                lv_async_call(regions_rebuild_async, nullptr);

                // Update active label
                const char* active2 = sigurdos::mesh::getActiveRegion();
                lv_obj_t* s = lv_obj_get_screen(row2);
                if (s) {
                    uint32_t cnt = lv_obj_get_child_cnt(s);
                    for (uint32_t i3 = 0; i3 < cnt; i3++) {
                        lv_obj_t* ch = lv_obj_get_child(s, i3);
                        if (lv_obj_check_type(ch, &lv_label_class)) {
                            const char* txt = lv_label_get_text(ch);
                            if (txt && strncmp(txt, "Active:", 7) == 0) {
                                char abuf[64];
                                if (!active2 || active2[0] == '\0') {
                                    snprintf(abuf, sizeof(abuf), "Active: Public (unscoped)");
                                } else {
                                    snprintf(abuf, sizeof(abuf), "Active: %s", active2);
                                }
                                lv_label_set_text(ch, abuf);
                                break;
                            }
                        }
                    }
                }
            }, LV_EVENT_CLICKED, nullptr);
        }
    };

    g_regions_rebuild = rebuild;
    rebuild();

    // "Sync from channels" button — auto-creates #regions from #channels
    lv_obj_t* sync_btn = lv_btn_create(scr);
    lv_obj_set_size(sync_btn, 120, 28);
    lv_obj_align(sync_btn, LV_ALIGN_BOTTOM_LEFT, 8, -(BOT_BAR_H + DIVIDER_H + 4));
    lv_obj_set_style_bg_color(sync_btn, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_radius(sync_btn, 0, 0);
    lv_obj_set_style_border_width(sync_btn, 2, 0);
    lv_obj_set_style_border_color(sync_btn, lv_color_hex(ACCENT), 0);
    lv_obj_t* sl = lv_label_create(sync_btn);
    lv_label_set_text(sl, LV_SYMBOL_REFRESH "  Sync");
    lv_obj_set_style_text_font(sl, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_text_color(sl, lv_color_hex(ACCENT), 0);
    lv_obj_center(sl);

    lv_obj_add_event_cb(sync_btn, [](lv_event_t* e) {
        sigurdos::mesh::syncRegionsFromChannels();
        lv_async_call(regions_rebuild_async, nullptr);
        // Update active label
        lv_obj_t* s2 = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        if (s2) {
            const char* active3 = sigurdos::mesh::getActiveRegion();
            uint32_t cnt = lv_obj_get_child_cnt(s2);
            for (uint32_t i = 0; i < cnt; i++) {
                lv_obj_t* ch = lv_obj_get_child(s2, i);
                if (lv_obj_check_type(ch, &lv_label_class)) {
                    const char* txt = lv_label_get_text(ch);
                    if (txt && strncmp(txt, "Active:", 7) == 0) {
                        char abuf[64];
                        if (!active3 || active3[0] == '\0') {
                            snprintf(abuf, sizeof(abuf), "Active: Public (unscoped)");
                        } else {
                            snprintf(abuf, sizeof(abuf), "Active: %s", active3);
                        }
                        lv_label_set_text(ch, abuf);
                        break;
                    }
                }
            }
        }
    }, LV_EVENT_CLICKED, nullptr);

    // "Add Region" button
    lv_obj_t* add_btn = lv_btn_create(scr);
    lv_obj_set_size(add_btn, 120, 28);
    lv_obj_align(add_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -(BOT_BAR_H + DIVIDER_H + 4));
    lv_obj_set_style_bg_color(add_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(add_btn, 0, 0);
    lv_obj_t* al = lv_label_create(add_btn);
    lv_label_set_text(al, LV_SYMBOL_PLUS "  Add Region");
    lv_obj_set_style_text_font(al, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(al);

    lv_obj_add_event_cb(add_btn, [](lv_event_t* e) {
        lv_obj_t* s = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        if (s) regions_add_dialog(s);
    }, LV_EVENT_CLICKED, nullptr);

    // Null the rebuild callback when this screen is destroyed
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        g_regions_rebuild = nullptr;
    }, LV_EVENT_DELETE, nullptr);

    show_screen(scr);
}

} // namespace sigurdos::ui
