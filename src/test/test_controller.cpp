// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Remote test controller — serial command interface for simulating user input.
//
// Commands are read from Serial (USB CDC) and dispatched to HAL/UI/mesh
// injection points. No LoRa radio is initialised — all mesh messages are
// simulated. This is a testing tool, not a backdoor.
//
// Commands:
//   help                          Show this help
//   nav <screen>                  Navigate to screen
//   back                          Go back
//   tb up|down|left|right|click   Simulate trackball event
//   type <text>                   Type text via keyboard simulation
//   press enter|backspace|esc     Press special key
//   inject <from> <text>          Simulate incoming DM
//   inject <from> channel=<ch> <text>  Simulate incoming channel msg
//   screen                        Show current screen name
//   status                        Show device info (heap, psram, batt)
//   term-log                      Dump terminal log content to serial
//   term-clear                    Clear terminal log
//   term-submit <text>            Submit a command directly to the terminal

#include "test_controller.h"
#include "hal/display.h"
#include "hal/trackball.h"
#include "hal/keyboard.h"
#include "hal/prefs.h"
#include "mesh/mesh_wrapper.h"
#include "ui/navigation.h"
#include "diagnostics/debug.h"
#include "ui/screens.h"
#include "fonts/emoji_font.h"
#include "fonts/emoji_data.h"
#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <lvgl.h>
#include <others/snapshot/lv_snapshot.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

// ── Forward declarations ─────────────────────────────────
static void dump_focused_widget();

// ── Constants ────────────────────────────────────────────
static constexpr uint32_t CMD_POLL_MS = 50;   // check Serial every 50ms
static constexpr size_t   CMD_BUF_SIZE = 256;
static constexpr size_t   TYPE_BUF_SIZE = 256;   // max chars in type queue
static constexpr size_t   TYPE_CHUNK_DELAY = 120; // ms between injected chars, must give LVGL time to consume

// ── State ────────────────────────────────────────────────
static bool     initialized = false;
static uint32_t last_poll_ms = 0;
static char     cmd_buf[CMD_BUF_SIZE];
static size_t   cmd_pos = 0;

// Type queue — drained one char per loop iteration so LVGL can consume each keypress
static char     type_buf[TYPE_BUF_SIZE];
static size_t   type_pos = 0;    // next char to inject
static size_t   type_count = 0;  // total chars in queue
static uint32_t type_last_inject_ms = 0;

// ── Screen name lookup ───────────────────────────────────
struct ScreenEntry {
    const char* name;
    slopos::ui::Screen screen;
};

static const ScreenEntry screen_table[] = {
    {"home",        slopos::ui::Screen::Home},
    {"chat",        slopos::ui::Screen::Chat},
    {"contacts",    slopos::ui::Screen::Contacts},
    {"channels",    slopos::ui::Screen::Channels},
    {"network",     slopos::ui::Screen::Network},
    {"heard",       slopos::ui::Screen::Heard},
    {"map",         slopos::ui::Screen::Map},
    {"advertise",   slopos::ui::Screen::Advertise},
    {"settings",    slopos::ui::Screen::Settings},
    {"trace",       slopos::ui::Screen::Trace},
    {"terminal",    slopos::ui::Screen::Terminal},
    {"signal",      slopos::ui::Screen::Signal},
    {"radio",       slopos::ui::Screen::RadioSetup},
    {"onboarding",  slopos::ui::Screen::Onboarding},
    {"contactdetail", slopos::ui::Screen::ContactDetail},
};

static const char* screen_name(slopos::ui::Screen s) {
    for (auto& e : screen_table) {
        if (e.screen == s) return e.name;
    }
    return "unknown";
}

static slopos::ui::Screen screen_from_name(const char* name) {
    for (auto& e : screen_table) {
        if (strcmp(e.name, name) == 0) return e.screen;
    }
    return slopos::ui::Screen::COUNT;  // invalid sentinel
}

