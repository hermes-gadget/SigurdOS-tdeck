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
static const sigurdos::RadioProfile* s_radio_profile = nullptr;

// ── Persistent widgets ───────────────────────────────────
static lv_obj_t* s_scr = nullptr;
static lv_obj_t* s_content = nullptr;
static lv_obj_t* s_name_input = nullptr;
static lv_obj_t* s_date_input = nullptr;
static lv_obj_t* s_time_input = nullptr;
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
    s_date_input = nullptr;
    s_time_input = nullptr;
    s_dt_error_label = nullptr;
    for (auto& b : s_profile_btns) b = nullptr;
    s_profile_summary_label = nullptr;
    s_sf_label = nullptr;
    s_pwr_label = nullptr;
}

static void add_to_group(lv_obj_t* obj)
{
    lv_group_t* g = lv_group_get_default();
    if (g && obj) lv_group_add_obj(g, obj);
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
        // Skip date/time step if the clock already has a valid time
        // (set at build time, by companion app, GPS, or Launcher/flasher).
        // A Unix epoch before 2026-06-01 means the clock was never explicitly set.
        uint32_t now = sigurdos::mesh::getCurrentTime();
        s_step = (now > 1751232000) ? 2 : 1;  // 2026-06-01 = 1751232000
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

    // Pre-fill with current RTC values (or defaults if clock not set)
    int y, mo, d, h, mi;
    sigurdos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);
    if (!onboarding_date_valid(y, mo, d)) { y = 2026; mo = 1; d = 1; }
    if (!onboarding_time_valid(h, mi)) { h = 0; mi = 0; }

    lv_obj_t* dl = lv_label_create(s_content);
    lv_label_set_text(dl, "Date (YYYY-MM-DD):");
    lv_obj_set_style_text_color(dl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(dl, LV_ALIGN_TOP_LEFT, 16, 38);

    char date_buf[16];
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", y, mo, d);
    s_date_input = lv_textarea_create(s_content);
    lv_obj_set_size(s_date_input, 160, 28);
    lv_obj_align(s_date_input, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_date_input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(s_date_input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_date_input, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(s_date_input, 0, 0);
    lv_textarea_set_one_line(s_date_input, true);
    lv_textarea_set_text(s_date_input, date_buf);
    apply_focus_style(s_date_input);

    lv_obj_t* tl = lv_label_create(s_content);
    lv_label_set_text(tl, "Time (HH:MM 24h):");
    lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(tl, emoji_wrapped_montserrat_10, 0);
    lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 16, 90);

    char time_buf[8];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", h, mi);
    s_time_input = lv_textarea_create(s_content);
    lv_obj_set_size(s_time_input, 120, 28);
    lv_obj_align(s_time_input, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_style_bg_color(s_time_input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(s_time_input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_time_input, emoji_wrapped_montserrat_10, 0);
    lv_obj_set_style_border_width(s_time_input, 0, 0);
    lv_textarea_set_one_line(s_time_input, true);
    lv_textarea_set_text(s_time_input, time_buf);
    apply_focus_style(s_time_input);

    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, s_date_input);
        lv_group_add_obj(g, s_time_input);
        lv_group_focus_obj(s_date_input);
    }

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
        const char* date_s = s_date_input ? lv_textarea_get_text(s_date_input) : "";
        const char* time_s = s_time_input ? lv_textarea_get_text(s_time_input) : "";

        int ny = 2025, nm = 1, nd = 1, nh = 0, nmi = 0;
        bool date_ok = (sscanf(date_s, "%d-%d-%d", &ny, &nm, &nd) == 3 &&
                        onboarding_date_valid(ny, nm, nd));
        bool time_ok = (sscanf(time_s, "%d:%d", &nh, &nmi) == 2 &&
                        onboarding_time_valid(nh, nmi));

        if (!date_ok || !time_ok) {
            if (s_dt_error_label) lv_label_set_text(s_dt_error_label, "Invalid date/time");
            return;
        }

        uint32_t epoch = sigurdos::mesh::makeEpoch(ny, nm, nd, nh, nmi);
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
    screens_clear_wifi_icon();
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();

    strncpy(s_name, p.node_name, sizeof(s_name) - 1);
    s_name[sizeof(s_name) - 1] = '\0';
    s_freq = p.configured ? p.freq : 869.618f;
    s_sf   = p.configured ? p.sf   : 8;
    s_pwr  = p.configured ? p.tx_power_dbm : 22;
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
    lv_obj_t* old_scr = lv_screen_active();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    if (old_scr && old_scr != scr) lv_obj_del_async(old_scr);
}

} // namespace sigurdos::ui
