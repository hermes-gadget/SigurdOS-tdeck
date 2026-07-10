#include "onboarding_screen.h"
#include "navigation.h"
#include "theme.h"
#include "responsive.h"
#include "chat_screen.h"
#include "screens.h"
#include "../fonts/emoji_font.h"
#include "../hal/prefs.h"
#include "../hal/radio_profiles.h"
#include "../mesh/mesh_wrapper.h"
#include <Arduino.h>
#include <lvgl.h>
#include <SPIFFS.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>

namespace sigurdos::ui {

using namespace theme;
using namespace responsive;

// ── Wizard state ─────────────────────────────────────────
static int s_step = 0;
static char s_name[32];
static float s_freq;
static int s_sf;
static int s_pwr;
static int s_dt_year;
static int s_dt_month;
static int s_dt_day;
static int s_dt_hour;
static int s_dt_minute;
static bool s_dt_loaded = false;
static const sigurdos::RadioProfile* s_radio_profile = nullptr;

// ── Persistent widgets ───────────────────────────────────
static lv_obj_t* s_scr = nullptr;
static lv_obj_t* s_content = nullptr;
static lv_obj_t* s_name_input = nullptr;
static lv_obj_t* s_dt_value_labels[5] = {};
static lv_obj_t* s_dt_error_label = nullptr;
static lv_obj_t* s_profile_btns[8] = {};
static lv_obj_t* s_profile_summary_label = nullptr;
static lv_obj_t* s_sf_label = nullptr;
static lv_obj_t* s_pwr_label = nullptr;

static void rebuild_content();
static void show_screen(lv_obj_t* scr);

static void clear_widget_ptrs()
{
    s_name_input = nullptr;
    for (auto& l : s_dt_value_labels) l = nullptr;
    s_dt_error_label = nullptr;
    for (auto& b : s_profile_btns) b = nullptr;
    s_profile_summary_label = nullptr;
    s_sf_label = nullptr;
    s_pwr_label = nullptr;
}

enum DateTimeField : int {
    DT_YEAR = 0,
    DT_MONTH,
    DT_DAY,
    DT_HOUR,
    DT_MINUTE,
    DT_FIELD_COUNT,
};

static void load_current_datetime_once()
{
    if (s_dt_loaded) return;
    sigurdos::mesh::getCurrentLocalDateTime(&s_dt_year, &s_dt_month, &s_dt_day,
                                            &s_dt_hour, &s_dt_minute);
    if (!onboarding_date_valid(s_dt_year, s_dt_month, s_dt_day)) {
        s_dt_year = 2026;
        s_dt_month = 1;
        s_dt_day = 1;
    }
    if (!onboarding_time_valid(s_dt_hour, s_dt_minute)) {
        s_dt_hour = 0;
        s_dt_minute = 0;
    }
    s_dt_loaded = true;
}

static void update_datetime_labels()
{
    char buf[16];
    if (s_dt_value_labels[DT_YEAR]) {
        snprintf(buf, sizeof(buf), "%04d", s_dt_year);
        lv_label_set_text(s_dt_value_labels[DT_YEAR], buf);
    }
    if (s_dt_value_labels[DT_MONTH]) {
        snprintf(buf, sizeof(buf), "%02d", s_dt_month);
        lv_label_set_text(s_dt_value_labels[DT_MONTH], buf);
    }
    if (s_dt_value_labels[DT_DAY]) {
        snprintf(buf, sizeof(buf), "%02d", s_dt_day);
        lv_label_set_text(s_dt_value_labels[DT_DAY], buf);
    }
    if (s_dt_value_labels[DT_HOUR]) {
        snprintf(buf, sizeof(buf), "%02d", s_dt_hour);
        lv_label_set_text(s_dt_value_labels[DT_HOUR], buf);
    }
    if (s_dt_value_labels[DT_MINUTE]) {
        snprintf(buf, sizeof(buf), "%02d", s_dt_minute);
        lv_label_set_text(s_dt_value_labels[DT_MINUTE], buf);
    }
}

static void adjust_datetime_field(DateTimeField field, int delta)
{
    switch (field) {
    case DT_YEAR:
        s_dt_year = onboarding_wrap_range(s_dt_year + delta, 2020, 2106);
        s_dt_day = onboarding_clamp_day(s_dt_year, s_dt_month, s_dt_day);
        break;
    case DT_MONTH:
        s_dt_month = onboarding_wrap_range(s_dt_month + delta, 1, 12);
        s_dt_day = onboarding_clamp_day(s_dt_year, s_dt_month, s_dt_day);
        break;
    case DT_DAY:
        s_dt_day = onboarding_wrap_range(
            s_dt_day + delta, 1,
            (int)onboarding_days_in_month(s_dt_year, s_dt_month));
        break;
    case DT_HOUR:
        s_dt_hour = onboarding_wrap_range(s_dt_hour + delta, 0, 23);
        break;
    case DT_MINUTE:
        s_dt_minute = onboarding_wrap_range(s_dt_minute + delta, 0, 59);
        break;
    default:
        break;
    }
    if (s_dt_error_label) lv_label_set_text(s_dt_error_label, "");
    update_datetime_labels();
}

static void datetime_adjust_cb(lv_event_t* e)
{
    intptr_t packed = (intptr_t)lv_event_get_user_data(e);
    DateTimeField field = (DateTimeField)((packed >> 8) & 0xFF);
    int delta = (packed & 0xFF) == 1 ? 1 : -1;
    adjust_datetime_field(field, delta);
}

static void add_to_group(lv_obj_t* obj)
{
    lv_group_t* g = lv_group_get_default();
    if (g && obj) lv_group_add_obj(g, obj);
}

static lv_obj_t* make_small_button(lv_obj_t* parent, int w, int h,
                                   const char* text, uint32_t color)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    apply_focus_style(btn);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_center(lbl);
    add_to_group(btn);
    return btn;
}