// ── Help text ────────────────────────────────────────────
static void print_help() {
    Serial.println(F("╔══════════════════════════════════════╗"));
    Serial.println(F("║  SlopOS Remote Test Controller      ║"));
    Serial.println(F("╠══════════════════════════════════════╣"));
    Serial.println(F("║  Commands:                          ║"));
    Serial.println(F("║  help        Show this help          ║"));
    Serial.println(F("║  nav <scr>   Navigate to screen      ║"));
    Serial.println(F("║  back        Go back                 ║"));
    Serial.println(F("║  tb <dir>    Trackball (u/d/l/r/c)   ║"));
    Serial.println(F("║  type <txt>  Type text               ║"));
    Serial.println(F("║  press <key> Press Enter/Bksp/Esc    ║"));
    Serial.println(F("║  inject <from> [channel=<ch>] <msg>  ║"));
    Serial.println(F("║  sendchannel <ch> <text>          Send on a channel        ║"));
    Serial.println(F("║  addchannel <name> [psk]          Add channel              ║"));
    Serial.println(F("║  screen      Show current screen     ║"));
    Serial.println(F("║  status      Show device state       ║"));
    Serial.println(F("║  debug <level>  Set debug level (1=quiet, 2=normal, 3=verbose)║"));
    Serial.println(F("║  debug <feat> <1|0>  Toggle feature: display/mesh/ui/map/diag║"));
    Serial.println(F("║  debug all <1|0>     Enable/disable all debug features      ║"));
    Serial.println(F("║  term-log    Dump terminal log       ║"));
    Serial.println(F("║  term-clear  Clear terminal log      ║"));
    Serial.println(F("║  term-submit <cmd>  Run cmd in terminal║"));
    Serial.println(F("║  emoji       Show emoji test grid     ║"));
    Serial.println(F("║  emoji-ac <p> Emoji autocomplete test ║"));
    Serial.println(F("║  capture     Capture framebuffer(hex)║"));
    Serial.println(F("║  tree        Dump LVGL widget tree   ║"));
    Serial.println(F("║  widgets     List visible widgets    ║"));
    Serial.println(F("║  tap <x> <y> Sim touch at coords    ║"));
    Serial.println(F("║  backlight   Get/set backlight bri  ║"));
    Serial.println(F("║  setrf <freq> <sf> <bw> <cr> <pwr>  ║"));
    Serial.println(F("║           Set radio params in NVS  ║"));
    Serial.println(F("║  reboot      Reboot the device     ║"));
    Serial.println(F("║  advert      Send self advert      ║"));
    Serial.println(F("╚══════════════════════════════════════╝"));
    Serial.println();
}

// ── Command dispatcher ───────────────────────────────────

static void cmd_navigate(const char* arg) {
    slopos::ui::Screen s = screen_from_name(arg);
    if (s == slopos::ui::Screen::COUNT) {
        Serial.printf("[test] unknown screen: %s\n", arg);
        return;
    }
    slopos::ui::navigate_to(s);
    Serial.printf("[test] nav -> %s\n", arg);
}

static void cmd_back() {
    if (slopos::ui::can_go_back()) {
        slopos::ui::go_back();
        Serial.printf("[test] back -> %s\n",
                      screen_name(slopos::ui::current_screen()));
    } else {
        Serial.println("[test] back: no navigation history");
    }
}

static void cmd_trackball(const char* arg) {
    SlopOSTrackballEvent ev = SlopOSTrackballEvent::None;
    if (strcmp(arg, "up") == 0)        ev = SlopOSTrackballEvent::Up;
    else if (strcmp(arg, "down") == 0) ev = SlopOSTrackballEvent::Down;
    else if (strcmp(arg, "left") == 0) ev = SlopOSTrackballEvent::Left;
    else if (strcmp(arg, "right") == 0) ev = SlopOSTrackballEvent::Right;
    else if (strcmp(arg, "click") == 0 || strcmp(arg, "c") == 0)
        ev = SlopOSTrackballEvent::Click;
    else if (strcmp(arg, "u") == 0)    ev = SlopOSTrackballEvent::Up;
    else if (strcmp(arg, "d") == 0)    ev = SlopOSTrackballEvent::Down;
    else if (strcmp(arg, "l") == 0)    ev = SlopOSTrackballEvent::Left;
    else if (strcmp(arg, "r") == 0)    ev = SlopOSTrackballEvent::Right;

    if (ev == SlopOSTrackballEvent::None) {
        Serial.printf("[test] tb: unknown direction \"%s\" (use up/down/left/right/click)\n", arg);
        return;
    }
    slopos_trackball_inject(ev);
    Serial.printf("[test] tb %s\n", arg);
}

static void cmd_type(const char* text) {
    if (!text || text[0] == '\0') {
        Serial.println("[test] type: no text provided");
        return;
    }
    // Don't overwrite an in-progress type
    if (type_count > 0) {
        Serial.println("[test] type: still typing previous text, wait for it to finish");
        return;
    }
    type_pos = 0;
    type_count = 0;
    size_t len = strlen(text);
    if (len > TYPE_BUF_SIZE - 1) len = TYPE_BUF_SIZE - 1;
    memcpy(type_buf, text, len);
    type_buf[len] = '\0';
    type_count = len;
    type_last_inject_ms = 0;  // inject first char on next loop
    Serial.printf("[test] queued %d chars to type\n", (int)type_count);
}

