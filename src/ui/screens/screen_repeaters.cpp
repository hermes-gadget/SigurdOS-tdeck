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
#include "../navigation.h"
#include "../theme.h"
#include "../responsive.h"
#include "../contact_paging.h"
#include "../chat_screen.h"
#include "../../hal/prefs.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../fonts/emoji_font.h"
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

static int g_repeaters_page = 0;

static int compare_contacts_by_last_seen_desc(const void* a, const void* b)
{
    const auto* ca = static_cast<const sigurdos::mesh::ContactInfo*>(a);
    const auto* cb = static_cast<const sigurdos::mesh::ContactInfo*>(b);
    if (ca->last_seen < cb->last_seen) return 1;
    if (ca->last_seen > cb->last_seen) return -1;
    return strcmp(ca->name, cb->name);
}

// ════════════════════════════════════════════════════════
// Repeaters — infrastructure relay nodes only
// ════════════════════════════════════════════════════════
void repeaters_screen_show()
{
    lv_obj_t* scr = make_screen_full("Repeaters");

    sigurdos::mesh::ContactInfo* contacts =
        new(std::nothrow) sigurdos::mesh::ContactInfo[MAX_CONTACTS];
    if (!contacts) {
        show_contact_memory_error(scr);
        show_screen(scr);
        return;
    }

    int total = sigurdos::mesh::exportContactsFull(contacts, MAX_CONTACTS);
    if (total < 0) total = 0;
    if (total > MAX_CONTACTS) total = MAX_CONTACTS;

    // Filter to repeaters only
    int n = 0;
    for (int i = 0; i < total; i++) {
        if (contacts[i].type == ADV_TYPE_REPEATER) {
            if (n < i) contacts[n] = contacts[i];
            n++;
        }
    }

    // Sort by last_seen descending (most recently heard first) before paging.
    if (n > 1) qsort(contacts, n, sizeof(contacts[0]), compare_contacts_by_last_seen_desc);

    if (n == 0) {
        g_repeaters_page = 0;
        lv_obj_t* info = lv_label_create(scr);
        lv_label_set_text(info,
            "No repeaters found.\n\n"
            "Repeaters are infrastructure relay nodes\n"
            "that extend the range of the mesh network.");
        lv_obj_set_width(info, CONTENT_W);
        lv_obj_set_style_pad_left(info, 8, 0);
        lv_obj_set_style_pad_right(info, 8, 0);
        lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(info, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(info, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y + 4);
        delete[] contacts;
        show_screen(scr);
        return;
    }

    int pages = contact_page_count(n);
    g_repeaters_page = contact_clamp_page(g_repeaters_page, n);
    int page_start = contact_page_start(g_repeaters_page, n);
    int page_end = contact_page_end(g_repeaters_page, n);
    int page_n = page_end - page_start;
    // Heap-allocate the per-page buffer rather than placing ~1.8 KB on the
    // stack — this runs in the LVGL task context (navigation + pager clicks).
    sigurdos::mesh::ContactInfo* page_contacts =
        new(std::nothrow) sigurdos::mesh::ContactInfo[CONTACT_LIST_PAGE_SIZE];
    if (!page_contacts) {
        delete[] contacts;
        show_contact_memory_error(scr);
        show_screen(scr);
        return;
    }
    for (int i = 0; i < page_n; i++) {
        page_contacts[i] = contacts[page_start + i];
    }
    delete[] contacts;
    contacts = page_contacts;

    int list_y = CONTENT_Y;
    int list_h = CONTENT_H;
    if (pages > 1) {
        add_contact_pager(scr, g_repeaters_page, pages, n,
            [](lv_event_t*) {
                if (g_repeaters_page > 0) g_repeaters_page--;
                repeaters_screen_show();
            },
            [](lv_event_t*) {
                g_repeaters_page++;
                repeaters_screen_show();
            });
        list_y += 28;
        list_h -= 28;
    }

    lv_obj_t* list = lv_obj_create(scr);
    if (!list) {
        delete[] page_contacts;
        show_contact_memory_error(scr);
        show_screen(scr);
        return;
    }
    lv_obj_set_size(list, LV_PCT(100), list_h);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, list_y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    static constexpr int ROW_H = 32;

    for (int i = 0; i < page_n; i++) {
        auto& c = contacts[i];

        lv_obj_t* row = lv_obj_create(list);
        if (!row) break;
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_style_bg_color(row,
            lv_color_hex((page_start + i) % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(ACCENT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        // Icon
        lv_obj_t* icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(icon, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 6, 0);

        // Name
        lv_obj_t* name_l = lv_label_create(row);
        lv_label_set_text(name_l, c.name);
        lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(name_l, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(name_l, LV_ALIGN_LEFT_MID, 28, 0);

        // Store name for click handler
        char* name_dup = strdup(c.name);
        lv_obj_set_user_data(row, name_dup);

        // RSSI
        char rssi_buf[12];
        snprintf(rssi_buf, sizeof(rssi_buf), "%ddBm", c.rssi);
        lv_obj_t* rssi_l = lv_label_create(row);
        lv_label_set_text(rssi_l, rssi_buf);
        lv_obj_set_style_text_color(rssi_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(rssi_l, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(rssi_l, LV_ALIGN_RIGHT_MID, -6, 0);

        // SNR
        char snr_buf[12];
        snprintf(snr_buf, sizeof(snr_buf), "%.1fdB", c.snr);
        lv_obj_t* snr_l = lv_label_create(row);
        lv_label_set_text(snr_l, snr_buf);
        lv_obj_set_style_text_color(snr_l, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(snr_l, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(snr_l, LV_ALIGN_RIGHT_MID, -56, 0);

        // Click handler — open dedicated repeater detail with login flow
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(target);
            if (name) {
                navigate_to_repeater_detail(name);
            }
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    delete[] page_contacts;
    show_screen(scr);
}

// ── Repeater command helpers ───────────────────────────
// Sends an admin CLI command to a logged-in repeater/server and pushes
// a confirmation message into the message queue. The actual response
// arrives later as a chat message from the server.
static void repeater_send(const char* contact_name, const char* cmd, const char* fmt) {
    if (!contact_name || !cmd || !cmd[0]) return;
    sigurdos::mesh::sendCommand(contact_name, cmd);
    char buf[80];
    snprintf(buf, sizeof(buf), fmt, cmd);
    sigurdos::mesh::mesh_v2_queue_push("System", "", buf, 0, 0.0f);
}

// Generic input dialog — shows a text entry box and sends
// <cmd_prefix><user_input> as an admin CLI command to the logged-in server.
static void repeater_input_dialog(const char* contact_name,
                                  const char* title,
                                  const char* hint,
                                  const char* cmd_prefix,
                                  bool password_mode)
{
    static constexpr size_t MAX_REPEATER_CLI_COMMAND_LENGTH = 63;
    if (!contact_name || !cmd_prefix) return;
    const size_t prefix_len = strlen(cmd_prefix);
    if (prefix_len >= MAX_REPEATER_CLI_COMMAND_LENGTH) return;
    lv_obj_t* scr = lv_obj_get_screen(lv_scr_act());
    auto dlg_sz = dialog_size(260, 124);
    lv_obj_t* dlg = lv_obj_create(scr);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    // Title
    lv_obj_t* ttl = lv_label_create(dlg);
    lv_label_set_text(ttl, title);
    lv_obj_set_style_text_color(ttl, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(ttl, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 4);

    // Hint
    lv_obj_t* hnt = lv_label_create(dlg);
    lv_label_set_text(hnt, hint);
    lv_obj_set_style_text_color(hnt, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(hnt, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(hnt, LV_ALIGN_TOP_MID, 0, 24);

    // Textarea
    lv_obj_t* ta = lv_textarea_create(dlg);
    lv_obj_set_size(ta, dlg_sz.w - 16, 28);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 38);
    lv_textarea_set_placeholder_text(ta, "Value (Enter to send)");
    lv_textarea_set_password_mode(ta, password_mode);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta,
        MAX_REPEATER_CLI_COMMAND_LENGTH - prefix_len);
    lv_obj_set_style_bg_color(ta, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_group_t* g = lv_group_get_default();
    if (g) lv_group_focus_obj(ta);

    // Cancel button
    lv_obj_t* cancel_btn = lv_btn_create(dlg);
    lv_obj_set_size(cancel_btn, 80, 24);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, -4);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_radius(cancel_btn, 0, 0);
    lv_obj_t* cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* ce) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
    }, LV_EVENT_CLICKED, nullptr);

    // Send button
    struct RiData { char* name; const char* prefix; lv_obj_t* ta; };
    RiData* rd = new RiData{strdup(contact_name), strdup(cmd_prefix), ta};

    lv_obj_t* send_btn = lv_btn_create(dlg);
    lv_obj_set_size(send_btn, 80, 24);
    lv_obj_align(send_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(send_btn, 0, 0);
    lv_obj_t* sb = lv_label_create(send_btn);
    lv_label_set_text(sb, "Send");
    lv_obj_center(sb);
    lv_obj_set_style_text_color(sb, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_user_data(send_btn, rd);

    lv_obj_add_event_cb(send_btn, [](lv_event_t* le) {
        RiData* d = (RiData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(le));
        if (d && d->name && d->prefix && d->ta) {
            const char* val = lv_textarea_get_text(d->ta);
            if (val && val[0]) {
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "%s%s", d->prefix, val);
                repeater_send(d->name, cmd, "Sent: %s");
            }
        }
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(le)));
    }, LV_EVENT_CLICKED, nullptr);

    // Enter key handler
    lv_obj_add_event_cb(ta, [](lv_event_t* te) {
        lv_obj_t* t = (lv_obj_t*)lv_event_get_target(te);
        uint32_t key = lv_event_get_key(te);
        if (key == LV_KEY_ENTER) {
            const char* val = lv_textarea_get_text(t);
            if (val && val[0]) {
                lv_obj_t* parent = lv_obj_get_parent(t);
                if (parent) {
                    uint32_t c = lv_obj_get_child_cnt(parent);
                    for (uint32_t i = 0; i < c; i++) {
                        lv_obj_t* child = lv_obj_get_child(parent, i);
                        if (child && lv_obj_check_type(child, &lv_button_class)) {
                            RiData* data = (RiData*)lv_obj_get_user_data(child);
                            if (data) {
                                lv_obj_send_event(child, LV_EVENT_CLICKED, nullptr);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }, LV_EVENT_KEY, nullptr);

    // Cleanup
    lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
        RiData* d = (RiData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de));
        if (d) {
            free(d->name);
            free((void*)d->prefix);
            delete d;
        }
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_set_user_data(dlg, rd);
}

// ════════════════════════════════════════════════════════
// Repeater Detail — login-first, then settings-style sections
// skip_login=true → show post-login view (used by login-polling timer)
// skip_login=false (default) → always show pre-login + Login button
// ════════════════════════════════════════════════════════
void repeater_detail_screen_show(const char* contact_name, bool skip_login)
{
    if (!contact_name || !contact_name[0]) return;

    // Look up the contact first (need type for screen title). Avoid copying
    // the full 350-contact table just to open one repeater detail page.
    sigurdos::mesh::ContactInfo target_info{};
    const sigurdos::mesh::ContactInfo* target = nullptr;
    if (sigurdos::mesh::getContactByName(contact_name, &target_info)) {
        target = &target_info;
    }

    // Determine screen title from contact type
    const char* screen_title = "Repeater";
    if (target) {
        switch (target->type) {
            case ADV_TYPE_REPEATER: screen_title = "Repeater"; break;
            case ADV_TYPE_ROOM:     screen_title = "Room Server"; break;
            default:                screen_title = "Node"; break;
        }
    }

    lv_obj_t* scr = make_screen_full(screen_title);

    if (!target) {
        lv_obj_t* err = lv_label_create(scr);
        lv_label_set_text(err, "Contact not found");
        lv_obj_set_style_text_color(err, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_text_font(err, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(err, LV_ALIGN_CENTER, 0, 0);
        show_screen(scr);
        return;
    }

    uint8_t login_st = sigurdos::mesh::getLoginStatus(contact_name);
    bool is_fav = sigurdos::mesh::isContactFavourite(contact_name);

    // ── Content list ──────────────────────────────────
    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    // Ensure flex column layout for proper vertical stacking
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    int row = 0;

    // skip_login=false: ALWAYS show pre-login (don't trust cached login state)
    // skip_login=true:  show post-login only if actually logged in (timer path)
    if (!skip_login || login_st != LOGIN_STATUS_OK) {
        // ── Pre-login view (compact) ────────────────────────
        // Content area is ~198px tall. Items are sized explicitly
        // to fit without overlapping or squishing.

        // Title row (28px) — repeater name + fav toggle
        lv_obj_t* title_row = lv_obj_create(list);
        lv_obj_set_size(title_row, LV_PCT(100), 28);
        lv_obj_set_style_bg_color(title_row, lv_color_hex(BG_PRIMARY), 0);
        lv_obj_set_style_bg_opa(title_row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(title_row, 0, 0);
        lv_obj_set_style_radius(title_row, 0, 0);
        lv_obj_set_style_pad_all(title_row, 0, 0);
        lv_obj_clear_flag(title_row, LV_OBJ_FLAG_CLICKABLE);
        {
            lv_obj_t* title_lbl = lv_label_create(title_row);
            lv_label_set_text(title_lbl, contact_name);
            lv_obj_set_style_text_color(title_lbl, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(title_lbl, emoji_wrapped_montserrat_14, 0);
            lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 8, 0);
        }
        // Favourite star toggle
        {
            char* fav_name = strdup(contact_name);
            lv_obj_t* fav_btn = lv_btn_create(title_row);
            lv_obj_set_size(fav_btn, 30, 24);
            lv_obj_align(fav_btn, LV_ALIGN_RIGHT_MID, -4, 0);
            lv_obj_set_style_bg_color(fav_btn, lv_color_hex(BG_INPUT), 0);
            lv_obj_set_style_radius(fav_btn, 0, 0);
            lv_obj_set_style_border_width(fav_btn, 0, 0);
            lv_obj_set_style_pad_all(fav_btn, 0, 0);
            lv_obj_t* fav_icon = lv_label_create(fav_btn);
            lv_label_set_text(fav_icon, is_fav ? LV_SYMBOL_CLOSE : LV_SYMBOL_OK);
            lv_obj_set_style_text_color(fav_icon,
                lv_color_hex(is_fav ? ACCENT : TEXT_MUTED), 0);
            lv_obj_center(fav_icon);
            lv_obj_set_user_data(fav_btn, fav_name);
            lv_obj_add_event_cb(fav_btn, [](lv_event_t* e) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(btn);
                if (name) {
                    bool cur = sigurdos::mesh::isContactFavourite(name);
                    sigurdos::mesh::setContactFavourite(name, !cur);

                    repeater_detail_screen_show(name, true);

                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(fav_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
        }
        row++;

        // Info rows (compact, no section header)
        {
            char snr_str[16], rssi_str[16], type_str[24];
            snprintf(snr_str, sizeof(snr_str), "%.1f dB", target->snr);
            snprintf(rssi_str, sizeof(rssi_str), "%d dBm", target->rssi);
            switch (target->type) {
                case ADV_TYPE_REPEATER: strncpy(type_str, "Repeater", sizeof(type_str) - 1); break;
                case ADV_TYPE_ROOM:     strncpy(type_str, "Room Server", sizeof(type_str) - 1); break;
                default:                strncpy(type_str, "Unknown", sizeof(type_str) - 1); break;
            }
            type_str[sizeof(type_str) - 1] = '\0';
            // Type row: left label, right value
            lv_obj_t* tr = lv_obj_create(list);
            lv_obj_set_size(tr, LV_PCT(100), 24);
            lv_obj_set_style_bg_color(tr, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(tr, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(tr, 0, 0);
            lv_obj_set_style_radius(tr, 0, 0);
            lv_obj_set_style_pad_all(tr, 0, 0);
            lv_obj_clear_flag(tr, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t* tl = lv_label_create(tr);
            lv_label_set_text(tl, "  Type");
            lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(tl, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(tl, LV_ALIGN_LEFT_MID, 4, 0);
            lv_obj_t* tv = lv_label_create(tr);
            lv_label_set_text(tv, type_str);
            lv_obj_set_style_text_color(tv, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_text_font(tv, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(tv, LV_ALIGN_RIGHT_MID, -4, 0);
            // SNR + RSSI on one row
            lv_obj_t* sr = lv_obj_create(list);
            lv_obj_set_size(sr, LV_PCT(100), 24);
            lv_obj_set_style_bg_color(sr, lv_color_hex(row % 2 == 1 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(sr, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(sr, 0, 0);
            lv_obj_set_style_radius(sr, 0, 0);
            lv_obj_set_style_pad_all(sr, 0, 0);
            lv_obj_clear_flag(sr, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t* snr_l = lv_label_create(sr);
            lv_label_set_text(snr_l, "  SNR");
            lv_obj_set_style_text_color(snr_l, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(snr_l, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(snr_l, LV_ALIGN_LEFT_MID, 4, 0);
            lv_obj_t* snr_v = lv_label_create(sr);
            lv_label_set_text(snr_v, snr_str);
            lv_obj_set_style_text_color(snr_v, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(snr_v, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(snr_v, LV_ALIGN_LEFT_MID, 40, 0);
            lv_obj_t* rssi_l = lv_label_create(sr);
            lv_label_set_text(rssi_l, "RSSI");
            lv_obj_set_style_text_color(rssi_l, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(rssi_l, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(rssi_l, LV_ALIGN_LEFT_MID, 120, 0);
            lv_obj_t* rssi_v = lv_label_create(sr);
            lv_label_set_text(rssi_v, rssi_str);
            lv_obj_set_style_text_color(rssi_v, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(rssi_v, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(rssi_v, LV_ALIGN_LEFT_MID, 158, 0);
            row += 2;
        }

        // Login status row
        {
            const char* login_text = "Not logged in";
            uint32_t login_color = TEXT_SECONDARY;
            switch (login_st) {

                case LOGIN_STATUS_OK:     login_text = "Logged in";      login_color = ACCENT_GREEN; break;

                case LOGIN_STATUS_PENDING: login_text = "Login pending..."; login_color = ACCENT; break;
                case LOGIN_STATUS_FAILED:  login_text = "Login failed";     login_color = ACCENT_RED; break;
                case LOGIN_STATUS_TIMEOUT: login_text = "Login timed out";  login_color = ACCENT_RED; break;
                case LOGIN_STATUS_DROPPED: login_text = "Connection dropped"; login_color = ACCENT_RED; break;
            }
            lv_obj_t* lr = lv_obj_create(list);
            lv_obj_set_size(lr, LV_PCT(100), 24);
            lv_obj_set_style_bg_color(lr, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(lr, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(lr, 0, 0);
            lv_obj_set_style_radius(lr, 0, 0);
            lv_obj_set_style_pad_all(lr, 0, 0);
            lv_obj_clear_flag(lr, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t* login_l = lv_label_create(lr);
            lv_label_set_text(login_l, "  Login");
            lv_obj_set_style_text_color(login_l, lv_color_hex(TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(login_l, emoji_wrapped_montserrat_10, 0);
            lv_obj_align(login_l, LV_ALIGN_LEFT_MID, 4, 0);
            lv_obj_t* login_v = lv_label_create(lr);
            lv_label_set_text(login_v, login_text);
            lv_obj_set_style_text_color(login_v, lv_color_hex(login_color), 0);
            lv_obj_set_style_text_font(login_v, emoji_wrapped_montserrat_10, 0);
            if (login_st == LOGIN_STATUS_FAILED ||
                login_st == LOGIN_STATUS_TIMEOUT ||
                login_st == LOGIN_STATUS_DROPPED) {
                // Terminal session failures remain visible as a compact badge
                // after the management view is rebuilt. In particular, a
                // failed keep-alive must not look like an ordinary logout.
                apply_pixel_badge(login_v);
                lv_obj_set_style_bg_color(login_v, lv_color_hex(ACCENT_RED), 0);
                lv_obj_set_style_border_color(login_v, lv_color_hex(ACCENT_RED), 0);
            }
            lv_obj_align(login_v, LV_ALIGN_RIGHT_MID, -4, 0);
            row++;
        }

        // Spacer (flex grow — fills remaining space)
        lv_obj_t* spacer = lv_obj_create(list);
        lv_obj_set_size(spacer, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(spacer, 1);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
        lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
        row++;

        // Login button (prominent)
        if (login_st != LOGIN_STATUS_PENDING) {
            char* li_name = strdup(contact_name);
            lv_obj_t* login_btn = lv_btn_create(list);
            lv_obj_set_size(login_btn, LV_PCT(100), 32);
            lv_obj_set_style_bg_color(login_btn, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_bg_opa(login_btn, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(login_btn, 0, 0);
            lv_obj_set_style_border_width(login_btn, 0, 0);
            lv_obj_t* login_lbl = lv_label_create(login_btn);
            lv_label_set_text(login_lbl, LV_SYMBOL_DIRECTORY "  Login");
            lv_obj_set_style_text_color(login_lbl, lv_color_hex(BG_PRIMARY), 0);
            lv_obj_center(login_lbl);
            lv_obj_set_user_data(login_btn, li_name);
            lv_obj_add_event_cb(login_btn, [](lv_event_t* e) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(btn);
                if (name) show_login_password_dialog(name);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(login_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
            row++;
        }

    } else {
        // ── Post-login: full management UI ─────────────

        // ── Section helpers ────────────────────────────
        auto sec_header = [&](const char* name) {
            lv_obj_t* h = lv_list_add_btn(list, nullptr, name);
            lv_obj_set_style_bg_color(h, lv_color_hex(BG_TERTIARY), 0);
            lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(h, lv_color_hex(TEXT_MUTED), 0);
            lv_obj_set_style_text_font(h, emoji_wrapped_montserrat_10, 0);
            lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE);
            row++;
        };

        // Set-value button: opens an input dialog and sends <prefix> + user input
        struct SetCtx { char* name; char prefix[24]; char title[24]; char hint[32]; bool pw; };
        auto add_set = [&](const char* icon_lbl, const char* title, const char* hint, const char* prefix, bool pw) {
            auto* ctx = new SetCtx();
            ctx->name = strdup(contact_name);
            ctx->pw = pw;
            strncpy(ctx->prefix, prefix, sizeof(ctx->prefix)-1);
            strncpy(ctx->title, title, sizeof(ctx->title)-1);
            strncpy(ctx->hint, hint, sizeof(ctx->hint)-1);
            lv_obj_t* r = lv_list_add_btn(list, icon_lbl, "set >");
            lv_obj_set_style_bg_color(r, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_t* vl = lv_obj_get_child(r, 1);
            if (vl && lv_obj_check_type(vl, &lv_label_class))
                lv_obj_set_style_text_color(vl, lv_color_hex(ACCENT), 0);
            lv_obj_set_user_data(r, ctx);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                auto* c = (SetCtx*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (c && c->name) repeater_input_dialog(c->name, c->title, c->hint, c->prefix, c->pw);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                auto* c = (SetCtx*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (c) { free(c->name); delete c; }
            }, LV_EVENT_DELETE, nullptr);
            row++;
        };

        // Action button: sends a command immediately and pushes confirmation
        struct ActCtx { char* name; char cmd[32]; char fmt[48]; };
        auto add_act = [&](const char* icon_lbl, const char* cmd, const char* fmt) {
            auto* ctx = new ActCtx();
            ctx->name = strdup(contact_name);
            strncpy(ctx->cmd, cmd, sizeof(ctx->cmd)-1);
            strncpy(ctx->fmt, fmt, sizeof(ctx->fmt)-1);
            lv_obj_t* r = lv_list_add_btn(list, icon_lbl, ">");
            lv_obj_set_style_bg_color(r, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_t* vl = lv_obj_get_child(r, 1);
            if (vl && lv_obj_check_type(vl, &lv_label_class))
                lv_obj_set_style_text_color(vl, lv_color_hex(ACCENT), 0);
            lv_obj_set_user_data(r, ctx);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                auto* c = (ActCtx*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (c && c->name) repeater_send(c->name, c->cmd, c->fmt);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                auto* c = (ActCtx*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (c) { free(c->name); delete c; }
            }, LV_EVENT_DELETE, nullptr);
            row++;
        };

        // ── Section: Connection ───────────────────────────
        sec_header("  Connection");
        {
            char buf[128];
            auto add_con = [&](const char* label, const char* val, uint32_t color) {
                snprintf(buf, sizeof(buf), "  %s", label);
                lv_obj_t* r = lv_list_add_btn(list, buf, val);
                lv_obj_set_style_bg_color(r, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
                lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(r, lv_color_hex(TEXT_PRIMARY), 0);
                lv_obj_t* val_lbl = lv_obj_get_child(r, 1);
                if (val_lbl && lv_obj_check_type(val_lbl, &lv_label_class))
                    lv_obj_set_style_text_color(val_lbl, lv_color_hex(color), 0);
                row++;
            };
            char snr_str[16], rssi_str[16];
            snprintf(snr_str, sizeof(snr_str), "%.1f dB", target->snr);
            snprintf(rssi_str, sizeof(rssi_str), "%d dBm", target->rssi);
            add_con("Status", "Connected", ACCENT_GREEN);
            add_con("SNR", snr_str, TEXT_PRIMARY);
            add_con("RSSI", rssi_str, TEXT_PRIMARY);
            add_con("Login", "Logged in", ACCENT_GREEN);
            uint8_t perm = sigurdos::mesh::getLoginPermission(contact_name);
            char perm_buf[24];
            if (perm == 1) {
                snprintf(perm_buf, sizeof(perm_buf), "Admin (perm=%d)", perm);
            } else if (perm == 0) {
                snprintf(perm_buf, sizeof(perm_buf), "Read-Write");
            } else {
                snprintf(perm_buf, sizeof(perm_buf), "Guest");
            }
            add_con("Permission", perm_buf, perm == 1 ? ACCENT : TEXT_SECONDARY);
        }

        // Determine if the user has admin permission
        // Server permission encoding: 1 = Admin, 0 = Read-Write, 2 = Guest
        bool is_admin = (sigurdos::mesh::getLoginPermission(contact_name) == 1);

        // ── Section: Radio Settings ──────────────────────
        if (is_admin) {
        sec_header("  Radio Settings");
        add_set(LV_SYMBOL_WIFI "  Freq (MHz)",  "Set Freq",       "e.g. 868.0",       "set freq ",  false);
        add_set(LV_SYMBOL_WIFI "  Bandwidth",   "Set Bandwidth",  "e.g. 125.0",       "set bw ",    false);
        add_set(LV_SYMBOL_WIFI "  Spreading Factor", "Set SF",    "Range 5-12",       "set sf ",    false);
        add_set(LV_SYMBOL_WIFI "  Coding Rate",  "Set CR",        "Range 5-8",        "set cr ",    false);
        }

        // ── Section: Management ──────────────────────────
        if (is_admin) {
        sec_header("  Management");
        add_set(LV_SYMBOL_REFRESH "  Advert Duration", "Advert Duration", "Hours (24/72/168)", "set advert.duration ", false);
        add_act(LV_SYMBOL_REFRESH "  Sync Clock", "clock sync", "Sent: clock sync");
        add_set(LV_SYMBOL_CLOSE "  Admin Password",   "Admin Password",   "New admin password", "password ",  true);
        add_set(LV_SYMBOL_CLOSE "  Guest Password",   "Guest Password",   "New guest password", "set guest.password ", false);
        add_act(LV_SYMBOL_LIST "  Version",           "ver",              "Sent: ver");
        }

        // ── Section: Network ─────────────────────────────
        if (is_admin) {
        sec_header("  Network");
        add_act(LV_SYMBOL_LIST "  Neighbours",        "neighbors",        "Sent: neighbors");
        add_act(LV_SYMBOL_LIST "  Regions",           "region",           "Sent: region");
        add_act(LV_SYMBOL_REFRESH "  Repeat On",      "set repeat on",    "Sent: repeat on");
        add_act(LV_SYMBOL_REFRESH "  Repeat Off",     "set repeat off",   "Sent: repeat off");
        add_act(LV_SYMBOL_REFRESH "  Advert (flood)",  "advert",          "Sent: flood advert");
        add_act(LV_SYMBOL_EDIT "  Public Key",        "get public.key",   "Sent: get public.key");
        }

        // ── Section: Commands ────────────────────────────
        sec_header("  Commands");

        // Admin Cmd (admin only)
        if (is_admin) {
            char* n = strdup(contact_name);
            lv_obj_t* r = lv_list_add_btn(list, LV_SYMBOL_KEYBOARD "  Admin Cmd", ">");
            lv_obj_set_style_bg_color(r, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_user_data(r, n);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                const char* name = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (name) show_admin_cmd_dialog(name);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
            row++;
        }

        // Fetch Msgs (available to all logged-in users)
        {
            char* n = strdup(contact_name);
            lv_obj_t* r = lv_list_add_btn(list, LV_SYMBOL_LIST "  Fetch Msgs", ">");
            lv_obj_set_style_bg_color(r, lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_user_data(r, n);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                const char* name = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (name) show_fetch_msgs_dialog(name);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
            row++;
        }

        // Reboot (with confirmation dialog) — admin only
        if (is_admin) {
            char* n = strdup(contact_name);
            lv_obj_t* r = lv_list_add_btn(list, LV_SYMBOL_REFRESH "  Reboot", "!!");
            lv_obj_set_style_bg_color(r, lv_color_hex(ACCENT_RED), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(0xffffff), 0);
            lv_obj_set_user_data(r, n);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                const char* name = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (name) {
                    // Confirmation dialog before reboot
                    lv_obj_t* act = lv_scr_act();
                    auto dsz = dialog_size(220, 80);
                    lv_obj_t* dlg = lv_obj_create(act);
                    lv_obj_set_size(dlg, dsz.w, dsz.h);
                    lv_obj_center(dlg);
                    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
                    lv_obj_set_style_radius(dlg, 0, 0);
                    lv_obj_set_style_border_width(dlg, 0, 0);
                    lv_obj_t* tl = lv_label_create(dlg);
                    lv_label_set_text(tl, "Reboot repeater?");
                    lv_obj_set_style_text_color(tl, lv_color_hex(ACCENT_RED), 0);
                    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 10);
                    char* cn = strdup(name);
                    lv_obj_set_user_data(dlg, cn);
                    lv_obj_t* yb = lv_btn_create(dlg);
                    lv_obj_set_size(yb, 80, 24);
                    lv_obj_align(yb, LV_ALIGN_BOTTOM_LEFT, 10, -4);
                    lv_obj_set_style_bg_color(yb, lv_color_hex(ACCENT_RED), 0);
                    lv_obj_set_style_radius(yb, 0, 0);
                    lv_obj_t* yl = lv_label_create(yb);
                    lv_label_set_text(yl, "Reboot");
                    lv_obj_center(yl);
                    lv_obj_set_style_text_color(yl, lv_color_hex(0xffffff), 0);
                    lv_obj_add_event_cb(yb, [](lv_event_t* ce) {
                        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce));
                        const char* cn = (const char*)lv_obj_get_user_data(dlg);

                        if (cn) { repeater_send(cn, "reboot", "Reboot sent"); }

                        lv_obj_del_async(dlg);
                    }, LV_EVENT_CLICKED, nullptr);
                    lv_obj_t* nb = lv_btn_create(dlg);
                    lv_obj_set_size(nb, 80, 24);
                    lv_obj_align(nb, LV_ALIGN_BOTTOM_RIGHT, -10, -4);
                    lv_obj_set_style_bg_color(nb, lv_color_hex(BG_INPUT), 0);
                    lv_obj_set_style_radius(nb, 0, 0);
                    lv_obj_t* nl = lv_label_create(nb);
                    lv_label_set_text(nl, "Cancel");
                    lv_obj_center(nl);
                    lv_obj_add_event_cb(nb, [](lv_event_t* ce) {
                        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
                    }, LV_EVENT_CLICKED, nullptr);
                    lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
                        free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de)));
                    }, LV_EVENT_DELETE, nullptr);
                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
            row++;
        }

        // Logout
        {
            char* n = strdup(contact_name);
            lv_obj_t* r = lv_list_add_btn(list, LV_SYMBOL_REFRESH "  Logout", ">");
            lv_obj_set_style_bg_color(r, lv_color_hex(ACCENT_ORANGE), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(0xffffff), 0);
            lv_obj_set_user_data(r, n);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                const char* name = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                if (name) {
                    sigurdos::mesh::sendLogout(name);
                    lv_timer_create([](lv_timer_t* t) {
                        go_back();
                        lv_timer_del(t);
                    }, 600, nullptr);
                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(r, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);
            row++;
        }
    }

    // Add all clickable list items to the default group for trackball/keyboard navigation
    lv_group_t* grp = lv_group_get_default();
    if (grp) {
        uint32_t child_cnt = lv_obj_get_child_cnt(list);
        bool focused = false;
        for (uint32_t ci = 0; ci < child_cnt; ci++) {
            lv_obj_t* child = lv_obj_get_child(list, ci);
            if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
                lv_group_add_obj(grp, child);
                if (!focused) {
                    lv_group_focus_obj(child);
                    focused = true;
                }
            }
        }
    }

    show_screen(scr);
}

} // namespace sigurdos::ui