static void add_datetime_row(const char* label, DateTimeField field, int y)
{
    lv_obj_t* name = lv_label_create(s_content);
    lv_label_set_text(name, label);
    lv_obj_set_style_text_color(name, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(name, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 16, y + 5);

    lv_obj_t* minus = make_small_button(s_content, 32, 22, "-", ACCENT_RED);
    lv_obj_align(minus, LV_ALIGN_TOP_LEFT, 88, y);
    lv_obj_add_event_cb(minus, datetime_adjust_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)(((int)field << 8) | 0));

    lv_obj_t* value = make_small_button(s_content, 78, 22, "", BG_INPUT);
    lv_obj_align(value, LV_ALIGN_TOP_LEFT, 126, y);
    s_dt_value_labels[field] = lv_obj_get_child(value, 0);
    lv_obj_add_event_cb(value, datetime_adjust_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)(((int)field << 8) | 1));

    lv_obj_t* plus = make_small_button(s_content, 32, 22, "+", ACCENT);
    lv_obj_align(plus, LV_ALIGN_TOP_LEFT, 210, y);
    lv_obj_add_event_cb(plus, datetime_adjust_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)(((int)field << 8) | 1));
}

static void select_radio_profile(const sigurdos::RadioProfile* profile)
{
    if (!profile) return;
    s_radio_profile = profile;
    s_freq = profile->freq_mhz;
    s_sf = profile->sf;
    s_pwr = profile->tx_power_dbm;

    const size_t count = sigurdos::radio_profile_count();
    for (size_t i = 0; i < count && i < (sizeof(s_profile_btns) / sizeof(s_profile_btns[0])); ++i) {
        if (!s_profile_btns[i]) continue;
        const auto* p = sigurdos::radio_profile_at(i);
        const bool selected = p && strcmp(p->id, profile->id) == 0;
        lv_obj_set_style_bg_color(s_profile_btns[i],
                                  lv_color_hex(selected ? 0x2a5a2a : BG_TERTIARY), 0);
    }
    if (s_profile_summary_label) {
        char summary[96];
        snprintf(summary, sizeof(summary), "%.3f MHz / SF%d / BW%.1f / CR4/%d",
                 profile->freq_mhz, profile->sf, profile->bw_khz, profile->cr);
        lv_label_set_text(s_profile_summary_label, summary);
    }
}