static void cmd_press(const char* key) {
    uint8_t code = 0;
    if (strcmp(key, "enter") == 0 || strcmp(key, "return") == 0) {
        code = 0x0D;
    } else if (strcmp(key, "backspace") == 0 || strcmp(key, "bksp") == 0) {
        code = 0x08;
    } else if (strcmp(key, "escape") == 0 || strcmp(key, "esc") == 0) {
        code = 0x1B;
    } else if (strcmp(key, "tab") == 0) {
        code = 0x09;
    } else {
        Serial.printf("[test] press: unknown key \"%s\" (use enter/backspace/esc/tab)\n", key);
        return;
    }
    slopos_keyboard_inject(code);
    Serial.printf("[test] press %s (0x%02X)\n", key, code);
    // Small delay to let LVGL process the keypress before reading focus
    delay(50);
    dump_focused_widget();
}

static void cmd_inject(const char* args) {
    // Parse: <from> [channel=<ch>] <message...>
    // Extract first token as sender
    char buf[256];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* from = strtok(buf, " ");
    if (!from || !from[0]) {
        Serial.println("[test] inject: usage: inject <from> [channel=<ch>] <message>");
        return;
    }

    const char* channel = nullptr;
    char text_buf[200];
    text_buf[0] = '\0';

    // Parse remaining tokens
    char* rest = strtok(nullptr, "");
    if (!rest || !rest[0]) {
        Serial.println("[test] inject: no message text");
        return;
    }

    // Check if second token is channel=...
    char temp[200];
    strncpy(temp, rest, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char* second = strtok(temp, " ");
    if (second && strncmp(second, "channel=", 8) == 0) {
        channel = second + 8;
        char* msg_start = strtok(nullptr, "");
        if (msg_start) {
            strncpy(text_buf, msg_start, sizeof(text_buf) - 1);
            text_buf[sizeof(text_buf) - 1] = '\0';
        }
    } else {
        // Everything after the sender is the message
        strncpy(text_buf, rest, sizeof(text_buf) - 1);
        text_buf[sizeof(text_buf) - 1] = '\0';
    }

    if (text_buf[0] == '\0') {
        Serial.println("[test] inject: no message text");
        return;
    }

    slopos::mesh::injectMessage(from, channel, text_buf);
    if (channel && channel[0]) {
        Serial.printf("[test] injected channel msg from %s in #%s: %s\n",
                      from, channel, text_buf);
    } else {
        Serial.printf("[test] injected DM from %s: %s\n", from, text_buf);
    }
}

static void cmd_screen() {
    Serial.printf("[test] current screen: %s\n",
                  screen_name(slopos::ui::current_screen()));
}

static void cmd_status() {
    Serial.printf("[test] heap=%u psram=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram());
}

static void cmd_debug(const char* arg) {
    if (!arg || !arg[0]) {
        // No args: show current state
        Serial.printf("[test] debug level: %u\n", (unsigned)slopos::debug::get_level());
        Serial.printf("[test] features: display=%d mesh=%d ui=%d map=%d diag=%d\n",
                      slopos::debug::feat_get_display() ? 1 : 0,
                      slopos::debug::feat_get_mesh() ? 1 : 0,
                      slopos::debug::feat_get_ui() ? 1 : 0,
                      slopos::debug::feat_get_map() ? 1 : 0,
                      slopos::debug::feat_get_diag() ? 1 : 0);
        return;
    }
    // Skip leading whitespace
    while (*arg == ' ') arg++;

    // Check for sub-commands: debug display 1|0, debug mesh 1|0, etc.
    struct FeatEntry {
        const char* name;
        void (*set)(bool);
        bool (*get)();
    };
    static const FeatEntry feat_table[] = {
        {"display", slopos::debug::feat_set_display, slopos::debug::feat_get_display},
        {"mesh",    slopos::debug::feat_set_mesh,    slopos::debug::feat_get_mesh},
        {"ui",      slopos::debug::feat_set_ui,      slopos::debug::feat_get_ui},
        {"map",     slopos::debug::feat_set_map,     slopos::debug::feat_get_map},
        {"diag",    slopos::debug::feat_set_diag,    slopos::debug::feat_get_diag},
    };

    char buf[64];
    strncpy(buf, arg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* subcmd = strtok(buf, " ");
    char* val_str = strtok(nullptr, " ");

    // Check for "level" sub-command
    if (strcmp(subcmd, "level") == 0) {
        if (!val_str) {
            Serial.printf("[test] debug level: %u\n", (unsigned)slopos::debug::get_level());
            return;
        }
        char* end;
        long level = strtol(val_str, &end, 10);
        while (*end == ' ') end++;
        if (*end != '\0' || level < 1 || level > 3) {
            Serial.println("[test] debug: usage: debug level <1|2|3>  (1=quiet, 2=normal, 3=verbose)");
            return;
        }
        slopos::debug::set_level((uint8_t)level);
        return;
    }

    // Check feature sub-commands
    for (auto& feat : feat_table) {
        if (strcmp(subcmd, feat.name) == 0) {
            if (!val_str) {
                // Show current state for this feature
                Serial.printf("[test] debug %s: %d\n", feat.name, feat.get() ? 1 : 0);
                return;
            }
            // Validate: must be exactly "0" or "1"
            if ((val_str[0] == '0' || val_str[0] == '1') && val_str[1] == '\0') {
                feat.set(val_str[0] != '0');
                Serial.printf("[test] debug %s: %s\n", feat.name, val_str[0] != '0' ? "ON" : "OFF");
            } else {
                Serial.printf("[test] debug: invalid value \"%s\" for %s (use 0 or 1)\n", val_str, feat.name);
            }
            return;
        }
    }

    // "all on" / "all off"
    if (strcmp(subcmd, "all") == 0 && val_str) {
        if ((val_str[0] == '0' || val_str[0] == '1') && val_str[1] == '\0') {
            slopos::debug::feat_set_all_mask(val_str[0] != '0');
            Serial.printf("[test] debug all features: %s\n", val_str[0] != '0' ? "ON" : "OFF");
        } else {
            Serial.printf("[test] debug: invalid value \"%s\" for all (use 0 or 1)\n", val_str);
        }
        return;
    }

    // Fallback: try parsing as a plain level number (backward compat)
    bool all_digits = true;
    for (const char* p = subcmd; *p; p++) { if (!isdigit((unsigned char)*p)) { all_digits = false; break; } }
    if (all_digits) {
        char* end;
        long level = strtol(subcmd, &end, 10);
        while (*end == ' ') end++;
        if (*end != '\0' || level < 1 || level > 3) {
            Serial.println("[test] debug: usage: debug <1|2|3> or debug <feature> <1|0>");
            return;
        }
        slopos::debug::set_level((uint8_t)level);
        return;
    }

    Serial.println("[test] debug: usage: debug <1|2|3>  |  debug <display|mesh|ui|map|diag|all> <1|0>  |  debug level <1|2|3>");
}

// Show full emoji grid for visual verification
static void cmd_emoji() {
    lv_obj_t* parent = lv_scr_act();
    if (!parent) {
        Serial.println("[test] emoji: no active screen");
        return;
    }

    lv_obj_t* dlg = lv_obj_create(parent);
    lv_obj_set_size(dlg, LV_PCT(100), LV_PCT(100));
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(0x0F0F0F), 0);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 4, 0);

    lv_obj_t* title = lv_label_create(dlg);
    lv_label_set_text(title, "Emoji Test Grid (362)");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00BFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* close_btn = lv_btn_create(dlg);
    lv_obj_set_size(close_btn, 24, 20);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xCC3333), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(close_btn, 0, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "X");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        lv_obj_t* d = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
        if (d) lv_obj_del_async(d);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* grid = lv_obj_create(dlg);
    lv_obj_set_size(grid, LV_PCT(96), LV_PCT(85));
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 4, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(grid, (lv_obj_flag_t)(
        LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

    int count = emoji_font_get_count();
    for (int i = 0; i < count; i++) {
        if (const char* emoji_str = emoji_font_get_by_index(i)) {
            lv_obj_t* btn = lv_btn_create(grid);
            lv_obj_set_size(btn, 28, 26);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(btn, 0, 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, 0, 0);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, emoji_str);
            lv_obj_set_style_text_font(lbl, &emoji_font, 0);
            lv_obj_center(lbl);
        }
    }
    Serial.printf("[test] emoji grid: %d emoji displayed\n", count);
}

// Test emoji autocomplete for a given prefix
static void cmd_emoji_ac(const char* arg) {
    if (!arg || !arg[0]) {
        Serial.println("[test] emoji-ac <prefix>  — test autocomplete search");
        return;
    }

    EmojiEntry matches[12];
    int count = emoji_search(arg, matches, 12);

    Serial.printf("[test] emoji-ac '%s': %d matches\n", arg, count);
    for (int i = 0; i < count && i < 8; i++) {
        Serial.printf("  [%d] :%s: → %s\n", i + 1, matches[i].short_name, matches[i].utf8);
    }
    if (count > 8) {
        Serial.printf("  ... and %d more\n", count - 8);
    }
}

// ── Remote test commands ─────────────────────────────────

static void cmd_capture() {
    lv_display_t* disp = lv_display_get_default();
    if (!disp) {
        Serial.println("[test] capture: no display");
        return;
    }

    uint32_t w = (uint32_t)lv_display_get_horizontal_resolution(disp);
    uint32_t h = (uint32_t)lv_display_get_vertical_resolution(disp);
    uint32_t stride = w * 2;
    uint32_t total_bytes = w * h * 2;

    uint8_t* snap_buf = (uint8_t*)heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snap_buf) {
        Serial.println("[test] capture: pre-alloc failed");
        return;
    }

    lv_draw_buf_t snap_db;
    lv_draw_buf_init(&snap_db, w, h, LV_COLOR_FORMAT_RGB565, stride, snap_buf, total_bytes);

    lv_result_t res = lv_snapshot_take_to_draw_buf(lv_scr_act(), LV_COLOR_FORMAT_RGB565, &snap_db);
    if (res != LV_RES_OK) {
        heap_caps_free(snap_buf);
        Serial.println("[test] capture: snapshot failed");
        return;
    }

    Serial.printf("[capture] W=%lu H=%lu S=%lu\n",
                  (unsigned long)w, (unsigned long)h, (unsigned long)stride);

    char hex_line[128];
    const int HEX_PER_LINE = 64;

    for (uint32_t y = 0; y < h; y++) {
        uint8_t* row = snap_buf + y * stride;
        uint32_t offset = 0;
        while (offset < stride) {
            uint32_t remaining = stride - offset;
            uint32_t chunk = (remaining > (HEX_PER_LINE / 2))
                             ? (HEX_PER_LINE / 2) : remaining;
            int n = 0;
            for (uint32_t i = 0; i < chunk; i++) {
                n += snprintf(hex_line + n, sizeof(hex_line) - n, "%02X", row[offset + i]);
            }
            hex_line[n] = '\0';
            Serial.printf("[cdata] %s\n", hex_line);
            offset += chunk;
        }
        if (y % 16 == 0) yield();
    }

    heap_caps_free(snap_buf);
    Serial.println("[capture] END");
}

