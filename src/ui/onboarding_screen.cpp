#include "onboarding_screen.h"
#include "navigation.h"
#include "theme.h"
#include "responsive.h"
#include "chat_screen.h"
#include "screens.h"
#include "../hal/prefs.h"
#include "../mesh/mesh_wrapper.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace slopos::ui {

using namespace theme;
using namespace responsive;

// ── Wizard state ─────────────────────────────────────────
static int s_step = 0;
static char s_name[32];
static float s_freq;
static int s_sf;
static int s_pwr;

// ── Persistent widgets ───────────────────────────────────
static lv_obj_t* s_scr = nullptr;
static lv_obj_t* s_content = nullptr;
static lv_obj_t* s_name_input = nullptr;
static lv_obj_t* s_date_input = nullptr;
static lv_obj_t* s_time_input = nullptr;
static lv_obj_t* s_freq_btns[5] = {};
static lv_obj_t* s_sf_label = nullptr;
static lv_obj_t* s_pwr_label = nullptr;

// ── Frequency presets ────────────────────────────────────
static constexpr float FREQ_VALS[] = {868.000f, 869.525f, 869.618f, 915.000f, 433.500f};

static void rebuild_content();
static void show_screen(lv_obj_t* scr);

static void clear_widget_ptrs()
{
    s_name_input = nullptr;
    s_date_input = nullptr;
    s_time_input = nullptr;
    for (auto& b : s_freq_btns) b = nullptr;
    s_sf_label = nullptr;
    s_pwr_label = nullptr;
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
    lv_obj_set_style_text_font(step_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(step_lbl, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* title = lv_label_create(s_content);
    lv_label_set_text(title, "Set Node Name");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t* hint = lv_label_create(s_content);
    lv_label_set_text(hint, "Choose a name other nodes will see.");
    lv_obj_set_style_text_color(hint, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 38);

    s_name_input = lv_textarea_create(s_content);
    lv_obj_set_size(s_name_input, DISPLAY_W - 32, 32);
    lv_obj_align(s_name_input, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_color(s_name_input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(s_name_input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_name_input, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_width(s_name_input, 0, 0);
    lv_textarea_set_one_line(s_name_input, true);
    lv_textarea_set_max_length(s_name_input, 31);
    lv_textarea_set_text(s_name_input, s_name);

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
    lv_obj_set_style_text_font(step_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(step_lbl, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* title = lv_label_create(s_content);
    lv_label_set_text(title, "Set Date & Time");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    int y, mo, d, h, mi;
    slopos::mesh::getCurrentLocalDateTime(&y, &mo, &d, &h, &mi);

    lv_obj_t* dl = lv_label_create(s_content);
    lv_label_set_text(dl, "Date (YYYY-MM-DD):");
    lv_obj_set_style_text_color(dl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_10, 0);
    lv_obj_align(dl, LV_ALIGN_TOP_LEFT, 16, 38);

    char date_buf[16];
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", y, mo, d);
    s_date_input = lv_textarea_create(s_content);
    lv_obj_set_size(s_date_input, 160, 28);
    lv_obj_align(s_date_input, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_date_input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(s_date_input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_date_input, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_width(s_date_input, 0, 0);
    lv_textarea_set_one_line(s_date_input, true);
    lv_textarea_set_text(s_date_input, date_buf);

    lv_obj_t* tl = lv_label_create(s_content);
    lv_label_set_text(tl, "Time (HH:MM 24h):");
    lv_obj_set_style_text_color(tl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_10, 0);
    lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 16, 90);

    char time_buf[8];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", h, mi);
    s_time_input = lv_textarea_create(s_content);
    lv_obj_set_size(s_time_input, 120, 28);
    lv_obj_align(s_time_input, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_style_bg_color(s_time_input, lv_color_hex(BG_INPUT), 0);
    lv_obj_set_style_text_color(s_time_input, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_time_input, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_width(s_time_input, 0, 0);
    lv_textarea_set_one_line(s_time_input, true);
    lv_textarea_set_text(s_time_input, time_buf);

    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, s_date_input);
        lv_group_add_obj(g, s_time_input);
        lv_group_focus_obj(s_date_input);
    }

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
                        ny >= 2020 && nm >= 1 && nm <= 12 && nd >= 1 && nd <= 31);
        bool time_ok = (sscanf(time_s, "%d:%d", &nh, &nmi) == 2 &&
                        nh >= 0 && nh <= 23 && nmi >= 0 && nmi <= 59);

        if (date_ok && time_ok) {
            uint32_t epoch = slopos::mesh::makeEpoch(ny, nm, nd, nh, nmi);
            slopos::mesh::setSystemTime(epoch);
        }

        s_step = 2;
        lv_timer_create([](lv_timer_t* t) { lv_timer_del(t); rebuild_content(); }, 1, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
}

// ═══════════════════════════════════════════════════════════
// Step 3 — Frequency Preset, SF, TX Power
// ═══════════════════════════════════════════════════════════
static void build_step3()
{
    clear_widget_ptrs();

    // Reload from prefs — "Full Radio Setup" may have changed freq/SF/power
    const auto& p = slopos::prefs_get();
    s_freq = p.configured ? p.freq : 869.618f;
    s_sf   = p.configured ? p.sf   : 8;
    s_pwr  = p.configured ? p.tx_power_dbm : 22;

    lv_obj_t* title = lv_label_create(s_content);
    lv_label_set_text(title, "Set Frequency");
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    static const struct { const char* label; float freq; } freqs[] = {
        {"868.000 MHz (EU)", 868.000f},
        {"869.525 MHz (UK)", 869.525f},
        {"869.618 MHz (UK)", 869.618f},
        {"915.000 MHz (US)", 915.000f},
        {"433.500 MHz (EU)", 433.500f},
    };

    int fy = 20;
    for (int i = 0; i < 5; i++) {
        auto* btn = lv_btn_create(s_content);
        lv_obj_set_size(btn, 220, 16);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, fy);
        bool selected = fabsf(s_freq - freqs[i].freq) < 0.001f;
        lv_obj_set_style_bg_color(btn, lv_color_hex(selected ? 0x2a5a2a : BG_TERTIARY), 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        auto* tl = lv_label_create(btn);
        lv_label_set_text(tl, freqs[i].label);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_10, 0);
        lv_obj_center(tl);
        s_freq_btns[i] = btn;

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            s_freq = FREQ_VALS[idx];
            for (int j = 0; j < 5; j++) {
                if (s_freq_btns[j]) {
                    bool sel = (j == idx);
                    lv_obj_set_style_bg_color(s_freq_btns[j],
                        lv_color_hex(sel ? 0x2a5a2a : BG_TERTIARY), 0);
                }
            }
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        fy += 18;
    }

    // SF and TX power on one compact row
    int row_y = fy + 6;
    char sf_buf[16];
    snprintf(sf_buf, sizeof(sf_buf), "SF:%d", s_sf);
    s_sf_label = lv_label_create(s_content);
    lv_label_set_text(s_sf_label, sf_buf);
    lv_obj_set_style_text_color(s_sf_label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_sf_label, &lv_font_montserrat_10, 0);
    lv_obj_align(s_sf_label, LV_ALIGN_TOP_LEFT, 16, row_y);

    auto* sf_minus = lv_btn_create(s_content);
    lv_obj_set_size(sf_minus, 28, 22);
    lv_obj_align(sf_minus, LV_ALIGN_TOP_LEFT, 70, row_y - 2);
    lv_obj_set_style_bg_color(sf_minus, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(sf_minus, 0, 0);
    auto* sml = lv_label_create(sf_minus);
    lv_label_set_text(sml, "-");
    lv_obj_center(sml);
    lv_obj_add_event_cb(sf_minus, [](lv_event_t*) {
        if (s_sf > 6) { s_sf--;
            if (s_sf_label) { char b[16]; snprintf(b, sizeof(b), "SF:%d", s_sf); lv_label_set_text(s_sf_label, b); } }
    }, LV_EVENT_CLICKED, nullptr);

    auto* sf_plus = lv_btn_create(s_content);
    lv_obj_set_size(sf_plus, 28, 22);
    lv_obj_align(sf_plus, LV_ALIGN_TOP_LEFT, 100, row_y - 2);
    lv_obj_set_style_bg_color(sf_plus, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(sf_plus, 0, 0);
    auto* spl = lv_label_create(sf_plus);
    lv_label_set_text(spl, "+");
    lv_obj_center(spl);
    lv_obj_add_event_cb(sf_plus, [](lv_event_t*) {
        if (s_sf < 12) { s_sf++;
            if (s_sf_label) { char b[16]; snprintf(b, sizeof(b), "SF:%d", s_sf); lv_label_set_text(s_sf_label, b); } }
    }, LV_EVENT_CLICKED, nullptr);

    char pwr_buf[24];
    snprintf(pwr_buf, sizeof(pwr_buf), "TX:%d dBm", s_pwr);
    s_pwr_label = lv_label_create(s_content);
    lv_label_set_text(s_pwr_label, pwr_buf);
    lv_obj_set_style_text_color(s_pwr_label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_pwr_label, &lv_font_montserrat_10, 0);
    lv_obj_align(s_pwr_label, LV_ALIGN_TOP_LEFT, 150, row_y);

    auto* pwr_minus = lv_btn_create(s_content);
    lv_obj_set_size(pwr_minus, 28, 22);
    lv_obj_align(pwr_minus, LV_ALIGN_TOP_LEFT, 215, row_y - 2);
    lv_obj_set_style_bg_color(pwr_minus, lv_color_hex(ACCENT_RED), 0);
    lv_obj_set_style_radius(pwr_minus, 0, 0);
    auto* pml = lv_label_create(pwr_minus);
    lv_label_set_text(pml, "-");
    lv_obj_center(pml);
    lv_obj_add_event_cb(pwr_minus, [](lv_event_t*) {
        if (s_pwr > 2) { s_pwr--;
            if (s_pwr_label) { char b[24]; snprintf(b, sizeof(b), "TX:%d dBm", s_pwr); lv_label_set_text(s_pwr_label, b); } }
    }, LV_EVENT_CLICKED, nullptr);

    auto* pwr_plus = lv_btn_create(s_content);
    lv_obj_set_size(pwr_plus, 28, 22);
    lv_obj_align(pwr_plus, LV_ALIGN_TOP_LEFT, 245, row_y - 2);
    lv_obj_set_style_bg_color(pwr_plus, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(pwr_plus, 0, 0);
    auto* ppl = lv_label_create(pwr_plus);
    lv_label_set_text(ppl, "+");
    lv_obj_center(ppl);
    lv_obj_add_event_cb(pwr_plus, [](lv_event_t*) {
        if (s_pwr < 22) { s_pwr++;
            if (s_pwr_label) { char b[24]; snprintf(b, sizeof(b), "TX:%d dBm", s_pwr); lv_label_set_text(s_pwr_label, b); } }
    }, LV_EVENT_CLICKED, nullptr);

    // Custom RF button
    int custom_y = row_y + 28;
    auto* custom_btn = lv_btn_create(s_content);
    lv_obj_set_size(custom_btn, 160, 20);
    lv_obj_align(custom_btn, LV_ALIGN_TOP_MID, 0, custom_y);
    lv_obj_set_style_bg_color(custom_btn, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_radius(custom_btn, 0, 0);
    lv_obj_set_style_border_width(custom_btn, 0, 0);
    auto* ctl = lv_label_create(custom_btn);
    lv_label_set_text(ctl, "Full Radio Setup...");
    lv_obj_set_style_text_font(ctl, &lv_font_montserrat_10, 0);
    lv_obj_center(ctl);
    lv_obj_add_event_cb(custom_btn, [](lv_event_t*) {
        slopos::ui::radio_setup_screen_show();
    }, LV_EVENT_CLICKED, nullptr);

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

    lv_obj_t* done_btn = lv_btn_create(s_content);
    lv_obj_set_size(done_btn, 120, 32);
    lv_obj_align(done_btn, LV_ALIGN_BOTTOM_RIGHT, -16, -4);
    lv_obj_set_style_bg_color(done_btn, lv_color_hex(ACCENT_GREEN), 0);
    lv_obj_set_style_radius(done_btn, 0, 0);
    lv_obj_t* dl = lv_label_create(done_btn);
    lv_label_set_text(dl, LV_SYMBOL_OK "  Done");
    lv_obj_center(dl);
    lv_obj_add_event_cb(done_btn, [](lv_event_t*) {
        slopos::NodePrefs np = slopos::prefs_get();
        strncpy(np.node_name, s_name, sizeof(np.node_name) - 1);
        np.node_name[sizeof(np.node_name) - 1] = '\0';
        np.freq = s_freq;
        np.bw = 62.5f;
        np.sf = (uint8_t)s_sf;
        np.cr = 5;
        np.tx_power_dbm = (int8_t)s_pwr;
        np.configured = true;
        slopos::mesh::setOwnName(s_name);
        slopos::prefs_set(np);
        slopos::mesh::saveChannels();
        chat_save_messages();
        delay(100); // allow SPIFFS writes to complete before restart
        ESP.restart();
    }, LV_EVENT_CLICKED, nullptr);
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
    const slopos::NodePrefs& p = slopos::prefs_get();

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
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_12, 0);
    lv_obj_center(back_icon);

    lv_obj_t* title_lbl = lv_label_create(top);
    lv_label_set_text(title_lbl, "Setup Wizard");
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_10, 0);
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

} // namespace slopos::ui