// ═══════════════════════════════════════════════════════════
// Step 1 — Node Name
// ═══════════════════════════════════════════════════════════
static void build_step1()
{
    clear_widget_ptrs();

    lv_obj_t* step_lbl = lv_label_create(s_content);
    lv_label_set_text(step_lbl, "Step 1 of 3");
    lv_obj_set_style_text_color(step_lbl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(step_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(step_lbl, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* title = lv_label_create(s_content);
    lv_label_set_text(title, "Set Node Name");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t* hint = lv_label_create(s_content);
    lv_label_set_text(hint, "Choose a name other nodes will see.");
    lv_obj_set_style_text_color(hint, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(hint, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 38);

    s_name_input = lv_textarea_create(s_content);
    lv_obj_set_size(s_name_input, DISPLAY_W - 32, 32);
    lv_obj_align(s_name_input, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_color(s_name_input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(s_name_input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_name_input, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(s_name_input, 0, 0);
    lv_textarea_set_one_line(s_name_input, true);
    lv_textarea_set_max_length(s_name_input, 31);
    lv_textarea_set_text(s_name_input, s_name);
    apply_focus_style(s_name_input);

    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, s_name_input);
        lv_group_focus_obj(s_name_input);
    }

    lv_obj_t* next_btn = lv_btn_create(s_content);
    lv_obj_set_size(next_btn, 120, 32);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(next_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(next_btn, 0, 0);
    lv_obj_t* nl = lv_label_create(next_btn);
    lv_label_set_text(nl, "Next  " LV_SYMBOL_RIGHT);
    lv_obj_center(nl);
    lv_obj_add_event_cb(next_btn, [](lv_event_t*) {
        if (s_name_input) {
            const char* text = lv_textarea_get_text(s_name_input);
            if (text && text[0]) {
                strncpy(s_name, text, sizeof(s_name) - 1);
                s_name[sizeof(s_name) - 1] = '\0';
            } else {
                return;  // reject empty name
            }
        }
        s_step = 1;
        // Use timer to defer rebuild (safer inside LVGL event)
        lv_timer_create([](lv_timer_t* t) { lv_timer_del(t); rebuild_content(); }, 1, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    add_to_group(next_btn);
}

// ═══════════════════════════════════════════════════════════
// Step 2 — Date & Time
// ═══════════════════════════════════════════════════════════
static void build_step2()
{
    clear_widget_ptrs();
    load_current_datetime_once();

    lv_obj_t* step_lbl = lv_label_create(s_content);
    lv_label_set_text(step_lbl, "Step 2 of 3");
    lv_obj_set_style_text_color(step_lbl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(step_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(step_lbl, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* title = lv_label_create(s_content);
    lv_label_set_text(title, "Set Date & Time");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t* hint = lv_label_create(s_content);
    lv_label_set_text(hint, "Tap +/- or focus a value with the trackball.");
    lv_obj_set_width(hint, DISPLAY_W - 24);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(hint, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 36);

    add_datetime_row("Year", DT_YEAR, 52);
    add_datetime_row("Month", DT_MONTH, 75);
    add_datetime_row("Day", DT_DAY, 98);
    add_datetime_row("Hour", DT_HOUR, 121);
    add_datetime_row("Minute", DT_MINUTE, 144);
    update_datetime_labels();

    s_dt_error_label = lv_label_create(s_content);
    lv_label_set_text(s_dt_error_label, "");
    lv_obj_set_width(s_dt_error_label, DISPLAY_W - 32);
    lv_obj_set_style_text_align(s_dt_error_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_dt_error_label, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_text_font(s_dt_error_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(s_dt_error_label, LV_ALIGN_BOTTOM_MID, 0, -42);

    lv_obj_t* back_btn = lv_btn_create(s_content);
    lv_obj_set_size(back_btn, 100, 32);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 16, -8);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(back_btn, 0, 0);
    lv_obj_t* bl = lv_label_create(back_btn);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  Back");
    lv_obj_center(bl);
    lv_obj_add_event_cb(back_btn, [](lv_event_t*) {
        s_step = 0;
        lv_timer_create([](lv_timer_t* t) { lv_timer_del(t); rebuild_content(); }, 1, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    add_to_group(back_btn);

    lv_obj_t* next_btn = lv_btn_create(s_content);
    lv_obj_set_size(next_btn, 120, 32);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_RIGHT, -16, -8);
    lv_obj_set_style_bg_color(next_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(next_btn, 0, 0);
    lv_obj_t* nl = lv_label_create(next_btn);
    lv_label_set_text(nl, "Next  " LV_SYMBOL_RIGHT);
    lv_obj_center(nl);
    lv_obj_add_event_cb(next_btn, [](lv_event_t*) {
        if (!onboarding_date_valid(s_dt_year, s_dt_month, s_dt_day) ||
            !onboarding_time_valid(s_dt_hour, s_dt_minute)) {
            if (s_dt_error_label) lv_label_set_text(s_dt_error_label, "Invalid date/time");
            return;
        }

        uint32_t epoch = sigurdos::mesh::makeEpoch(
            s_dt_year, s_dt_month, s_dt_day, s_dt_hour, s_dt_minute);
        if (!sigurdos::mesh::setSystemTime(epoch)) {
            if (s_dt_error_label) lv_label_set_text(s_dt_error_label, "Clock not ready");
            return;
        }

        s_step = 2;
        lv_timer_create([](lv_timer_t* t) { lv_timer_del(t); rebuild_content(); }, 1, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    add_to_group(next_btn);
}

// ═══════════════════════════════════════════════════════════
// Step 3 — Frequency Preset, SF, TX Power
// ═══════════════════════════════════════════════════════════
static void build_step3()
{
    clear_widget_ptrs();

    // Reload from prefs — "Full Radio Setup" may have changed freq/SF/power
    const auto& p = sigurdos::prefs_get();
    s_freq = p.configured ? p.freq : 869.618f;
    s_sf   = p.configured ? p.sf   : 8;
    s_pwr  = p.configured ? p.tx_power_dbm : 22;
    s_radio_profile = p.configured ? sigurdos::radio_profile_match(p)
                                   : sigurdos::radio_profile_default();
    if (!s_radio_profile) s_radio_profile = sigurdos::radio_profile_default();

    lv_obj_t* title = lv_label_create(s_content);
    lv_label_set_text(title, "Country Profile");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, emoji_wrapped_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* hint = lv_label_create(s_content);
    lv_label_set_text(hint, "Choose the preset for where this device will be used.");
    lv_obj_set_width(hint, DISPLAY_W - 24);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(hint, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 20);

    int fy = 42;
    const size_t count = sigurdos::radio_profile_count();
    const size_t max_buttons = sizeof(s_profile_btns) / sizeof(s_profile_btns[0]);
    for (size_t i = 0; i < count && i < max_buttons; i++) {
        const auto* profile = sigurdos::radio_profile_at(i);
        if (!profile) continue;
        auto* btn = lv_btn_create(s_content);
        lv_obj_set_size(btn, 230, 20);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, fy);
        bool selected = s_radio_profile && strcmp(s_radio_profile->id, profile->id) == 0;
        lv_obj_set_style_bg_color(btn, lv_color_hex(selected ? 0x2a5a2a : BG_TERTIARY), 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        apply_focus_style(btn);
        auto* tl = lv_label_create(btn);
        char label[96];
        snprintf(label, sizeof(label), "%s  %.3f/SF%d/BW%.1f",
                 profile->short_label, profile->freq_mhz, profile->sf, profile->bw_khz);
        lv_label_set_text(tl, label);
        lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(tl, emoji_wrapped_montserrat_10, 0);
        lv_obj_center(tl);
        s_profile_btns[i] = btn;
        add_to_group(btn);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            size_t idx = (size_t)(intptr_t)lv_event_get_user_data(e);
            select_radio_profile(sigurdos::radio_profile_at(idx));
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        fy += 23;
    }

    // Summary row reflects the selected profile.
    char summary[96];
    if (s_radio_profile) {
        snprintf(summary, sizeof(summary), "%.3f MHz / SF%d / BW%.1f / CR4/%d",
                 s_radio_profile->freq_mhz, s_radio_profile->sf,
                 s_radio_profile->bw_khz, s_radio_profile->cr);
    } else {
        snprintf(summary, sizeof(summary), "%.3f MHz / SF%d", s_freq, s_sf);
    }
    s_profile_summary_label = lv_label_create(s_content);
    lv_label_set_text(s_profile_summary_label, summary);
    lv_obj_set_width(s_profile_summary_label, DISPLAY_W - 24);
    lv_obj_set_style_text_align(s_profile_summary_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_profile_summary_label, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(s_profile_summary_label, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(s_profile_summary_label, LV_ALIGN_TOP_MID, 0, fy + 4);

    // Bottom: Back + Done
    lv_obj_t* back_btn = lv_btn_create(s_content);
    lv_obj_set_size(back_btn, 100, 32);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 16, -4);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(BG_TERTIARY), 0);
    lv_obj_set_style_radius(back_btn, 0, 0);
    lv_obj_t* bl = lv_label_create(back_btn);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  Back");
    lv_obj_center(bl);
    lv_obj_add_event_cb(back_btn, [](lv_event_t*) {
        s_step = 1;
        lv_timer_create([](lv_timer_t* t) { lv_timer_del(t); rebuild_content(); }, 1, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    add_to_group(back_btn);

    lv_obj_t* done_btn = lv_btn_create(s_content);
    lv_obj_set_size(done_btn, 120, 32);
    lv_obj_align(done_btn, LV_ALIGN_BOTTOM_RIGHT, -16, -4);
    lv_obj_set_style_bg_color(done_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(done_btn, 0, 0);
    lv_obj_t* dl = lv_label_create(done_btn);
    lv_label_set_text(dl, LV_SYMBOL_OK "  Done");
    lv_obj_center(dl);
    lv_obj_add_event_cb(done_btn, [](lv_event_t*) {
        sigurdos::NodePrefs np = sigurdos::prefs_get();
        strncpy(np.node_name, s_name, sizeof(np.node_name) - 1);
        np.node_name[sizeof(np.node_name) - 1] = '\0';
        if (!s_radio_profile) s_radio_profile = sigurdos::radio_profile_default();
        if (s_radio_profile) {
            sigurdos::radio_profile_apply(*s_radio_profile, np);
        }
        sigurdos::mesh::setOwnName(s_name);
        sigurdos::prefs_set(np);
        // Auto-join the Public channel so new devices can receive group messages
        // immediately. Without this, a freshly-flashed device has zero channels
        // and cannot decrypt any group traffic after restart.
        sigurdos::mesh::joinPublicChannel();
        sigurdos::mesh::saveChannels();
        chat_save_messages();
        // Flush and wait for flash writes to complete before restart
        SPIFFS.end();
        delay(200);
        ESP.restart();
    }, LV_EVENT_CLICKED, nullptr);
    add_to_group(done_btn);
}

// ═══════════════════════════════════════════════════════════
// Rebuild content for current step
// ═══════════════════════════════════════════════════════════
static void rebuild_content()
{
    if (!s_content) return;
    lv_obj_clean(s_content);

    switch (s_step) {
    case 0: build_step1(); break;
    case 1: build_step2(); break;
    case 2: build_step3(); break;
    }
}

// ═══════════════════════════════════════════════════════════
// Entry point
// ═══════════════════════════════════════════════════════════
void onboarding_screen_show()
{
    screens_clear_back_btn();
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();

    strncpy(s_name, p.node_name, sizeof(s_name) - 1);
    s_name[sizeof(s_name) - 1] = '\0';
    s_freq = p.configured ? p.freq : 869.618f;
    s_sf   = p.configured ? p.sf   : 8;
    s_pwr  = p.configured ? p.tx_power_dbm : 22;
    s_dt_loaded = false;
    s_step = 0;

    clear_widget_ptrs();

    s_scr = lv_obj_create(nullptr);
    apply_dark_bg(s_scr);

    // ── Top bar ──────────────────────────────────────────
    lv_obj_t* top = lv_obj_create(s_scr);
    lv_obj_set_size(top, LV_PCT(100), TOP_BAR_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(BG_SECONDARY), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);

    lv_obj_t* back = lv_btn_create(top);
    lv_obj_set_size(back, 24, TOP_BAR_H - 4);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
    apply_topbar_icon_btn(back);
    lv_obj_add_event_cb(back, [](lv_event_t*) { go_back(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_text_font(back_icon, emoji_wrapped_montserrat_12, 0);
    lv_obj_center(back_icon);

    lv_obj_t* title_lbl = lv_label_create(top);
    lv_label_set_text(title_lbl, "Setup Wizard");
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(title_lbl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* tdiv = lv_obj_create(s_scr);
    lv_obj_set_size(tdiv, LV_PCT(100), DIVIDER_H);
    lv_obj_align(tdiv, LV_ALIGN_TOP_MID, 0, TOP_BAR_H);
    lv_obj_set_style_bg_color(tdiv, lv_color_hex(DIVIDER), 0);
    lv_obj_set_style_bg_opa(tdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tdiv, 0, 0);

    // ── Content area ─────────────────────────────────────
    int content_y = TOP_BAR_H + DIVIDER_H;
    int content_h = DISPLAY_H - content_y;

    s_content = lv_obj_create(s_scr);
    lv_obj_set_size(s_content, LV_PCT(100), content_h);
    lv_obj_align(s_content, LV_ALIGN_TOP_MID, 0, content_y);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);

    lv_obj_add_event_cb(s_scr, [](lv_event_t*) {
        s_scr = nullptr;
        s_content = nullptr;
        clear_widget_ptrs();
    }, LV_EVENT_DELETE, nullptr);

    rebuild_content();
    show_screen(s_scr);
}

static void show_screen(lv_obj_t* scr)
{
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
}

} // namespace sigurdos::ui