static void dump_widget_tree(lv_obj_t* obj, int depth) {
    if (!obj) return;
    for (int i = 0; i < depth; i++) Serial.print("  ");

    const char* type = "?";
    if (lv_obj_check_type(obj, &lv_button_class))      type = "btn";
    else if (lv_obj_check_type(obj, &lv_label_class))   type = "label";
    else if (lv_obj_check_type(obj, &lv_obj_class))     type = "obj";
    else if (lv_obj_check_type(obj, &lv_image_class))   type = "img";
    else if (lv_obj_check_type(obj, &lv_textarea_class)) type = "textarea";
    else if (lv_obj_check_type(obj, &lv_list_class))    type = "list";
    else if (lv_obj_check_type(obj, &lv_roller_class))  type = "roller";
    else if (lv_obj_check_type(obj, &lv_dropdown_class)) type = "dropdown";

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    Serial.printf("%s x=%d y=%d w=%d h=%d visible=%d",
                  type, lv_obj_get_x(obj), lv_obj_get_y(obj),
                  coords.x2 - coords.x1 + 1, coords.y2 - coords.y1 + 1,
                  lv_obj_is_valid(obj) && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN));

    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char* text = lv_label_get_text(obj);
        if (text) {
            char buf[48];
            strncpy(buf, text, 44);
            buf[44] = '\0';
            for (char* p = buf; *p; p++) if (*p == '\n') *p = ' ';
            Serial.printf(" \"%s\"", buf);
        }
    }
    Serial.println();

    uint32_t child_cnt = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(obj, i);
        if (child) dump_widget_tree(child, depth + 1);
    }
}

