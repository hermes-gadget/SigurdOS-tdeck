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
#include "../contact_list_power.h"
#include "../repeater_transcript.h"
#include "../../hal/prefs.h"
#include "../../mesh/mesh_wrapper.h"
#include "../../app/qr_show.h"
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

static int g_contacts_page = 0;

void show_contact_memory_error(lv_obj_t* scr)
{
    lv_obj_t* err = lv_label_create(scr);
    lv_label_set_text(err, "Contact list unavailable.\nNot enough memory.");
    lv_obj_set_width(err, CONTENT_W);
    lv_obj_set_style_text_align(err, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(err, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(err, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(err, LV_ALIGN_CENTER, 0, 0);
}

static void add_contact_pager_at(lv_obj_t* scr, int y, int page, int pages, int total,
                                 lv_event_cb_t prev_cb, lv_event_cb_t next_cb)
{
    if (pages <= 1) return;

    lv_obj_t* pager = lv_obj_create(scr);
    lv_obj_set_size(pager, CONTENT_W, 24);
    lv_obj_align(pager, LV_ALIGN_TOP_LEFT, 0, y + 2);
    lv_obj_set_style_bg_opa(pager, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pager, 0, 0);
    lv_obj_set_style_pad_all(pager, 0, 0);

    lv_obj_t* prev = lv_btn_create(pager);
    lv_obj_set_size(prev, 34, 22);
    lv_obj_align(prev, LV_ALIGN_LEFT_MID, 4, 0);
    apply_pixel_btn_outline(prev);
    if (page <= 0) lv_obj_add_state(prev, LV_STATE_DISABLED);
    lv_obj_add_event_cb(prev, prev_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* prev_l = lv_label_create(prev);
    lv_label_set_text(prev_l, LV_SYMBOL_LEFT);
    lv_obj_center(prev_l);

    int start = contact_page_start(page, total) + 1;
    int end = contact_page_end(page, total);
    char page_buf[32];
    snprintf(page_buf, sizeof(page_buf), "%d-%d/%d", start, end, total);
    lv_obj_t* status = lv_label_create(pager);
    lv_label_set_text(status, page_buf);
    lv_obj_set_width(status, CONTENT_W - 88);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(status, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* next = lv_btn_create(pager);
    lv_obj_set_size(next, 34, 22);
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, -4, 0);
    apply_pixel_btn_outline(next);
    if (page >= pages - 1) lv_obj_add_state(next, LV_STATE_DISABLED);
    lv_obj_add_event_cb(next, next_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* next_l = lv_label_create(next);
    lv_label_set_text(next_l, LV_SYMBOL_RIGHT);
    lv_obj_center(next_l);
}

void add_contact_pager(lv_obj_t* scr, int page, int pages, int total,
                       lv_event_cb_t prev_cb, lv_event_cb_t next_cb)
{
    add_contact_pager_at(scr, CONTENT_Y, page, pages, total, prev_cb, next_cb);
}

using ContactListEntry = sigurdos::mesh::ContactInfo;

static ContactSortMode g_contact_sort = ContactSortMode::Alpha;
static ContactFilterMode g_contact_filter = ContactFilterMode::All;

static int compare_contact_entries(const void* a, const void* b)
{
    const auto* ca = static_cast<const ContactListEntry*>(a);
    const auto* cb = static_cast<const ContactListEntry*>(b);
    return contact_sort_compare(g_contact_sort,
        ca->name, ca->type, ca->last_seen,
        cb->name, cb->type, cb->last_seen);
}

static void show_manual_contact_dialog();

static void add_contact_power_bar(lv_obj_t* scr)
{
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LV_PCT(100), 26);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 1, 0);
    lv_obj_set_style_pad_gap(bar, 3, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(bar, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);

    auto chip = [bar](const char* text, bool selected, lv_event_cb_t cb, void* data) {
        lv_obj_t* btn = lv_btn_create(bar);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, 23);
        selected ? apply_pixel_btn(btn) : apply_pixel_btn_outline(btn);
        lv_obj_set_style_pad_left(btn, 7, 0);
        lv_obj_set_style_pad_right(btn, 7, 0);
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, emoji_wrapped_montserrat_10, 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, data);
    };
    auto filter_cb = [](lv_event_t* e) {
        g_contact_filter = (ContactFilterMode)(intptr_t)lv_event_get_user_data(e);
        g_contacts_page = 0;
        contacts_screen_show();
    };
    chip("ALL", g_contact_filter == ContactFilterMode::All, filter_cb,
         (void*)(intptr_t)ContactFilterMode::All);
    chip("FAV", g_contact_filter == ContactFilterMode::Favourites, filter_cb,
         (void*)(intptr_t)ContactFilterMode::Favourites);
    chip("INFRA", g_contact_filter == ContactFilterMode::Infrastructure, filter_cb,
         (void*)(intptr_t)ContactFilterMode::Infrastructure);
    chip("PATH", g_contact_filter == ContactFilterMode::HasPath, filter_cb,
         (void*)(intptr_t)ContactFilterMode::HasPath);
    const char* sort_text = g_contact_sort == ContactSortMode::Alpha ? "SORT A-Z" :
        (g_contact_sort == ContactSortMode::Recent ? "SORT RECENT" : "SORT TYPE");
    chip(sort_text, false, [](lv_event_t*) {
        g_contact_sort = g_contact_sort == ContactSortMode::Alpha
            ? ContactSortMode::Recent : (g_contact_sort == ContactSortMode::Recent
                ? ContactSortMode::Type : ContactSortMode::Alpha);
        g_contacts_page = 0;
        contacts_screen_show();
    }, nullptr);
    chip("+ ADD", false, [](lv_event_t*) { show_manual_contact_dialog(); }, nullptr);
}

// ════════════════════════════════════════════════════════
// Contacts — tap-to-message directory
// ════════════════════════════════════════════════════════
static int contacts_filter_type = -1; // -1=all, else ADV_TYPE_*

void contacts_screen_set_filter(int adv_type) {
    contacts_filter_type = adv_type;
    g_contacts_page = 0;
}

void contacts_screen_show()
{
    lv_obj_t* scr = make_screen_full("Contacts");

    sigurdos::mesh::ContactInfo* all_contacts =
        new(std::nothrow) sigurdos::mesh::ContactInfo[MAX_CONTACTS];
    if (!all_contacts) {
        show_contact_memory_error(scr);
        show_screen(scr);
        return;
    }

    int total = sigurdos::mesh::exportContactsFull(all_contacts, MAX_CONTACTS);
    if (total < 0) total = 0;
    if (total > MAX_CONTACTS) total = MAX_CONTACTS;

    ContactListEntry* entries = new(std::nothrow) ContactListEntry[MAX_CONTACTS];
    if (!entries) {
        delete[] all_contacts;
        show_contact_memory_error(scr);
        show_screen(scr);
        return;
    }

    // Filter and annotate once before paging; no per-row mesh scans.
    int n = 0;
    for (int i = 0; i < total; i++) {
        bool keep = true;
        if (contacts_filter_type >= 0) {
            keep = (all_contacts[i].type == contacts_filter_type);
        }
        const bool favourite = all_contacts[i].favourite;
        const bool has_path = all_contacts[i].has_path;
        keep = keep && contact_filter_accepts(
            g_contact_filter, all_contacts[i].type, favourite, has_path);
        if (keep) {
            entries[n] = all_contacts[i];
            n++;
        }
    }
    delete[] all_contacts;
    all_contacts = nullptr;

    add_contact_power_bar(scr);
    const int content_start = CONTENT_Y + 28;

    if (n > 1) qsort(entries, n, sizeof(entries[0]), compare_contact_entries);

    if (n == 0) {
        g_contacts_page = 0;
        lv_obj_t* info = lv_label_create(scr);
        lv_label_set_text(info,
            "No contacts yet.\n\n"
            "Companions (chat nodes) and room servers\n"
            "appear here once they broadcast an advert\n"
            "or send you a message.\n\n"
            "Tap a contact to send a direct message.");
        lv_obj_set_width(info, CONTENT_W);
        lv_obj_set_style_pad_left(info, 8, 0);
        lv_obj_set_style_pad_right(info, 8, 0);
        lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(info, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(info, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, content_start + 4);
        delete[] entries;
        show_screen(scr);
        return;
    }

    int pages = contact_page_count(n);
    g_contacts_page = contact_clamp_page(g_contacts_page, n);
    int page_start = contact_page_start(g_contacts_page, n);
    int page_end = contact_page_end(g_contacts_page, n);
    int page_n = page_end - page_start;
    // Heap-allocate the per-page buffer rather than placing ~1.8 KB on the
    // stack — this runs in the LVGL task context (navigation + pager clicks).
    ContactListEntry* page_contacts =
        new(std::nothrow) ContactListEntry[CONTACT_LIST_PAGE_SIZE];
    if (!page_contacts) {
        delete[] entries;
        show_contact_memory_error(scr);
        show_screen(scr);
        return;
    }
    for (int i = 0; i < page_n; i++) {
        page_contacts[i] = entries[page_start + i];
    }
    delete[] entries;
    entries = nullptr;

    int list_y = content_start;
    int list_h = CONTENT_H - 28;
    if (pages > 1) {
        add_contact_pager_at(scr, content_start, g_contacts_page, pages, n,
            [](lv_event_t*) {
                if (g_contacts_page > 0) g_contacts_page--;
                contacts_screen_show();
            },
            [](lv_event_t*) {
                g_contacts_page++;
                contacts_screen_show();
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
        auto& c = page_contacts[i];

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
        lv_label_set_text(icon, LV_SYMBOL_CALL);
        lv_obj_set_style_text_color(icon, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_text_font(icon, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 6, 0);

        // Name
        lv_obj_t* name_l = lv_label_create(row);
        lv_label_set_text(name_l, c.name);
        lv_obj_set_width(name_l, CONTENT_W - 100);
        lv_label_set_long_mode(name_l, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(name_l, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(name_l, LV_ALIGN_LEFT_MID, 28, 0);

        const char* quality = contact_quality_name(c.rssi, c.snr);
        lv_obj_t* quality_l = lv_label_create(row);
        lv_label_set_text(quality_l, quality);
        lv_obj_set_style_text_color(quality_l, lv_color_hex(
            strcmp(quality, "GOOD") == 0 ? ACCENT_GREEN :
            (strcmp(quality, "FAIR") == 0 ? ACCENT_ORANGE :
             (strcmp(quality, "WEAK") == 0 ? ACCENT_RED : TEXT_MUTED))), 0);
        lv_obj_set_style_text_font(quality_l, emoji_wrapped_montserrat_10, 0);
        lv_obj_align(quality_l, LV_ALIGN_RIGHT_MID, -6, 0);

        // Store name + type for click handler
        // Room servers open the detail screen; chat nodes open DM
        struct ContactRowData { char* name; uint8_t type; };
        ContactRowData* row_data = new ContactRowData{strdup(c.name), c.type};
        lv_obj_set_user_data(row, row_data);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_current_target(e);
            ContactRowData* d = (ContactRowData*)lv_obj_get_user_data(target);
            if (d && d->name) {
                if (d->type == ADV_TYPE_ROOM) {
                    repeater_detail_screen_show(d->name);
                } else {
                    chat_screen_open_dm(d->name);
                }
            }
        }, LV_EVENT_CLICKED, nullptr);

        // Free the heap-allocated name copy when the row is deleted
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            ContactRowData* d = (ContactRowData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
            if (d) { free(d->name); delete d; }
        }, LV_EVENT_DELETE, nullptr);
    }

    delete[] page_contacts;
    show_screen(scr);
}

struct ManualContactCtx {
    lv_obj_t* name;
    lv_obj_t* pubkey;
    lv_obj_t* type_label;
    lv_obj_t* error;
    uint8_t type;
};

static const char* manual_contact_type_name(uint8_t type)
{
    return type == ADV_TYPE_REPEATER ? "REPEATER" :
        (type == ADV_TYPE_ROOM ? "ROOM" : "CHAT");
}

static void show_manual_contact_dialog()
{
    lv_obj_t* parent = lv_obj_get_screen(lv_scr_act());
    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, 290, 190);
    lv_obj_center(dlg);
    apply_pixel_card_accent(dlg);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "ADD CONTACT");
    lv_obj_set_style_text_color(title, lv_color_hex(ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* name = lv_textarea_create(dlg);
    lv_obj_set_size(name, 274, 28);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 24);
    lv_textarea_set_one_line(name, true);
    lv_textarea_set_max_length(name, 31);
    lv_textarea_set_placeholder_text(name, "Contact name");
    apply_pixel_input(name);

    lv_obj_t* key = lv_textarea_create(dlg);
    lv_obj_set_size(key, 274, 28);
    lv_obj_align(key, LV_ALIGN_TOP_LEFT, 0, 56);
    lv_textarea_set_one_line(key, true);
    lv_textarea_set_max_length(key, 64);
    lv_textarea_set_placeholder_text(key, "64-hex public key");
    lv_obj_set_style_text_font(key, emoji_wrapped_montserrat_10, 0);
    apply_pixel_input(key);

    lv_obj_t* type = lv_btn_create(dlg);
    lv_obj_set_size(type, 110, 24);
    lv_obj_align(type, LV_ALIGN_TOP_LEFT, 0, 90);
    apply_pixel_btn_outline(type);
    lv_obj_t* type_label = lv_label_create(type);
    lv_label_set_text(type_label, "TYPE: CHAT");
    lv_obj_center(type_label);

    lv_obj_t* error = lv_label_create(dlg);
    lv_label_set_text(error, "");
    lv_obj_set_width(error, 274);
    lv_obj_set_style_text_color(error, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(error, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(error, LV_ALIGN_TOP_LEFT, 0, 120);

    ManualContactCtx* ctx = new ManualContactCtx{
        name, key, type_label, error, ADV_TYPE_CHAT};
    lv_obj_set_user_data(dlg, ctx);
    lv_obj_set_user_data(type, ctx);
    lv_obj_add_event_cb(type, [](lv_event_t* e) {
        auto* data = (ManualContactCtx*)lv_obj_get_user_data(
            (lv_obj_t*)lv_event_get_target(e));
        if (!data) return;
        data->type = data->type == ADV_TYPE_CHAT ? ADV_TYPE_REPEATER :
            (data->type == ADV_TYPE_REPEATER ? ADV_TYPE_ROOM : ADV_TYPE_CHAT);
        char text[24];
        snprintf(text, sizeof(text), "TYPE: %s", manual_contact_type_name(data->type));
        lv_label_set_text(data->type_label, text);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel = lv_btn_create(dlg);
    lv_obj_set_size(cancel, 80, 24);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    apply_pixel_btn_outline(cancel);
    lv_obj_t* cancel_l = lv_label_create(cancel);
    lv_label_set_text(cancel_l, "CANCEL");
    lv_obj_center(cancel_l);
    lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
        lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e)));
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* add = lv_btn_create(dlg);
    lv_obj_set_size(add, 80, 24);
    lv_obj_align(add, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    apply_pixel_btn(add);
    lv_obj_t* add_l = lv_label_create(add);
    lv_label_set_text(add_l, "ADD");
    lv_obj_center(add_l);
    lv_obj_set_user_data(add, ctx);
    lv_obj_add_event_cb(add, [](lv_event_t* e) {
        auto* data = (ManualContactCtx*)lv_obj_get_user_data(
            (lv_obj_t*)lv_event_get_target(e));
        if (!data) return;
        const char* name_text = lv_textarea_get_text(data->name);
        const char* key_text = lv_textarea_get_text(data->pubkey);
        if (!contact_manual_fields_valid(name_text, key_text)) {
            lv_label_set_text(data->error, "Name or public key is invalid");
            return;
        }
        if (!sigurdos::mesh::addContactManual(name_text, key_text, data->type)) {
            lv_label_set_text(data->error, "Duplicate contact or list full");
            return;
        }
        g_contacts_page = 0;
        contacts_screen_show();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        delete (ManualContactCtx*)lv_obj_get_user_data(
            (lv_obj_t*)lv_event_get_target(e));
    }, LV_EVENT_DELETE, nullptr);
    if (lv_group_get_default()) lv_group_focus_obj(name);
}

// Forward declaration of login-polling timer (defined after dialog functions)
static void start_login_poll_timer(const char* name);
static void start_connection_watch_timer(const char* name);

// ── Login password dialog ─────────────────────────
// Shows a modal dialog for entering a password to log into a repeater/room server.
// Includes a "Save Password" checkbox that persists the password to NVS.
void show_login_password_dialog(const char* contact_name)
{
    if (!contact_name) return;

    lv_obj_t* scr = lv_obj_get_screen(lv_scr_act());
    auto dlg_sz = dialog_size(240, 130);
    lv_obj_t* dlg = lv_obj_create(scr);
    lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 8, 0);

    // Title
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "Login to %s", contact_name);
    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // Password textarea
    lv_obj_t* ta = lv_textarea_create(dlg);
    lv_obj_set_size(ta, dlg_sz.w - 16, 28);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 26);
    lv_textarea_set_placeholder_text(ta, "Password (press Enter to submit)");
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    // Focus the textarea so keyboard input is routed here
    lv_group_t* g = lv_group_get_default();
    if (g) lv_group_focus_obj(ta);

    // "Save Password" checkbox
    lv_obj_t* save_cb = lv_checkbox_create(dlg);
    lv_checkbox_set_text(save_cb, "Save Password");
    lv_obj_align(save_cb, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_text_color(save_cb, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(save_cb, emoji_wrapped_montserrat_10, 0);

    // Pre-fill password from NVS if saved
    char saved_pw[64] = {0};
    if (sigurdos::loadRepeaterPassword(contact_name, saved_pw, sizeof(saved_pw))) {
        lv_textarea_set_text(ta, saved_pw);
        lv_obj_add_state(save_cb, LV_STATE_CHECKED);
    }

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

    // Login button
    char* pw_name = strdup(contact_name);
    lv_obj_t* login_btn = lv_btn_create(dlg);
    lv_obj_set_size(login_btn, 80, 24);
    lv_obj_align(login_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
    lv_obj_set_style_bg_color(login_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(login_btn, 0, 0);
    lv_obj_t* lb = lv_label_create(login_btn);
    lv_label_set_text(lb, "Login");
    lv_obj_center(lb);
    lv_obj_set_style_text_color(lb, lv_color_hex(BG_PRIMARY), 0);

    // Store references for the click handler
    struct PwDialogData { char* name; lv_obj_t* ta; lv_obj_t* save_cb; };
    PwDialogData* dd = new PwDialogData{pw_name, ta, save_cb};
    lv_obj_set_user_data(login_btn, dd);

    lv_obj_add_event_cb(login_btn, [](lv_event_t* le) {
        PwDialogData* d = (PwDialogData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(le));
        if (d && d->name) {
            const char* pw = lv_textarea_get_text(d->ta);
            if (pw && pw[0]) {
                sigurdos::mesh::sendLogin(d->name, pw);
                // Start a login-polling timer on the main screen
                start_login_poll_timer(d->name);
                // Save password to NVS if checkbox is checked
                if (d->save_cb && (lv_obj_get_state(d->save_cb) & LV_STATE_CHECKED)) {
                    sigurdos::saveRepeaterPassword(d->name, pw);
                }
            }
        }
        // Close dialog
        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(le));
        lv_obj_del_async(dlg);
    }, LV_EVENT_CLICKED, nullptr);

    // Add Enter-key handler to textarea (submit on Enter)
    lv_obj_add_event_cb(ta, [](lv_event_t* te) {
        lv_obj_t* t = (lv_obj_t*)lv_event_get_target(te);
        uint32_t key = lv_event_get_key(te);
        if (key == LV_KEY_ENTER) {
            const char* pwt = lv_textarea_get_text(t);
            if (pwt && pwt[0]) {
                lv_obj_t* parent = lv_obj_get_parent(t);
                if (parent) {
                    uint32_t c = lv_obj_get_child_cnt(parent);
                    for (uint32_t i = 0; i < c; i++) {
                        lv_obj_t* child = lv_obj_get_child(parent, i);
                        if (child && lv_obj_check_type(child, &lv_button_class)) {
                            PwDialogData* data = (PwDialogData*)lv_obj_get_user_data(child);
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

    // Cleanup on delete
    lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
        PwDialogData* d = (PwDialogData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de));
        if (d) {
            free(d->name);
            delete d;
        }
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_set_user_data(dlg, dd);
    // dd and dd->name are owned by the dialog's user_data and freed in the
    // LV_EVENT_DELETE handler above; cppcheck cannot see through the callback.
    // cppcheck-suppress memleak
}

// ── Login status polling timer ────────────────────
// After a login attempt, this timer polls getLoginStatus() every 2 seconds.
// On LOGIN_OK → rebuilds the screen with the post-login (management) view.
// On terminal failure/timeout/drop → rebuilds pre-login so the reason is visible.
// Cancels itself if the user navigates away.
struct LoginPollCtx {
    char* name;
    lv_obj_t* screen;
    bool watch_connection;
    uint32_t gen;        // matches g_login_poll_gen at creation time; stale if timer restarted
};
static lv_timer_t* g_login_poll_timer = nullptr;
static uint32_t g_login_poll_gen = 0;


static void on_login_poll_timer(lv_timer_t* t) {
    LoginPollCtx* ctx = (LoginPollCtx*)lv_timer_get_user_data(t);
    if (!ctx || !ctx->name) {
        lv_timer_del(t);
        if (g_login_poll_timer == t) g_login_poll_timer = nullptr;
        return;
    }


    // If a newer generation timer was started, this ctx is stale
    // If user navigated away from the screen, stop polling
    if (!ctx->screen || ctx->gen != g_login_poll_gen || lv_scr_act() != ctx->screen) {

        free(ctx->name);
        delete ctx;
        lv_timer_del(t);
        if (g_login_poll_timer == t) g_login_poll_timer = nullptr;
        return;
    }

    uint8_t st = sigurdos::mesh::getLoginStatus(ctx->name);
    if (st == LOGIN_STATUS_OK) {
        if (ctx->watch_connection) return;
        char* n = strdup(ctx->name);
        free(ctx->name);
        delete ctx;
        lv_timer_del(t);
        if (g_login_poll_timer == t) g_login_poll_timer = nullptr;
        // Rebuild screen in post-login mode
        repeater_detail_screen_show(n, true);
        start_connection_watch_timer(n);
        free(n);
    } else if (st == LOGIN_STATUS_FAILED || st == LOGIN_STATUS_TIMEOUT ||
               st == LOGIN_STATUS_DROPPED ||
               (ctx->watch_connection && st == LOGIN_STATUS_NONE)) {
        // Rebuild pre-login view so the terminal reason and Login button show.
        char* n = strdup(ctx->name);
        free(ctx->name);
        delete ctx;
        lv_timer_del(t);
        if (g_login_poll_timer == t) g_login_poll_timer = nullptr;
        repeater_detail_screen_show(n, false);
        free(n);
    }
    // LOGIN_PENDING/LOGIN_NONE while waiting, or LOGIN_OK while watching, keep polling.
}

static void start_session_poll_timer(const char* name, bool watch_connection) {
    if (!name) return;
    // Cancel any existing timer first
    if (g_login_poll_timer) {
        LoginPollCtx* old = (LoginPollCtx*)lv_timer_get_user_data(g_login_poll_timer);
        if (old) { free(old->name); delete old; }
        lv_timer_del(g_login_poll_timer);
        g_login_poll_timer = nullptr;
    }

    g_login_poll_gen++;
    LoginPollCtx* ctx = new LoginPollCtx{
        strdup(name), lv_scr_act(), watch_connection, g_login_poll_gen};

    g_login_poll_timer = lv_timer_create(on_login_poll_timer, 2000, ctx);
}

static void start_login_poll_timer(const char* name) {
    start_session_poll_timer(name, false);
}

static void start_connection_watch_timer(const char* name) {
    start_session_poll_timer(name, true);
}

// ── Admin command dialog ──────────────────────────
// Shows a modal dialog for entering an admin CLI command to send to a logged-in server.
void show_admin_cmd_dialog(const char* contact_name)
{
    if (!contact_name) return;

    // Clear stale responses from previous session
    sigurdos::mesh::clearCmdResponses();

    static constexpr int TERM_TOP_H    = TOP_BAR_H;         // 21
    static constexpr int TERM_INPUT_H  = 28;
    static constexpr int TERM_OUTPUT_H = DISPLAY_H - TERM_TOP_H - 2 * DIVIDER_H - TERM_INPUT_H;  // 189 on 320×240

    lv_obj_t* scr = lv_obj_get_screen(lv_scr_act());
    lv_obj_t* dlg = lv_obj_create(scr);
    lv_obj_set_size(dlg, LV_PCT(100), LV_PCT(100));
    lv_obj_align(dlg, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 0, 0);

    // ── Top bar (matches make_screen_full) ──────
    lv_obj_t* top = lv_obj_create(dlg);
    lv_obj_set_size(top, LV_PCT(100), TERM_TOP_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);

    lv_obj_t* title = lv_label_create(top);
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "> Admin: %s", contact_name);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_color(title, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    // Close button (top-right)
    lv_obj_t* close_btn = lv_btn_create(top);
    lv_obj_set_size(close_btn, 20, TERM_TOP_H - 2);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(close_btn, 0, 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* x_lbl = lv_label_create(close_btn);
    lv_label_set_text(x_lbl, LV_SYMBOL_CLOSE);
    lv_obj_center(x_lbl);
    lv_obj_set_style_text_color(x_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* top = lv_obj_get_parent(btn);
        lv_obj_t* dlg = lv_obj_get_parent(top);
        lv_obj_del_async(dlg);
    }, LV_EVENT_CLICKED, nullptr);

    // Divider below top bar
    lv_obj_t* tdiv = lv_obj_create(dlg);
    lv_obj_set_size(tdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(tdiv, LV_ALIGN_TOP_MID, 0, TERM_TOP_H);
    lv_obj_set_style_bg_color(tdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(tdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tdiv, 0, 0);

    // ── Output area (scrollable, read-only, pure black) ─
    lv_obj_t* out = lv_textarea_create(dlg);
    lv_obj_set_size(out, LV_PCT(100), TERM_OUTPUT_H);
    lv_obj_align(out, LV_ALIGN_TOP_MID, 0, TERM_TOP_H + DIVIDER_H);
    lv_obj_set_style_bg_color(out, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(out, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(out, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(out, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_radius(out, 0, 0);
    lv_obj_set_style_border_width(out, 0, 0);
    lv_obj_set_style_pad_all(out, 4, 0);
    lv_textarea_set_cursor_click_pos(out, false);
    lv_textarea_set_max_length(out, 4096);  // prevent unbounded growth

    // Welcome banner
    lv_textarea_add_text(out, "-- Admin Terminal --\n");
    lv_textarea_add_text(out, "Commands: ver, reboot, password,\n");
    lv_textarea_add_text(out, "set freq, get name, etc.\n");

    // Divider above input
    lv_obj_t* bdiv = lv_obj_create(dlg);
    lv_obj_set_size(bdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(bdiv, LV_ALIGN_BOTTOM_MID, 0, -TERM_INPUT_H);
    lv_obj_set_style_bg_color(bdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(bdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bdiv, 0, 0);

    // ── Input row (full-width flex) ──────────────
    lv_obj_t* input_row = lv_obj_create(dlg);
    lv_obj_set_size(input_row, LV_PCT(100), TERM_INPUT_H);
    lv_obj_align(input_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_row, 0, 0);
    lv_obj_set_style_pad_all(input_row, 2, 0);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* ta = lv_textarea_create(input_row);
    lv_obj_set_size(ta, LV_PCT(80), TERM_INPUT_H - 4);
    lv_textarea_set_placeholder_text(ta, "> command...");
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 240);  // prevent buffer overflow in echo buffer
    lv_obj_set_style_bg_color(ta, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(ta, emoji_wrapped_montserrat_12, 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_group_t* g = lv_group_get_default();
    if (g) lv_group_focus_obj(ta);

    // Store contact name + widget handles
    char* cmd_name = strdup(contact_name);
    struct TermData { char* name; lv_obj_t* ta; lv_obj_t* out; lv_timer_t* timer; bool deleted; };
    TermData* td = new TermData{cmd_name, ta, out, nullptr, false};
    lv_obj_set_user_data(dlg, td);

    // ── Send button ──────────────────────────────
    lv_obj_t* send_btn = lv_btn_create(input_row);
    lv_obj_set_size(send_btn, LV_PCT(18), TERM_INPUT_H - 4);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(send_btn, 0, 0);
    lv_obj_set_style_pad_all(send_btn, 0, 0);
    lv_obj_t* sb = lv_label_create(send_btn);
    lv_label_set_text(sb, "Send");
    lv_obj_center(sb);
    lv_obj_set_style_text_color(sb, lv_color_hex(BG_PRIMARY), 0);
    lv_obj_set_user_data(send_btn, td);
    lv_obj_add_event_cb(send_btn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        TermData* d = (TermData*)lv_obj_get_user_data(btn);
        if (d) {
            auto send = [](TermData* dd) {
                if (!dd || !dd->name) return;
                const char* cmd = lv_textarea_get_text(dd->ta);
                if (!cmd || !cmd[0]) return;
                char echo[256];
                snprintf(echo, sizeof(echo), "\n> %s\n", cmd);
                lv_textarea_add_text(dd->out, echo);
                bool ok = sigurdos::mesh::sendCommand(dd->name, cmd);
                lv_textarea_set_text(dd->ta, "");
                if (lv_group_get_default()) lv_group_focus_obj(dd->ta);
                if (!ok) lv_textarea_add_text(dd->out, "! Send failed\n");
                if (!dd->timer) {
                    dd->timer = lv_timer_create([](lv_timer_t* t) {
                        TermData* dd2 = (TermData*)lv_timer_get_user_data(t);
                        if (!dd2 || dd2->deleted) return;
                        char nb[32], tb[160];
                        while (sigurdos::mesh::pollCmdResponse(nb, sizeof(nb), tb, sizeof(tb))) {
                            if (dd2->deleted) return;
                            if (dd2->name && strcmp(nb, dd2->name) == 0) {
                                char rb[256];
                                format_repeater_cli_reply(rb, sizeof(rb), tb);
                                // Trim output if near max to prevent overflow
                                const char* full = lv_textarea_get_text(dd2->out);
                                if (full && strlen(full) > 3500) {
                                    const char* mid = full + 1500;
                                    while (*mid && *mid != '\n') mid++;
                                    if (*mid) mid++;
                                    static char mid_copy[2048];  // static to avoid 2KB on LVGL timer stack
                                    size_t copy_len = strlen(mid);
                                    if (copy_len >= sizeof(mid_copy)) copy_len = sizeof(mid_copy) - 1;
                                    memcpy(mid_copy, mid, copy_len);
                                    mid_copy[copy_len] = '\0';
                                    lv_textarea_set_text(dd2->out, "(...trimmed...)\n");
                                    lv_textarea_add_text(dd2->out, mid_copy);
                                }
                                lv_textarea_add_text(dd2->out, rb);
                            }
                        }
                    }, 500, dd);
                }
            };
            send(d);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // ── Enter key on textarea ────────────────────
    lv_obj_add_event_cb(ta, [](lv_event_t* te) {
        uint32_t key = lv_event_get_key(te);
        if (key == LV_KEY_ENTER) {
            lv_obj_t* t = (lv_obj_t*)lv_event_get_target(te);
            lv_obj_t* row = lv_obj_get_parent(t);
            uint32_t c = lv_obj_get_child_cnt(row);
            for (uint32_t i = 0; i < c; i++) {
                lv_obj_t* child = lv_obj_get_child(row, i);
                if (child && lv_obj_check_type(child, &lv_button_class)) {
                    TermData* d = (TermData*)lv_obj_get_user_data(child);
                    if (d) {
                        lv_obj_send_event(child, LV_EVENT_CLICKED, nullptr);
                        break;
                    }
                }
            }
        }
    }, LV_EVENT_KEY, nullptr);

    // ── Cleanup ──────────────────────────────────
    lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
        TermData* d = (TermData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de));
        if (d) {
            d->deleted = true;  // prevent timer callback from using members
            if (d->timer) lv_timer_del(d->timer);
            if (d->name) free(d->name);
            delete d;
        }
        sigurdos::mesh::clearCmdResponses();
    }, LV_EVENT_DELETE, nullptr);
    // td and td->name are owned by the dialog's user_data (set at creation)
    // and freed in the LV_EVENT_DELETE handler above; not a leak.
    // cppcheck-suppress memleak
}

// Public test hook — callable from test_controller
void admin_cmd_show(const char* contact_name) {
    show_admin_cmd_dialog(contact_name);
}

// ── Room message fetch dialog (Phase 4.6) ──────────
// Shows a modal dialog for entering a channel name to fetch messages from.
void show_fetch_msgs_dialog(const char* contact_name)
{
    if (!contact_name) return;

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
    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Fetch room messages");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // Hint
    lv_obj_t* hint = lv_label_create(dlg);
    lv_label_set_text(hint, "Enter channel name (e.g. #general)");
    lv_obj_set_style_text_color(hint, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(hint, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 24);

    // Channel name textarea
    lv_obj_t* ta = lv_textarea_create(dlg);
    lv_obj_set_size(ta, dlg_sz.w - 16, 28);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 38);
    lv_textarea_set_placeholder_text(ta, "#channel");
    lv_textarea_set_one_line(ta, true);
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

    // Fetch button
    struct FetchDialogData { char* name; lv_obj_t* ta; };
    char* fm_name = strdup(contact_name);
    FetchDialogData* fd = new FetchDialogData{fm_name, ta};
    lv_obj_t* fetch_btn = lv_btn_create(dlg);
    lv_obj_set_size(fetch_btn, 80, 24);
    lv_obj_align(fetch_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
    lv_obj_set_style_bg_color(fetch_btn, lv_color_hex(0x0088cc), 0);
    lv_obj_set_style_radius(fetch_btn, 0, 0);
    lv_obj_t* fb = lv_label_create(fetch_btn);
    lv_label_set_text(fb, "Fetch");
    lv_obj_center(fb);
    lv_obj_set_style_text_color(fb, lv_color_hex(0xffffff), 0);
    lv_obj_set_user_data(fetch_btn, fd);

    lv_obj_add_event_cb(fetch_btn, [](lv_event_t* le) {
        FetchDialogData* d = (FetchDialogData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(le));
        if (d && d->name) {
            const char* channel = lv_textarea_get_text(d->ta);
            if (channel && channel[0]) {
                sigurdos::mesh::sendRoomMsgFetchRequest(d->name, channel);
                char confirm[64];
                snprintf(confirm, sizeof(confirm), "Fetching msgs from %s channel %s",
                         d->name, channel);
                sigurdos::mesh::mesh_v2_queue_push("System", "", confirm, 0, 0.0f);
                // Navigate to Chat screen so user sees incoming messages
                sigurdos::ui::navigate_to(sigurdos::ui::Screen::Chat);
            }
        }
        lv_obj_t* dlg = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(le));
        lv_obj_del_async(dlg);
    }, LV_EVENT_CLICKED, nullptr);

    // Enter-key handler on textarea
    lv_obj_add_event_cb(ta, [](lv_event_t* te) {
        lv_obj_t* t = (lv_obj_t*)lv_event_get_target(te);
        uint32_t key = lv_event_get_key(te);
        if (key == LV_KEY_ENTER) {
            lv_obj_t* parent = lv_obj_get_parent(t);
            if (parent) {
                uint32_t c = lv_obj_get_child_cnt(parent);
                for (uint32_t i = 0; i < c; i++) {
                    lv_obj_t* child = lv_obj_get_child(parent, i);
                    if (child && lv_obj_check_type(child, &lv_button_class)) {
                        FetchDialogData* data = (FetchDialogData*)lv_obj_get_user_data(child);
                        if (data) {
                            lv_obj_send_event(child, LV_EVENT_CLICKED, nullptr);
                            break;
                        }
                    }
                }
            }
        }
    }, LV_EVENT_KEY, nullptr);

    // Cleanup
    lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
        FetchDialogData* d = (FetchDialogData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de));
        if (d) {
            free(d->name);
            delete d;
        }
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_set_user_data(dlg, fd);
    // fd and fd->name are owned by the dialog's user_data and freed in the
    // LV_EVENT_DELETE handler above; cppcheck cannot see through the callback.
    // cppcheck-suppress memleak
}

// ════════════════════════════════════════════════════════
// Contact Detail — full info about a single contact
// ════════════════════════════════════════════════════════
void contact_detail_screen_show(const char* contact_name)
{
    if (!contact_name || !contact_name[0]) {
        Serial.println("[ui] contact_detail: empty name");
        return;
    }

    lv_obj_t* scr = make_screen_full("Contact");

    // Look up only the requested contact; the firmware can store 350 contacts.
    sigurdos::mesh::ContactInfo target_info{};
    const sigurdos::mesh::ContactInfo* target = nullptr;
    if (sigurdos::mesh::getContactByName(contact_name, &target_info)) {
        target = &target_info;
    }
    if (!target) {
        lv_obj_t* err = lv_label_create(scr);
        lv_label_set_text(err, "Contact not found");
        lv_obj_set_style_text_color(err, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_text_font(err, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(err, LV_ALIGN_CENTER, 0, 0);
        show_screen(scr);
        return;
    }

    bool is_room_type = (target->type == ADV_TYPE_ROOM || target->type == ADV_TYPE_REPEATER);
    const bool room_logged_in = is_room_type && sigurdos::mesh::getLoginStatus(contact_name) == LOGIN_STATUS_OK;
    const int bottom_reserved_h = responsive::contact_bottom_reserved(is_room_type, room_logged_in);

    // Content area — scrollable vertical column for all fields. Keep it above
    // the fixed bottom action rows so controls never cover contact fields.
    lv_obj_t* list = lv_obj_create(scr);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_H - bottom_reserved_h);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, CONTENT_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    // Helper: add a labeled info row
    auto add_row = [&](const char* label, const char* value, uint32_t color) {
        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_PCT(100), 22);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_left(row, 8, 0);
        lv_obj_set_style_pad_right(row, 8, 0);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_10, 0);

        lv_obj_t* val = lv_label_create(row);
        lv_label_set_text(val, value);
        lv_obj_set_style_text_color(val, lv_color_hex(color), 0);
        lv_obj_set_style_text_font(val, emoji_wrapped_montserrat_12, 0);
        lv_obj_align(val, LV_ALIGN_RIGHT_MID, -8, 0);
    };

    // Name (large, top)
    lv_obj_t* name_l = lv_label_create(list);
    lv_label_set_text(name_l, target->name);
    lv_obj_set_style_text_color(name_l, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(name_l, emoji_wrapped_montserrat_14, 0);
    lv_obj_set_style_pad_left(name_l, 8, 0);
    lv_obj_set_style_pad_bottom(name_l, 4, 0);

    // Node type
    const char* type_str = "Unknown";
    switch (target->type) {
        case ADV_TYPE_CHAT:     type_str = "Chat Node"; break;
        case ADV_TYPE_REPEATER: type_str = "Repeater"; break;
        case ADV_TYPE_ROOM:     type_str = "Room Server"; break;
        case ADV_TYPE_SENSOR:   type_str = "Sensor"; break;
    }
    add_row("Type", type_str, ACCENT);

    // RSSI
    char rssi_buf[16];
    snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm", target->rssi);
    add_row("RSSI", rssi_buf, TEXT_PRIMARY);

    // SNR
    char snr_buf[16];
    snprintf(snr_buf, sizeof(snr_buf), "%.1f dB", target->snr);
    add_row("SNR", snr_buf, TEXT_PRIMARY);

    // Last seen
    if (target->last_seen > 0) {
        // Get contract index to look up path info later
        int cidx = sigurdos::mesh::findContactIndex(contact_name);
        char time_buf[24];
        time_t t = (time_t)target->last_seen;
        struct tm* tm_info = localtime(&t);
        if (tm_info) {
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm_info);
            add_row("Last Seen", time_buf, TEXT_PRIMARY);
        } else {
            add_row("Last Seen", "?", TEXT_SECONDARY);
        }

        // Show path status with hop count
        if (cidx >= 0 && sigurdos::mesh::contactHasPath(cidx)) {
            uint8_t pl = sigurdos::mesh::getContactPathLen(contact_name);
            if (pl > 0 && pl < 0xFF) {
                char path_buf[32];
                snprintf(path_buf, sizeof(path_buf), "Direct (%d hop%s)", pl, pl == 1 ? "" : "s");
                add_row("Path", path_buf, ACCENT_GREEN);
            } else {
                add_row("Path", "Direct", ACCENT_GREEN);
            }
        } else if (cidx >= 0) {
            add_row("Path", "Flood", TEXT_SECONDARY);
        }

        // Show inbound advert path (how the advert reached us)
        uint8_t advert_hops = sigurdos::mesh::getAdvertPathLen(contact_name);
        if (advert_hops > 0) {
            char ad_buf[32];
            snprintf(ad_buf, sizeof(ad_buf), "%d hop%s via mesh", advert_hops, advert_hops == 1 ? "" : "s");
            add_row("Advert Path", ad_buf, ACCENT);
        }
    } else {
        add_row("Last Seen", "N/A", TEXT_SECONDARY);
    }

    // GPS location
    if (target->has_location) {
        char loc_buf[48];
        snprintf(loc_buf, sizeof(loc_buf), "%.4f, %.4f",
                 target->latitude, target->longitude);
        add_row("Location", loc_buf, ACCENT_GREEN);
    } else {
        add_row("Location", "Not shared", TEXT_SECONDARY);
    // ── Login status row (repeater/room only) ─────────
    if (target->type == ADV_TYPE_REPEATER || target->type == ADV_TYPE_ROOM) {
        uint8_t st = sigurdos::mesh::getLoginStatus(contact_name);
        const char* login_text = "Not logged in";
        uint32_t login_color = TEXT_SECONDARY;
        switch (st) {
            case LOGIN_STATUS_PENDING: login_text = "Login pending..."; login_color = ACCENT; break;
            case LOGIN_STATUS_OK:      login_text = "Logged in";        login_color = ACCENT_GREEN; break;
            case LOGIN_STATUS_FAILED:  login_text = "Login failed";     login_color = ACCENT_RED; break;
            case LOGIN_STATUS_TIMEOUT: login_text = "Login timed out";  login_color = ACCENT_RED; break;
            case LOGIN_STATUS_DROPPED: login_text = "Connection dropped"; login_color = ACCENT_RED; break;
        }
        add_row("Login", login_text, login_color);

        // When logged in, show permission level
        if (st == LOGIN_STATUS_OK) {
            uint8_t perm = sigurdos::mesh::getLoginPermission(contact_name);
            char perm_buf[24];
            // Server permission encoding:
            //   1 = Admin, 0 = Read-Write (logged-in non-admin), 2 = Guest (read-only)
            if (perm == 1) {
                snprintf(perm_buf, sizeof(perm_buf), "Admin (perm=%d)", perm);
            } else if (perm == 0) {
                snprintf(perm_buf, sizeof(perm_buf), "Read-Write");
            } else {
                snprintf(perm_buf, sizeof(perm_buf), "Guest");
            }
            add_row("Permission", perm_buf, ACCENT);
        }
    }

    }

    // ── Local ACL display ──────────────────────────
    {
        int acl_perm = sigurdos::mesh::getContactPerm(contact_name);
        const char* perm_names[] = {"Guest", "Read-Only", "Read-Write", "Admin"};
        const char* perm_str = (acl_perm >= 0 && acl_perm <= 3) ? perm_names[acl_perm] : "None";
        char acl_buf[32];
        snprintf(acl_buf, sizeof(acl_buf), "%s", perm_str);
        uint32_t acl_color = (acl_perm >= PERM_ACL_READ_WRITE) ? ACCENT_GREEN : TEXT_SECONDARY;
        add_row("Local ACL", acl_buf, acl_color);
    }

    // ── ACL promote/demote buttons ─────────────────
    {
        lv_obj_t* acl_row = lv_obj_create(scr);
        lv_obj_set_size(acl_row, CONTENT_W, 22);
        lv_obj_align(acl_row, LV_ALIGN_BOTTOM_LEFT, 0,
                     -(BOT_BAR_H + DIVIDER_H + responsive::contact_acl_offset(is_room_type, room_logged_in)));
        lv_obj_set_style_bg_opa(acl_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(acl_row, 0, 0);
        lv_obj_set_flex_flow(acl_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(acl_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* demote_btn = lv_btn_create(acl_row);
        lv_obj_set_size(demote_btn, 70, 20);
        lv_obj_set_style_bg_color(demote_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(demote_btn, 0, 0);
        lv_obj_t* demote_lbl = lv_label_create(demote_btn);
        lv_label_set_text(demote_lbl, "\u25BC Demote");
        lv_obj_center(demote_lbl);
        lv_obj_set_style_text_color(demote_lbl, lv_color_hex(BG_PRIMARY), 0);
        lv_obj_set_style_text_font(demote_lbl, emoji_wrapped_montserrat_10, 0);
        char* acl_demote = strdup(contact_name);
        lv_obj_set_user_data(demote_btn, acl_demote);
        lv_obj_add_event_cb(demote_btn, [](lv_event_t* e) {
            const char* name = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            if (name) {
                int cur = sigurdos::mesh::getContactPerm(name);
                if (cur < 0) cur = PERM_ACL_GUEST;
                int next = (cur == PERM_ACL_GUEST) ? PERM_ACL_GUEST : cur - 1;
                sigurdos::mesh::setContactPerm(name, next);
                sigurdos::ui::contact_detail_screen_show(name);
            }
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(demote_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);

        lv_obj_t* promote_btn = lv_btn_create(acl_row);
        lv_obj_set_size(promote_btn, 70, 20);
        lv_obj_set_style_bg_color(promote_btn, lv_color_hex(ACCENT_GREEN), 0);
        lv_obj_set_style_radius(promote_btn, 0, 0);
        lv_obj_t* promote_lbl = lv_label_create(promote_btn);
        lv_label_set_text(promote_lbl, "\u25B2 Promote");
        lv_obj_center(promote_lbl);
        lv_obj_set_style_text_color(promote_lbl, lv_color_hex(BG_PRIMARY), 0);
        lv_obj_set_style_text_font(promote_lbl, emoji_wrapped_montserrat_10, 0);
        char* acl_promote = strdup(contact_name);
        lv_obj_set_user_data(promote_btn, acl_promote);
        lv_obj_add_event_cb(promote_btn, [](lv_event_t* e) {
            const char* name = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            if (name) {
                int cur = sigurdos::mesh::getContactPerm(name);
                if (cur < 0) cur = PERM_ACL_GUEST;
                int next = (cur >= PERM_ACL_ADMIN) ? PERM_ACL_ADMIN : cur + 1;
                sigurdos::mesh::setContactPerm(name, next);
                sigurdos::ui::contact_detail_screen_show(name);
            }
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(promote_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // ── Action button row ───────────────────────────
    lv_obj_t* btn_row = lv_obj_create(scr);
    lv_obj_set_size(btn_row, CONTENT_W, 30);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_LEFT, 0, -(BOT_BAR_H + DIVIDER_H));
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Send DM button (skip for room/repeater — you don't DM a server)
    if (!is_room_type) {
        lv_obj_t* dm_btn = lv_btn_create(btn_row);
        lv_obj_set_size(dm_btn, 110, 24);
        lv_obj_set_style_bg_color(dm_btn, lv_color_hex(ACCENT), 0);
        lv_obj_set_style_radius(dm_btn, 0, 0);
        lv_obj_t* dm_lbl = lv_label_create(dm_btn);
        lv_label_set_text(dm_lbl, LV_SYMBOL_ENVELOPE " DM");
        lv_obj_center(dm_lbl);
        lv_obj_set_style_text_color(dm_lbl, lv_color_hex(BG_PRIMARY), 0);
        char* dm_name = strdup(contact_name);
        lv_obj_set_user_data(dm_btn, dm_name);
        lv_obj_add_event_cb(dm_btn, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(btn);
            if (name) {
                sigurdos::ui::chat_screen_open_dm(name);
            }
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(dm_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // Send Trace button (skip for room/repeater)
    int trace_idx = sigurdos::mesh::findContactIndex(contact_name);
    if (!is_room_type && trace_idx >= 0) {
        lv_obj_t* trace_btn = lv_btn_create(btn_row);
        lv_obj_set_size(trace_btn, 110, 24);
        lv_obj_set_style_bg_color(trace_btn, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_radius(trace_btn, 0, 0);
        lv_obj_t* trace_lbl = lv_label_create(trace_btn);
        lv_label_set_text(trace_lbl, LV_SYMBOL_SHUFFLE " Trace");
        lv_obj_center(trace_lbl);
        lv_obj_set_style_text_color(trace_lbl, lv_color_hex(TEXT_PRIMARY), 0);
        // Store contact_idx in user_data (value fits in intptr_t, no heap alloc needed)
        lv_obj_set_user_data(trace_btn, (void*)(intptr_t)trace_idx);
        lv_obj_add_event_cb(trace_btn, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
            if (idx >= 0) {
                uint32_t tag = 0;
                sigurdos::mesh::sendTrace(idx, &tag);
                // Brief snackbar-style feedback — navigate to trace screen
                // so the user can see the result
                sigurdos::ui::navigate_to(sigurdos::ui::Screen::Trace);
            }
        }, LV_EVENT_CLICKED, nullptr);
        // No LV_EVENT_DELETE handler needed — no heap allocation
    }

    // Request Status button
    {
        lv_obj_t* st_btn = lv_btn_create(btn_row);
        lv_obj_set_size(st_btn, 75, 24);
        lv_obj_set_style_bg_color(st_btn, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_radius(st_btn, 0, 0);
        lv_obj_t* st_lbl = lv_label_create(st_btn);
        lv_label_set_text(st_lbl, LV_SYMBOL_SETTINGS " Status");
        lv_obj_center(st_lbl);
        lv_obj_set_style_text_color(st_lbl, lv_color_hex(TEXT_PRIMARY), 0);
        char* st_name = strdup(contact_name);
        lv_obj_set_user_data(st_btn, st_name);
        lv_obj_add_event_cb(st_btn, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(btn);
            if (name) {
                sigurdos::mesh::requestStatus(name);
                sigurdos::ui::navigate_to(sigurdos::ui::Screen::NodeStatus);
            }
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(st_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // Request Telemetry button — second row below main buttons (skip for room/repeater)
    if (!is_room_type) {
        lv_obj_t* tm_row = lv_obj_create(scr);
        lv_obj_set_size(tm_row, CONTENT_W, 26);
        lv_obj_align(tm_row, LV_ALIGN_BOTTOM_LEFT, 0, -(BOT_BAR_H + DIVIDER_H + 34));
        lv_obj_set_style_bg_opa(tm_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tm_row, 0, 0);
        lv_obj_set_flex_flow(tm_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tm_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* tm_btn = lv_btn_create(tm_row);
        lv_obj_set_size(tm_btn, 180, 22);
        lv_obj_set_style_bg_color(tm_btn, lv_color_hex(BG_TERTIARY), 0);
        lv_obj_set_style_radius(tm_btn, 0, 0);
        lv_obj_t* tm_lbl = lv_label_create(tm_btn);
        lv_label_set_text(tm_lbl, LV_SYMBOL_WIFI " Telemetry");
        lv_obj_center(tm_lbl);
        lv_obj_set_style_text_color(tm_lbl, lv_color_hex(TEXT_PRIMARY), 0);
        char* tm_name = strdup(contact_name);
        lv_obj_set_user_data(tm_btn, tm_name);
        lv_obj_add_event_cb(tm_btn, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(btn);
            if (name) {
                sigurdos::mesh::requestTelemetry(name);
                sigurdos::ui::navigate_to(sigurdos::ui::Screen::Telemetry);
            }
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(tm_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // ── Share via QR button row ─────────────────────
    {
        lv_obj_t* qr_row = lv_obj_create(scr);
        lv_obj_set_size(qr_row, CONTENT_W, 26);
        lv_obj_align(qr_row, LV_ALIGN_BOTTOM_LEFT, 0,
                     -(BOT_BAR_H + DIVIDER_H + responsive::contact_qr_offset(is_room_type, room_logged_in)));
        lv_obj_set_style_bg_opa(qr_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(qr_row, 0, 0);
        lv_obj_set_flex_flow(qr_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(qr_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* qr_btn = lv_btn_create(qr_row);
        lv_obj_set_size(qr_btn, CONTENT_W - 20, 22);
        apply_pixel_btn(qr_btn);
        lv_obj_t* qr_lbl = lv_label_create(qr_btn);
        lv_label_set_text(qr_lbl, "Share via QR");
        lv_obj_set_style_text_color(qr_lbl, lv_color_hex(BG_PRIMARY), 0);
        lv_obj_center(qr_lbl);

        char* qr_name = strdup(contact_name);
        lv_obj_set_user_data(qr_btn, qr_name);
        lv_obj_add_event_cb(qr_btn, [](lv_event_t* e) {
            lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(btn);
            if (!name) return;

            // Get public key hex for this contact
            char pubkey_hex[65] = {0};
            if (!sigurdos::mesh::getContactPubkeyHex(name, pubkey_hex, sizeof(pubkey_hex))) {
                Serial.println("[qr] Failed to get pubkey for contact");
                return;
            }

            // Determine contact type from the stored type in row's user data
            int ctype = 1; // default ADV_TYPE_CHAT
            // Check if row has type data on its parent
            lv_obj_t* row = lv_obj_get_parent(btn);
            if (row) {
                void* td = lv_obj_get_user_data(row);
                if (td) ctype = (int)(intptr_t)td;
            }

            // Build URI: meshcore://contact/add?name=<url_encoded>&public_key=<64hex>&type=<type>
            char uri[512];
            char enc_name[96];  // worst-case 31 * 3 + NUL
            sigurdos::mesh::urlEncodeQueryValue(name, enc_name, sizeof(enc_name));
            snprintf(uri, sizeof(uri), "meshcore://contact/add?name=%s&public_key=%s&type=%d",
                     enc_name, pubkey_hex, ctype);
            sigurdos::app::qr_show("Share Contact", uri);
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(qr_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
        // Store the contact type on the row object for the callback to retrieve
        lv_obj_set_user_data(qr_row, (void*)(intptr_t)target->type);
    }

    // Remove Contact button
    {
        const char* remove_name = contact_name;
        lv_obj_t* remove_btn = lv_btn_create(btn_row);
        lv_obj_set_size(remove_btn, 110, 24);
        lv_obj_set_style_bg_color(remove_btn, lv_color_hex(ACCENT_RED), 0);
        lv_obj_set_style_radius(remove_btn, 0, 0);
        lv_obj_t* rl = lv_label_create(remove_btn);
        lv_label_set_text(rl, LV_SYMBOL_CLOSE " Remove");
        lv_obj_set_style_text_color(rl, lv_color_hex(0xffffff), 0);
        lv_obj_center(rl);
        char* name_dup = strdup(remove_name);
        lv_obj_set_user_data(remove_btn, name_dup);
        lv_obj_add_event_cb(remove_btn, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(target);
            if (!name) return;
            lv_obj_t* scr = lv_obj_get_screen(target);
            auto dlg_sz = dialog_size(220, 100);
            lv_obj_t* dlg = lv_obj_create(scr);
            lv_obj_set_size(dlg, dlg_sz.w, dlg_sz.h);
            lv_obj_center(dlg);
            lv_obj_set_style_bg_color(dlg, lv_color_hex(BG_SECONDARY), 0);
            lv_obj_set_style_radius(dlg, 0, 0);
            lv_obj_set_style_border_width(dlg, 0, 0);
            lv_obj_set_style_pad_all(dlg, 8, 0);

            lv_obj_t* title = lv_label_create(dlg);
            lv_label_set_text(title, "Remove contact?");
            lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_12, 0);
            lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

            lv_obj_t* msg = lv_label_create(dlg);
            char msg_buf[64];
            snprintf(msg_buf, sizeof(msg_buf), "Remove %s from contacts?", name);
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

            char* cn = strdup(name);
            lv_obj_t* confirm_btn = lv_btn_create(dlg);
            lv_obj_set_size(confirm_btn, 64, 24);
            lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
            lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(ACCENT_RED), 0);
            lv_obj_set_style_radius(confirm_btn, 0, 0);
            lv_obj_t* cfl_lb = lv_label_create(confirm_btn);
            lv_label_set_text(cfl_lb, "Remove");
            lv_obj_center(cfl_lb);
            lv_obj_set_user_data(confirm_btn, cn);
            lv_obj_add_event_cb(confirm_btn, [](lv_event_t* ce) {
                const char* cn = (const char*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ce));
                sigurdos::mesh::removeContact(cn);
                go_back();
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_user_data(dlg, cn);
            lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de)));
            }, LV_EVENT_DELETE, nullptr);
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(remove_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }

    // Reset Path + Discover Path buttons (second action row) — skip for room/repeater
    if (!is_room_type) {
        lv_obj_t* btn_row2 = lv_obj_create(scr);
        lv_obj_set_size(btn_row2, CONTENT_W, 30);
        lv_obj_align(btn_row2, LV_ALIGN_BOTTOM_LEFT, 0, -(BOT_BAR_H + DIVIDER_H + 64));
        lv_obj_set_style_bg_opa(btn_row2, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_row2, 0, 0);
        lv_obj_set_flex_flow(btn_row2, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Reset Path button
        char* rp_name = strdup(contact_name);
        lv_obj_t* rp_btn = lv_btn_create(btn_row2);
        lv_obj_set_size(rp_btn, 140, 24);
        lv_obj_set_style_bg_color(rp_btn, lv_color_hex(ACCENT_ORANGE), 0);
        lv_obj_set_style_radius(rp_btn, 0, 0);
        lv_obj_t* rp_lbl = lv_label_create(rp_btn);
        lv_label_set_text(rp_lbl, LV_SYMBOL_REFRESH " Reset Path");
        lv_obj_set_style_text_color(rp_lbl, lv_color_hex(0xffffff), 0);
        lv_obj_center(rp_lbl);
        lv_obj_set_user_data(rp_btn, rp_name);
        lv_obj_add_event_cb(rp_btn, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(target);
            if (!name) return;
            lv_obj_t* dlg = lv_obj_create(lv_obj_get_screen(target));
            lv_obj_set_size(dlg, 240, 100);
            lv_obj_center(dlg);
            apply_pixel_card_accent(dlg);
            lv_obj_t* msg = lv_label_create(dlg);
            lv_label_set_text(msg, "Reset saved route?\nNext send will flood.");
            lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 6);
            lv_obj_t* cancel = lv_btn_create(dlg);
            lv_obj_set_size(cancel, 80, 24);
            lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 8, -4);
            apply_pixel_btn_outline(cancel);
            lv_obj_t* cancel_l = lv_label_create(cancel);
            lv_label_set_text(cancel_l, "CANCEL");
            lv_obj_center(cancel_l);
            lv_obj_add_event_cb(cancel, [](lv_event_t* ce) {
                lv_obj_del_async(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ce)));
            }, LV_EVENT_CLICKED, nullptr);
            char* confirm_name = strdup(name);
            lv_obj_set_user_data(dlg, confirm_name);
            lv_obj_t* confirm = lv_btn_create(dlg);
            lv_obj_set_size(confirm, 80, 24);
            lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, -8, -4);
            apply_pixel_btn(confirm);
            lv_obj_t* confirm_l = lv_label_create(confirm);
            lv_label_set_text(confirm_l, "RESET");
            lv_obj_center(confirm_l);
            lv_obj_set_user_data(confirm, confirm_name);
            lv_obj_add_event_cb(confirm, [](lv_event_t* ce) {
                const char* value = (const char*)lv_obj_get_user_data(
                    (lv_obj_t*)lv_event_get_target(ce));
                char copy[32] = {};
                if (value) strncpy(copy, value, sizeof(copy) - 1);
                if (copy[0] && sigurdos::mesh::resetPathTo(copy)) {
                    contact_detail_screen_show(copy);
                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(dlg, [](lv_event_t* de) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(de)));
            }, LV_EVENT_DELETE, nullptr);
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(rp_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);

        // Discover Path button
        char* dp_name = strdup(contact_name);
        lv_obj_t* dp_btn = lv_btn_create(btn_row2);
        lv_obj_set_size(dp_btn, 140, 24);
        lv_obj_set_style_bg_color(dp_btn, lv_color_hex(0x0088cc), 0);
        lv_obj_set_style_radius(dp_btn, 0, 0);
        lv_obj_t* dp_lbl = lv_label_create(dp_btn);
        lv_label_set_text(dp_lbl, LV_SYMBOL_DIRECTORY " Discover");
        lv_obj_set_style_text_color(dp_lbl, lv_color_hex(0xffffff), 0);
        lv_obj_center(dp_lbl);
        lv_obj_set_user_data(dp_btn, dp_name);
        lv_obj_add_event_cb(dp_btn, [](lv_event_t* e) {
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            const char* name = (const char*)lv_obj_get_user_data(target);
            if (name) {
                sigurdos::mesh::discoverPath(name);
            }
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(dp_btn, [](lv_event_t* e) {
            free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
        }, LV_EVENT_DELETE, nullptr);
    }


    // ── Login/Logout/Admin Cmd buttons (repeater/room) ──
    if (target->type == ADV_TYPE_REPEATER || target->type == ADV_TYPE_ROOM) {
        uint8_t login_st = sigurdos::mesh::getLoginStatus(contact_name);
        lv_obj_t* login_row = lv_obj_create(scr);
        lv_obj_set_size(login_row, CONTENT_W, 30);
        // Place the login row above the action btn_row so it's always visible.
        // btn_row is at -(BOT_BAR_H + DIVIDER_H) from bottom. Place above it.
        lv_obj_align(login_row, LV_ALIGN_BOTTOM_LEFT, 0, -(BOT_BAR_H + DIVIDER_H + 34));
        lv_obj_set_style_bg_opa(login_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(login_row, 0, 0);
        lv_obj_set_flex_flow(login_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(login_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        if (login_st == LOGIN_STATUS_OK) {
            // ── Admin Cmd button ──
            char* ac_name = strdup(contact_name);
            lv_obj_t* ac_btn = lv_btn_create(login_row);
            lv_obj_set_size(ac_btn, 150, 24);
            lv_obj_set_style_bg_color(ac_btn, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_radius(ac_btn, 0, 0);
            lv_obj_t* ac_lbl = lv_label_create(ac_btn);
            lv_label_set_text(ac_lbl, LV_SYMBOL_SETTINGS " Admin Cmd");
            lv_obj_center(ac_lbl);
            lv_obj_set_style_text_color(ac_lbl, lv_color_hex(BG_PRIMARY), 0);
            lv_obj_set_user_data(ac_btn, ac_name);
            lv_obj_add_event_cb(ac_btn, [](lv_event_t* e) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(btn);
                if (name) show_admin_cmd_dialog(name);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(ac_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);

            // ── Logout button ──
            char* lo_name = strdup(contact_name);
            lv_obj_t* lo_btn = lv_btn_create(login_row);
            lv_obj_set_size(lo_btn, 110, 24);
            lv_obj_set_style_bg_color(lo_btn, lv_color_hex(ACCENT_ORANGE), 0);
            lv_obj_set_style_radius(lo_btn, 0, 0);
            lv_obj_t* lo_lbl = lv_label_create(lo_btn);
            lv_label_set_text(lo_lbl, LV_SYMBOL_REFRESH " Logout");
            lv_obj_center(lo_lbl);
            lv_obj_set_style_text_color(lo_lbl, lv_color_hex(0xffffff), 0);
            lv_obj_set_user_data(lo_btn, lo_name);
            lv_obj_add_event_cb(lo_btn, [](lv_event_t* e) {
                lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(target);
                if (name) {
                    sigurdos::mesh::sendLogout(name);
                    lv_timer_create([](lv_timer_t* t) {
                        go_back();
                        lv_timer_del(t);
                    }, 600, nullptr);
                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(lo_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);

            // ── Fetch Msgs row (second row of buttons) ──
            lv_obj_t* fetch_row = lv_obj_create(scr);
            lv_obj_set_size(fetch_row, CONTENT_W, 30);
            lv_obj_align(fetch_row, LV_ALIGN_BOTTOM_LEFT, 0, -(BOT_BAR_H + DIVIDER_H + 68));
            lv_obj_set_style_bg_opa(fetch_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(fetch_row, 0, 0);
            lv_obj_set_flex_flow(fetch_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(fetch_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // ── Fetch Msgs button ──
            char* fm_name = strdup(contact_name);
            lv_obj_t* fm_btn = lv_btn_create(fetch_row);
            lv_obj_set_size(fm_btn, 180, 24);
            lv_obj_set_style_bg_color(fm_btn, lv_color_hex(0x0088cc), 0);
            lv_obj_set_style_radius(fm_btn, 0, 0);
            lv_obj_t* fm_lbl = lv_label_create(fm_btn);
            lv_label_set_text(fm_lbl, LV_SYMBOL_LIST " Fetch Msgs");
            lv_obj_center(fm_lbl);
            lv_obj_set_style_text_color(fm_lbl, lv_color_hex(0xffffff), 0);
            lv_obj_set_user_data(fm_btn, fm_name);
            lv_obj_add_event_cb(fm_btn, [](lv_event_t* e) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(btn);
                if (name) {
                    show_fetch_msgs_dialog(name);
                }
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(fm_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);

        } else {
            // ── Login button ──
            char* li_name = strdup(contact_name);
            lv_obj_t* li_btn = lv_btn_create(login_row);
            lv_obj_set_size(li_btn, 150, 24);
            lv_obj_set_style_bg_color(li_btn, lv_color_hex(ACCENT), 0);
            lv_obj_set_style_radius(li_btn, 0, 0);
            lv_obj_t* li_lbl = lv_label_create(li_btn);
            lv_label_set_text(li_lbl, LV_SYMBOL_DIRECTORY " Login");
            lv_obj_center(li_lbl);
            lv_obj_set_style_text_color(li_lbl, lv_color_hex(BG_PRIMARY), 0);
            lv_obj_set_user_data(li_btn, li_name);
            lv_obj_add_event_cb(li_btn, [](lv_event_t* e) {
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                const char* name = (const char*)lv_obj_get_user_data(btn);
                if (name) show_login_password_dialog(name);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(li_btn, [](lv_event_t* e) {
                free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)));
            }, LV_EVENT_DELETE, nullptr);

            // When login pending or failed, show Cancel button
            if (login_st == LOGIN_STATUS_PENDING || login_st == LOGIN_STATUS_FAILED ||
                login_st == LOGIN_STATUS_TIMEOUT || login_st == LOGIN_STATUS_DROPPED) {
                char* cx_name = strdup(contact_name);
                lv_obj_t* cx_btn = lv_btn_create(login_row);
                lv_obj_set_size(cx_btn, 100, 24);
                lv_obj_set_style_bg_color(cx_btn, lv_color_hex(BG_TERTIARY), 0);
                lv_obj_set_style_radius(cx_btn, 0, 0);
                lv_obj_t* cx_lbl = lv_label_create(cx_btn);
                lv_label_set_text(cx_lbl, LV_SYMBOL_CLOSE " Cancel");
                lv_obj_center(cx_lbl);
                lv_obj_set_style_text_color(cx_lbl, lv_color_hex(TEXT_SECONDARY), 0);
                lv_obj_set_user_data(cx_btn, cx_name);
                lv_obj_add_event_cb(cx_btn, [](lv_event_t* ce) {
                    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(ce);
                    const char* n = (const char*)lv_obj_get_user_data(t);
                    if (n) {
                        sigurdos::mesh::sendLogout(n); // clears pending/failed state
                        go_back();
                    }
                }, LV_EVENT_CLICKED, nullptr);
                lv_obj_add_event_cb(cx_btn, [](lv_event_t* ce) {
                    free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ce)));
                }, LV_EVENT_DELETE, nullptr);
            }
        }
    }

    show_screen(scr);
}

} // namespace sigurdos::ui