static void cmd_tree() {
    lv_obj_t* scr = lv_scr_act();
    if (!scr) { Serial.println("[test] tree: no active screen"); return; }
    dump_widget_tree(scr, 0);
    Serial.println("[test] tree: see above");
}

static void cmd_widgets() {
    lv_obj_t* scr = lv_scr_act();
    if (!scr) { Serial.println("[test] widgets: no active screen"); return; }

    Serial.println("[widgets] visible text widgets:");
    int count = 0;
    lv_obj_t* stack[64];
    int sp = 0;
    stack[sp++] = scr;

    while (sp > 0) {
        lv_obj_t* obj = stack[--sp];
        if (!obj) continue;

        if (lv_obj_check_type(obj, &lv_label_class)) {
            const char* text = lv_label_get_text(obj);
            if (text && text[0] != '\0' && lv_obj_is_valid(obj) &&
                !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
                lv_area_t coords;
                lv_obj_get_coords(obj, &coords);
                int w = coords.x2 - coords.x1 + 1;
                int h = coords.y2 - coords.y1 + 1;
                char buf[64];
                strncpy(buf, text, 60);
                buf[60] = '\0';
                for (char* p = buf; *p; p++) if (*p == '\n') *p = ' ';
                Serial.printf("  [%d] x=%d y=%d w=%d h=%d \"%s\"\n", count,
                              lv_obj_get_x(obj), lv_obj_get_y(obj), w, h, buf);
                count++;
            }
        }

        uint32_t child_cnt = lv_obj_get_child_count(obj);
        for (int32_t i = (int32_t)child_cnt - 1; i >= 0; i--) {
            lv_obj_t* child = lv_obj_get_child(obj, i);
            if (child && sp < 64) stack[sp++] = child;
        }
    }
    Serial.printf("[widgets] total visible: %d\n", count);
}

static void cmd_tap(const char* arg) {
    int x, y;
    if (sscanf(arg, "%d %d", &x, &y) != 2) {
        Serial.println("[test] tap: usage: tap <x> <y>");
        return;
    }
    slopos_test_set_touch(x, y);
    Serial.printf("[test] tap %d %d\n", x, y);
}

static void cmd_backlight(const char* arg) {
    if (!arg || !arg[0]) {
        Serial.printf("[test] backlight: %s\n", slopos_display_is_on() ? "on" : "off");
        return;
    }
    int val = atoi(arg);
    if (val < 0 || val > 255) {
        Serial.println("[test] backlight: brightness must be 0-255");
        return;
    }
    slopos_display_set_brightness((uint8_t)val);
    if (val > 0) {
        slopos_keyboard_set_brightness(slopos::prefs_get().kbd_backlight);
    } else {
        slopos_keyboard_set_brightness(0);
    }
    Serial.printf("[test] backlight %d\n", val);
}

// ── Send channel message ───────────────────────────
static void cmd_sendchannel(const char* arg) {
    if (!arg || arg[0] == '\0') {
        Serial.println("[test] sendchannel: usage: sendchannel <channel_name> <text>");
        return;
    }
    // Find first space to split channel name from text
    const char* space = strchr(arg, ' ');
    if (!space) {
        Serial.println("[test] sendchannel: missing text after channel name");
        return;
    }
    // Extract channel name
    char channel[64];
    size_t name_len = space - arg;
    if (name_len > 63) name_len = 63;
    memcpy(channel, arg, name_len);
    channel[name_len] = '\0';

    // Skip past the space to get text
    const char* text = space + 1;
    while (*text == ' ') text++;

    if (text[0] == '\0') {
        Serial.println("[test] sendchannel: missing text after channel name");
        return;
    }

    // Validate channel exists by trying to send
    bool ok = slopos::mesh::sendChannelMessage(channel, text);
    if (ok) {
        Serial.printf("[test] sendchannel OK: #%s sent %d chars\n", channel, (int)strlen(text));
    } else {
        Serial.printf("[test] sendchannel FAILED: channel \"%s\" not found or send error\n", channel);
    }
}

// ── Add channel ───────────────────────────────────
static void cmd_addchannel(const char* arg) {
    if (!arg || arg[0] == '\0') {
        Serial.println("[test] addchannel: usage: addchannel <name> [psk_b64]");
        return;
    }
    const char* space = strchr(arg, ' ');
    if (space) {
        char name[64];
        size_t name_len = space - arg;
        if (name_len > 63) name_len = 63;
        memcpy(name, arg, name_len);
        name[name_len] = '\0';
        const char* psk = space + 1;
        while (*psk == ' ') psk++;
        bool ok = slopos::mesh::addChannel(name, psk);
        if (ok) Serial.printf("[test] addchannel OK: #%s with PSK\n", name);
        else Serial.printf("[test] addchannel FAILED: #%s\n", name);
    } else {
        // No PSK — try as hashtag channel
        bool ok = slopos::mesh::addHashtagChannel(arg);
        if (ok) Serial.printf("[test] addchannel OK: hashtag #%s\n", arg);
        else Serial.printf("[test] addchannel FAILED: hashtag #%s\n", arg);
    }
}

static void dump_focused_widget() {
    lv_group_t* g = lv_group_get_default();
    if (!g) { Serial.println("[test] focus: no default group"); return; }
    lv_obj_t* focused = lv_group_get_focused(g);
    if (!focused) { Serial.println("[test] focus: no focused widget"); return; }

    const char* type = "?";
    if (lv_obj_check_type(focused, &lv_textarea_class)) type = "textarea";
    else if (lv_obj_check_type(focused, &lv_label_class))   type = "label";
    else if (lv_obj_check_type(focused, &lv_button_class))  type = "btn";
    else if (lv_obj_check_type(focused, &lv_list_class))    type = "list";

    lv_area_t coords;
    lv_obj_get_coords(focused, &coords);

    Serial.printf("[test] focus: %s x=%d y=%d w=%d h=%d",
                  type, lv_obj_get_x(focused), lv_obj_get_y(focused),
                  coords.x2 - coords.x1 + 1, coords.y2 - coords.y1 + 1);

    if (lv_obj_check_type(focused, &lv_textarea_class)) {
        const char* text = lv_textarea_get_text(focused);
        if (text) {
            char buf[48];
            strncpy(buf, text, 44);
            buf[44] = '\0';
            for (char* p = buf; *p; p++) if (*p == '\n') *p = ' ';
            Serial.printf(" text=\"%s\"", buf);
        }
    }
    Serial.println();
}

// ── Cmd: setrf ────────────────────────────────────────────
static void cmd_setrf(const char* arg) {
    if (!arg) {
        Serial.println(F("[test] setrf: usage: setrf <freq> <sf> <bw> <cr> <tx_pwr>"));
        Serial.println(F("[test] setrf: e.g. setrf 869.525 10 250 5 22"));
        return;
    }
    float freq; int sf, cr, tx_pwr; float bw;
    int n = sscanf(arg, "%f %d %f %d %d", &freq, &sf, &bw, &cr, &tx_pwr);
    if (n != 5) {
        Serial.printf("[test] setrf: expected 5 args, got %d\n", n);
        return;
    }
    // Validate ranges
    if (freq < 400.0f || freq > 1000.0f) {
        Serial.println("[test] setrf: freq out of range (400-1000 MHz)");
        return;
    }
    if (sf < 6 || sf > 12) {
        Serial.println("[test] setrf: SF out of range (6-12)");
        return;
    }
    if (bw < 7.8f || bw > 500.0f) {
        Serial.println("[test] setrf: BW out of range (7.8-500 kHz)");
        return;
    }
    if (cr < 5 || cr > 8) {
        Serial.println("[test] setrf: CR out of range (5-8)");
        return;
    }
    if (tx_pwr < 2 || tx_pwr > 22) {
        Serial.println("[test] setrf: TX power out of range (2-22 dBm)");
        return;
    }

    slopos::NodePrefs p = slopos::prefs_get();
    p.freq = freq;
    p.sf = (uint8_t)sf;
    p.bw = bw;
    p.cr = (uint8_t)cr;
    p.tx_power_dbm = (int8_t)tx_pwr;
    p.configured = true;

    if (prefs_save(p)) {
        slopos::prefs_set(p);
        Serial.println(F("[test] setrf: radio params saved to NVS"));
        Serial.printf("[test] setrf: freq=%.3f SF=%d BW=%.1f CR=%d TX=%d dBm\n",
                      freq, sf, bw, cr, tx_pwr);
        Serial.println(F("[test] setrf: reboot to apply changes"));
    } else {
        Serial.println(F("[test] setrf: ERROR failed to save to NVS"));
    }
}

// ── Cmd: reboot ────────────────────────────────────────────
static void cmd_reboot() {
    Serial.println(F("[test] reboot: device restarting..."));
    Serial.flush();
    delay(100);
    esp_restart();
}

// ── Cmd: advert ───────────────────────────────────────────
static void cmd_advert() {
    bool ok = slopos::mesh::sendAdvert();
    Serial.printf("[test] advert: %s\n", ok ? "sent" : "FAILED");
}

// ── Command parsing ──────────────────────────────────────
static bool dispatch(const char* line) {
    // Skip empty lines and comments
    if (!line || line[0] == '\0' || line[0] == '#' || line[0] == ';') return false;

    char buf[CMD_BUF_SIZE];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* cmd = strtok(buf, " ");
    if (!cmd) return false;

    char* arg = strtok(nullptr, "");  // rest of line after command
    if (arg) {
        // Trim leading whitespace
        while (*arg == ' ') arg++;
        if (*arg == '\0') arg = nullptr;
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        print_help();
    } else if (strcmp(cmd, "nav") == 0 || strcmp(cmd, "navigate") == 0) {
        if (!arg) { Serial.println("[test] nav: missing screen name"); return true; }
        cmd_navigate(arg);
    } else if (strcmp(cmd, "back") == 0) {
        cmd_back();
    } else if (strcmp(cmd, "tb") == 0 || strcmp(cmd, "trackball") == 0) {
        if (!arg) { Serial.println("[test] tb: missing direction"); return true; }
        cmd_trackball(arg);
    } else if (strcmp(cmd, "type") == 0) {
        cmd_type(arg);
    } else if (strcmp(cmd, "press") == 0) {
        if (!arg) { Serial.println("[test] press: missing key name"); return true; }
        cmd_press(arg);
    } else if (strcmp(cmd, "inject") == 0 || strcmp(cmd, "msg") == 0) {
        if (!arg) { Serial.println("[test] inject: missing args"); return true; }
        cmd_inject(arg);
    } else if (strcmp(cmd, "sendchannel") == 0) {
        if (!arg) { Serial.println("[test] sendchannel: missing args — use: sendchannel <channel_name> <text>"); return true; }
        cmd_sendchannel(arg);
    } else if (strcmp(cmd, "addchannel") == 0 || strcmp(cmd, "addchan") == 0) {
        if (!arg) { Serial.println("[test] addchannel: missing args"); return true; }
        cmd_addchannel(arg);
    } else if (strcmp(cmd, "screen") == 0) {
        cmd_screen();
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status();
    } else if (strcmp(cmd, "debug") == 0) {
        cmd_debug(arg);
    } else if (strcmp(cmd, "term-log") == 0) {
        slopos::ui::term_dump_log();
    } else if (strcmp(cmd, "term-clear") == 0) {
        slopos::ui::term_clear_log();
    } else if (strcmp(cmd, "term-submit") == 0) {
        if (!arg) { Serial.println("[test] term-submit: missing command text"); return true; }
        slopos::ui::term_submit(arg);
    } else if (strcmp(cmd, "emoji") == 0) {
        cmd_emoji();
    } else if (strcmp(cmd, "emoji-ac") == 0) {
        cmd_emoji_ac(arg);
    } else if (strcmp(cmd, "capture") == 0) {
        cmd_capture();
    } else if (strcmp(cmd, "tree") == 0) {
        cmd_tree();
    } else if (strcmp(cmd, "widgets") == 0) {
        cmd_widgets();
    } else if (strcmp(cmd, "tap") == 0) {
        if (!arg) { Serial.println("[test] tap: missing args"); return true; }
        cmd_tap(arg);
    } else if (strcmp(cmd, "backlight") == 0) {
        cmd_backlight(arg);
    } else if (strcmp(cmd, "setrf") == 0) {
        cmd_setrf(arg);
    } else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "restart") == 0) {
        cmd_reboot();
    } else if (strcmp(cmd, "advert") == 0) {
        cmd_advert();
    } else {
        Serial.printf("[test] unknown command: %s (try 'help')\n", cmd);
    }
    return true;
}

// ── Public API ───────────────────────────────────────────

bool slopos_test_controller_exec(const char* cmd) {
    return dispatch(cmd);
}

void slopos_test_controller_init() {
    initialized = true;
    cmd_pos = 0;
    cmd_buf[0] = '\0';
    last_poll_ms = millis();

    Serial.println();
    Serial.println(F("╔══════════════════════════════════════╗"));
    Serial.println(F("║  SlopOS Remote Test Controller      ║"));
    Serial.println(F("║  Type 'help' for available commands ║"));
    Serial.println(F("╚══════════════════════════════════════╝"));
    Serial.println();
}

void slopos_test_controller_loop() {
    if (!initialized) return;

    uint32_t now = millis();

    // Drain type queue: inject one character per loop iteration
    if (type_count > 0 && (now - type_last_inject_ms >= TYPE_CHUNK_DELAY)) {
        slopos_keyboard_inject((uint8_t)type_buf[type_pos]);
        type_pos++;
        type_count--;
        type_last_inject_ms = now;
        if (type_count == 0) {
            Serial.printf("[test] type done: %d chars injected, verifying...\n", (int)type_pos);
            delay(50);
            dump_focused_widget();
        }
    }

    if (now - last_poll_ms < CMD_POLL_MS) return;
    last_poll_ms = now;

    // Read characters from Serial
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmd_pos > 0) {
                cmd_buf[cmd_pos] = '\0';
                Serial.printf("[test] > %s\n", cmd_buf);  // echo
                dispatch(cmd_buf);
                cmd_pos = 0;
            }
        } else if (cmd_pos < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_pos++] = c;
        }
    }
}

// ── Stub implementations for functions declared in screens.h ──
// These are needed by the test controller dispatch table but only
// meaningful when a real terminal screen is active.
namespace slopos::ui {
void term_dump_log()  { Serial.println("[term] dump: no terminal screen (test mode)"); }
void term_clear_log() { Serial.println("[term] cleared (test mode)"); }
void term_submit(const char* text) { Serial.printf("[term] submit: %s (test mode, ignored)\n", text); }
lv_obj_t* term_get_input() { return nullptr; }
} // namespace slopos::ui
