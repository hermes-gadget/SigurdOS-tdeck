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
//   picker <char>                 Open keyboard character picker
//   press enter|backspace|esc     Press special key
//   inject <from> <text>          Simulate incoming DM
//   inject <from> channel=<ch> <text>  Simulate incoming channel msg
//   simulateack <to> <timestamp> Simulate an ACK (no-radio profile only)
//   screen                        Show current screen name
//   status                        Show device info (heap, psram, batt)
//   stresschat [cycles]           Stress Chat/Home lifecycle (default 50)

#include "test_controller.h"
#include "test_controller_command.h"
#include "hal/display.h"
#include "hal/trackball.h"
#include "hal/touch.h"
#include "hal/keyboard.h"
#include "hal/gps.h"
#include "hal/prefs.h"
#include "mesh/mesh_wrapper.h"
#include "ui/navigation.h"
#include "diagnostics/debug.h"
#include "diagnostics/build_info.h"
#include "diagnostics/telemetry.h"
#include "ui/screens.h"
#include "ui/chat_screen.h"
#include "fonts/emoji_font.h"
#include "fonts/emoji_data.h"
#include "utils/utf8_util.h"
#include <lvgl.h>
#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#include <new>
#include <cctype>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <others/snapshot/lv_snapshot.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

// ── Forward declarations ─────────────────────────────────
static void dump_focused_widget();

// ── Constants ────────────────────────────────────────────
static constexpr uint32_t CMD_POLL_MS = 50;   // check Serial every 50ms
static constexpr size_t   CMD_BUF_SIZE = 256;
static constexpr size_t   TYPE_BUF_SIZE = 256;   // max codepoints in type queue
static constexpr size_t   TYPE_CHUNK_DELAY = 120; // ms between injected codepoints, must give LVGL time to consume
static constexpr uint32_t DIAGNOSTIC_STALL_TIMEOUT_MS = 5000;
// Radio-enabled debug builds can take roughly two minutes to stream a full
// RGB565 frame while keeping the cooperative mesh/UI loop responsive.
static constexpr uint32_t CAPTURE_TOTAL_TIMEOUT_MS = 150000;
static constexpr uint32_t TREE_TOTAL_TIMEOUT_MS = 30000;
static constexpr size_t CAPTURE_DATA_BYTES_PER_LINE = 32;
static constexpr uint8_t DIAGNOSTIC_LINES_PER_LOOP = 4;
static constexpr uint16_t STRESS_CHAT_DEFAULT_CYCLES = 50;
static constexpr uint16_t STRESS_CHAT_MAX_CYCLES = 1000;
static constexpr uint16_t STRESS_CHAT_CHANNEL_INTERVAL = 10;
static constexpr uint8_t STRESS_CHAT_SETTLE_PASSES = 3;

// ── State ────────────────────────────────────────────────
static bool     initialized = false;
static uint32_t last_poll_ms = 0;
static char     cmd_buf[CMD_BUF_SIZE];
static size_t   cmd_pos = 0;

// Type queue — drained one codepoint per loop iteration so LVGL can consume each keypress
static uint32_t type_buf[TYPE_BUF_SIZE];
static size_t   type_pos = 0;    // next codepoint to inject
static size_t   type_count = 0;  // remaining codepoints in queue
static uint32_t type_last_inject_ms = 0;

struct AsyncSerialLine {
    char data[192];
    size_t length;
    size_t offset;
};

static AsyncSerialLine diagnostic_line = {};

enum class CapturePhase : uint8_t {
    Idle,
    Snapshot,
    Header,
    Data,
    Stats,
    End,
    Done,
};

struct CaptureJob {
    CapturePhase phase;
    uint8_t* buffer;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t total_bytes;
    uint32_t offset;
    uint32_t started_ms;
    uint32_t last_progress_ms;
};

static CaptureJob capture_job = {};

struct TreeFrame {
    lv_obj_t* obj;
    uint32_t next_child;
    bool printed;
};

struct TreeJob {
    bool active;
    bool finishing;
    bool depth_limited;
    bool node_limited;
    bool summary_queued;
    uint8_t stack_size;
    uint8_t max_depth_seen;
    uint16_t nodes;
    uint32_t started_ms;
    uint32_t last_progress_ms;
    TreeFrame stack[SIGURDOS_TEST_TREE_MAX_DEPTH + 1];
};

static TreeJob tree_job = {};

struct EmojiGridState {
    lv_obj_t* dialog;
    lv_obj_t* title;
    lv_obj_t* grid;
    lv_obj_t* previous;
    lv_obj_t* page_label;
    lv_obj_t* next;
    uint16_t page;
    uint16_t total;
};

static EmojiGridState emoji_grid = {};

struct LvPoolState {
    uint32_t total;
    uint32_t free;
    uint32_t largest;
    uint8_t frag;
};

enum class StressChatPhase : uint8_t {
    Idle,
    SettleBaseline,
    NavigateChat,
    SettleChat,
    NavigateHome,
    SettleHome,
    NavigateChannelChat,
    SettleChannelList,
    OpenChannel,
    SettleChannel,
    CloseChannel,
    SettleChannelHome,
};

struct StressChatJob {
    StressChatPhase phase;
    uint16_t requested_cycles;
    uint16_t completed_cycles;
    uint8_t settle_passes;
    LvPoolState baseline;
    LvPoolState previous_home;
};

static StressChatJob stress_chat_job = {};

static uint32_t stress_min_stack_words = 0xFFFFFFFFUL;
static uint32_t stress_min_lvgl_free = 0xFFFFFFFFUL;
static uint8_t stress_max_lvgl_used_pct = 0;

static void sample_stress_metrics() {
    const uint32_t stack_words = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
    if (stack_words < stress_min_stack_words) stress_min_stack_words = stack_words;

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    if (mon.free_size < stress_min_lvgl_free) stress_min_lvgl_free = mon.free_size;
    if (mon.used_pct > stress_max_lvgl_used_pct) stress_max_lvgl_used_pct = mon.used_pct;
}

static void reset_stress_metrics() {
    stress_min_stack_words = 0xFFFFFFFFUL;
    stress_min_lvgl_free = 0xFFFFFFFFUL;
    stress_max_lvgl_used_pct = 0;
    sample_stress_metrics();
}

static LvPoolState read_lv_pool_state() {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    return {
        static_cast<uint32_t>(mon.total_size),
        static_cast<uint32_t>(mon.free_size),
        static_cast<uint32_t>(mon.free_biggest_size),
        static_cast<uint8_t>(mon.frag_pct),
    };
}

static bool lv_pool_degraded(const LvPoolState& reference, const LvPoolState& current) {
    return current.total != reference.total || current.free < reference.free ||
           current.largest < reference.largest || current.frag > reference.frag;
}

static void log_lv_pool_state(uint16_t cycle, const char* point,
                              const LvPoolState& state) {
    Serial.printf("[stresschat] cycle=%u point=%s total=%lu free=%lu largest=%lu frag=%u%%\n",
                  static_cast<unsigned>(cycle), point,
                  static_cast<unsigned long>(state.total),
                  static_cast<unsigned long>(state.free),
                  static_cast<unsigned long>(state.largest),
                  static_cast<unsigned>(state.frag));
}

static void log_lv_pool_degradation(uint16_t cycle, const char* reference_name,
                                    const LvPoolState& reference,
                                    const LvPoolState& current) {
    if (!lv_pool_degraded(reference, current)) return;
    Serial.printf("[stresschat] DEGRADE cycle=%u vs=%s total=%ld free=%ld largest=%ld frag=%+d\n",
                  static_cast<unsigned>(cycle), reference_name,
                  static_cast<long>(current.total) - static_cast<long>(reference.total),
                  static_cast<long>(current.free) - static_cast<long>(reference.free),
                  static_cast<long>(current.largest) - static_cast<long>(reference.largest),
                  static_cast<int>(current.frag) - static_cast<int>(reference.frag));
}

static void clear_diagnostic_line() {
    diagnostic_line.length = 0;
    diagnostic_line.offset = 0;
    diagnostic_line.data[0] = '\0';
}

static bool diagnostic_line_pending() {
    return diagnostic_line.offset < diagnostic_line.length;
}

static bool queue_diagnostic_line(size_t length) {
    if (diagnostic_line_pending()) return false;
    if (length >= sizeof(diagnostic_line.data)) {
        length = sizeof(diagnostic_line.data) - 1;
    }
    diagnostic_line.data[length] = '\0';
    diagnostic_line.length = length;
    diagnostic_line.offset = 0;
    return true;
}

static uint32_t* diagnostic_progress_clock() {
    if (capture_job.phase != CapturePhase::Idle) return &capture_job.last_progress_ms;
    if (tree_job.active) return &tree_job.last_progress_ms;
    return nullptr;
}

static bool drain_diagnostic_line(uint32_t now) {
    if (!diagnostic_line_pending()) return true;

    const int available = Serial.availableForWrite();
    if (available <= 0) return false;
    size_t remaining = diagnostic_line.length - diagnostic_line.offset;
    size_t chunk = static_cast<size_t>(available);
    if (chunk > remaining) chunk = remaining;
    const size_t written = Serial.write(
        reinterpret_cast<const uint8_t*>(diagnostic_line.data + diagnostic_line.offset),
        chunk);
    if (written == 0) return false;

    diagnostic_line.offset += written;
    if (uint32_t* progress = diagnostic_progress_clock()) *progress = now;
    if (!diagnostic_line_pending()) clear_diagnostic_line();
    return !diagnostic_line_pending();
}

static size_t format_stress_line(const char* operation) {
    sample_stress_metrics();
    const int n = snprintf(diagnostic_line.data, sizeof(diagnostic_line.data),
                           "[stress] op=%s stack_hwm_words=%lu "
                           "lvgl_min_free=%lu lvgl_max_used_pct=%u\n",
                           operation ? operation : "?",
                           (unsigned long)stress_min_stack_words,
                           (unsigned long)stress_min_lvgl_free,
                           (unsigned)stress_max_lvgl_used_pct);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

// ── Screen name lookup ───────────────────────────────────
struct ScreenEntry {
    const char* name;
    sigurdos::ui::Screen screen;
};

static const ScreenEntry screen_table[] = {
    {"home",        sigurdos::ui::Screen::Home},
    {"chat",        sigurdos::ui::Screen::Chat},
    {"contacts",    sigurdos::ui::Screen::Contacts},
    {"channels",    sigurdos::ui::Screen::Channels},
    {"network",     sigurdos::ui::Screen::Network},
    {"heard",       sigurdos::ui::Screen::Heard},
    {"map",         sigurdos::ui::Screen::Map},
    {"advertise",   sigurdos::ui::Screen::Advertise},
    {"settings",    sigurdos::ui::Screen::Settings},
    {"trace",       sigurdos::ui::Screen::Trace},
    {"terminal",    sigurdos::ui::Screen::Terminal},
    {"signal",      sigurdos::ui::Screen::Signal},
    {"radio",       sigurdos::ui::Screen::RadioSetup},
    {"onboarding",      sigurdos::ui::Screen::Onboarding},
    {"contactdetail",   sigurdos::ui::Screen::ContactDetail},
    {"nodestatus",      sigurdos::ui::Screen::NodeStatus},
    {"telemetry",       sigurdos::ui::Screen::Telemetry},
    {"repeaters",       sigurdos::ui::Screen::Repeaters},
    {"system",          sigurdos::ui::Screen::SettingsSystem},
    {"wifinetworks",    sigurdos::ui::Screen::WiFiNetworks},
    {"nodestats",       sigurdos::ui::Screen::NodeStats},
    {"regions",         sigurdos::ui::Screen::Regions},
    {"s-display",       sigurdos::ui::Screen::SettingsDisplay},
    {"s-radio",         sigurdos::ui::Screen::SettingsRadio},
    {"s-gps",           sigurdos::ui::Screen::SettingsGPS},
    {"bluetooth",       sigurdos::ui::Screen::Bluetooth},
};

static const char* screen_name(sigurdos::ui::Screen s) {
    for (auto& e : screen_table) {
        if (e.screen == s) return e.name;
    }
    return "unknown";
}

static sigurdos::ui::Screen screen_from_name(const char* name) {
    for (auto& e : screen_table) {
        if (strcmp(e.name, name) == 0) return e.screen;
    }
    return sigurdos::ui::Screen::COUNT;  // invalid sentinel
}

// ── Help text ────────────────────────────────────────────
static void print_help() {
    Serial.println(F("╔══════════════════════════════════════╗"));
    Serial.println(F("║  SigurdOS Remote Test Controller      ║"));
    Serial.println(F("╠══════════════════════════════════════╣"));
    Serial.println(F("║  Commands:                          ║"));
    Serial.println(F("║  help        Show this help          ║"));
    Serial.println(F("║  nav <scr>   Navigate to screen      ║"));
    Serial.println(F("║  back        Go back                 ║"));
    Serial.println(F("║  tb <dir>    Trackball (u/d/l/r/c)   ║"));
    Serial.println(F("║  type <txt>  Type text               ║"));
    Serial.println(F("║  picker <c>  Open char picker        ║"));
    Serial.println(F("║  press <key> Press Enter/Bksp/Esc    ║"));
    Serial.println(F("║  inject <from> [channel=<ch>] <msg>  ║"));
    Serial.println(F("║  sendchannel <ch> <text>          Send on a channel        ║"));
    Serial.println(F("║  addchannel <name> [psk]          Add channel              ║"));
    Serial.println(F("║  removechannel <idx|name>         Remove a saved channel   ║"));
    Serial.println(F("║  addrepeater <name>           Add test repeater contact  ║"));

    Serial.println(F("║  addroomserver <name>        Add test room server    ║"));
    Serial.println(F("║  login <name> <pw>            Login to room server      ║"));
    Serial.println(F("║  fetchmsgs <name> [chan]      Fetch room messages       ║"));

    Serial.println(F("║  screen      Show current screen     ║"));
    Serial.println(F("║  status      Show device state       ║"));
    Serial.println(F("║  buildinfo   Show build provenance   ║"));
    Serial.println(F("║  stresschat [n] Chat/Home LVGL stress ║"));
    Serial.println(F("║  keydiag     Dump keyboard diagnostics║"));
    Serial.println(F("║  inputdiag   Dump touch/trackball diag║"));
    Serial.println(F("║  gpsdiag     Dump GPS UART/fix diag   ║"));
    Serial.println(F("║  contactstats Show contact counters ║"));
    Serial.println(F("║  ble status|on|off|pin  Companion BLE control / PIN ║"));
    Serial.println(F("║  debug <level>  Set debug level (1=quiet, 2=normal, 3=verbose)║"));
    Serial.println(F("║  debug <feat> <1|0>  Toggle feature: display/mesh/ui/map/diag║"));
    Serial.println(F("║  debug all <1|0>     Enable/disable all debug features      ║"));
    Serial.println(F("║  sendmessage <name> <text>       Send DM to contact         ║"));
#if !defined(SIGURDOS_REMOTE_TEST_RADIO) || !SIGURDOS_REMOTE_TEST_RADIO
    Serial.println(F("║  simulateack <name> <timestamp>  Simulate ACK (no radio)    ║"));
#endif
    Serial.println(F("║  opendm <name>                  Open DM conversation        ║"));
    Serial.println(F("║  emoji       Show emoji test grid     ║"));
    Serial.println(F("║  emoji-ac <p> Emoji autocomplete test ║"));
    Serial.println(F("║  capture [cancel]  Stream framebuffer║"));
    Serial.println(F("║  tree [cancel]     Dump bounded tree ║"));
    Serial.println(F("║  widgets     List visible widgets    ║"));
    Serial.println(F("║  tap <x> <y> Sim touch at coords    ║"));
    Serial.println(F("║  backlight   Get/set backlight bri  ║"));
    Serial.println(F("║  setrf <freq> <sf> <bw> <cr> <pwr>  ║"));
    Serial.println(F("║           Set radio params in NVS  ║"));
    Serial.println(F("║  getrf       Show current radio params  ║"));
    Serial.println(F("║  reboot      Reboot the device     ║"));
    Serial.println(F("║  advert      Send self advert      ║"));
    Serial.println(F("║  telemetry on|off|diff|hb N|full     ║"));
    Serial.println(F("║  query state|heap|lvgl|mesh|screen|crash|drift|wifi|gps|radio|sd|nvs|temp|task|pktlog|hb-ring|full  ║"));
    Serial.println(F("║  crash [report|clear|test]           ║"));
    Serial.println(F("║  drift                               ║"));
    Serial.println(F("╚══════════════════════════════════════╝"));
    Serial.println();
}

// ── Command dispatcher ───────────────────────────────────

static void cmd_navigate(const char* arg) {
    // Support "contactdetail <name>" to open a specific contact's detail screen
    if (strncmp(arg, "contactdetail ", 14) == 0) {
        const char* name = arg + 14;
        if (name[0]) {
            sigurdos::ui::contact_detail_screen_show(name);
            return;
        }
    }
    // Support "repeaterdetail <name>" to open the repeater detail screen (login + post-login sections)
    if (strncmp(arg, "repeaterdetail ", 15) == 0) {
        const char* raw = arg + 15;
        if (raw[0]) {
            char name[64];
            if (raw[0] == '"') {
                const char* endq = strchr(raw + 1, '"');
                if (endq) {
                    size_t nlen = endq - (raw + 1);
                    if (nlen > 63) nlen = 63;
                    memcpy(name, raw + 1, nlen);
                    name[nlen] = 0;
                } else {
                    strncpy(name, raw, sizeof(name) - 1);
                    name[sizeof(name) - 1] = 0;
                }
            } else {
                strncpy(name, raw, sizeof(name) - 1);
                name[sizeof(name) - 1] = 0;
            }
            if (name[0]) {
                bool skip = (sigurdos::mesh::getLoginStatus(name) == 2);
                sigurdos::ui::repeater_detail_screen_show(name, skip);
                return;
            }
        }
    }
    sigurdos::ui::Screen s = screen_from_name(arg);
    if (s == sigurdos::ui::Screen::COUNT) {
        Serial.printf("[test] unknown screen: %s\n", arg);
        return;
    }
    sigurdos::ui::navigate_to(s);
    Serial.printf("[test] nav -> %s\n", arg);
}

static void cmd_back() {
    if (sigurdos::ui::can_go_back()) {
        sigurdos::ui::go_back();
        Serial.printf("[test] back -> %s\n",
                      screen_name(sigurdos::ui::current_screen()));
    } else {
        Serial.println("[test] back: no navigation history");
    }
}

static void cmd_trackball(const char* arg) {
    SigurdOSTrackballEvent ev = SigurdOSTrackballEvent::None;
    if (strcmp(arg, "up") == 0)        ev = SigurdOSTrackballEvent::Up;
    else if (strcmp(arg, "down") == 0) ev = SigurdOSTrackballEvent::Down;
    else if (strcmp(arg, "left") == 0) ev = SigurdOSTrackballEvent::Left;
    else if (strcmp(arg, "right") == 0) ev = SigurdOSTrackballEvent::Right;
    else if (strcmp(arg, "click") == 0 || strcmp(arg, "c") == 0)
        ev = SigurdOSTrackballEvent::Click;
    else if (strcmp(arg, "u") == 0)    ev = SigurdOSTrackballEvent::Up;
    else if (strcmp(arg, "d") == 0)    ev = SigurdOSTrackballEvent::Down;
    else if (strcmp(arg, "l") == 0)    ev = SigurdOSTrackballEvent::Left;
    else if (strcmp(arg, "r") == 0)    ev = SigurdOSTrackballEvent::Right;

    if (ev == SigurdOSTrackballEvent::None) {
        Serial.printf("[test] tb: unknown direction \"%s\" (use up/down/left/right/click)\n", arg);
        return;
    }
    sigurdos_trackball_inject(ev);
    Serial.printf("[test] tb %s\n", arg);
}

// Scroll a list on the active screen by a number of pixels (positive = down, negative = up)
static void cmd_scrolllist(const char* arg) {
    if (!arg || !arg[0]) {
        Serial.println("[test] scrolllist: usage: scrolllist <px>");
        return;
    }
    int px = atoi(arg);
    if (px == 0) return;
    // Find the first LVGL list on the active screen and scroll it
    lv_obj_t* scr = lv_scr_act();
    bool scrolled = false;
    // Search current screen children for a list object
    uint32_t cnt = lv_obj_get_child_cnt(scr);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(scr, i);
        if (child && lv_obj_check_type(child, &lv_list_class)) {
            // Search inside container for a scrollable list
            uint32_t cc = lv_obj_get_child_cnt(child);
            for (uint32_t j = 0; j < cc; j++) {
                lv_obj_t* grand = lv_obj_get_child(child, j);
                if (grand && lv_obj_has_flag(grand, LV_OBJ_FLAG_SCROLLABLE)) {
                    lv_obj_scroll_by(grand, 0, px, LV_ANIM_OFF);
                    scrolled = true;
                    Serial.printf("[test] scrolllist %d on scrollable child %u of container %u\n", px, j, i);
                    break;
                }
            }
            if (!scrolled) {
                // scroll_to_y uses absolute positions; keep a static offset
                static int scrolled_px = 0;
                scrolled_px += px;
                if (scrolled_px < 0) scrolled_px = 0;
                lv_obj_scroll_to_y(child, scrolled_px, LV_ANIM_OFF);
                scrolled = true;
                Serial.printf("[test] scrolllist scroll_to_y=%d on container %u\n", scrolled_px, i);
            }
            break;
        }
    }
    if (!scrolled) {
        // Fallback: scroll the active screen's direct children
        lv_obj_scroll_by(scr, 0, px, LV_ANIM_OFF);
        Serial.printf("[test] scrolllist %d on screen (fallback)\n", px);
    }
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
    const unsigned char* p = (const unsigned char*)text;
    while (*p && type_count < TYPE_BUF_SIZE) {
        uint32_t cp = 0;
        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xE0) == 0xC0 &&
                   p[1] != 0 &&
                   (p[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(p[0] & 0x1F) << 6) |
                 (uint32_t)(p[1] & 0x3F);
            p += 2;
        } else if ((*p & 0xF0) == 0xE0 &&
                   p[1] != 0 &&
                   p[2] != 0 &&
                   (p[1] & 0xC0) == 0x80 &&
                   (p[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(p[0] & 0x0F) << 12) |
                 ((uint32_t)(p[1] & 0x3F) << 6) |
                 (uint32_t)(p[2] & 0x3F);
            p += 3;
        } else if ((*p & 0xF8) == 0xF0 &&
                   p[1] != 0 &&
                   p[2] != 0 &&
                   p[3] != 0 &&
                   (p[1] & 0xC0) == 0x80 &&
                   (p[2] & 0xC0) == 0x80 &&
                   (p[3] & 0xC0) == 0x80) {
            cp = ((uint32_t)(p[0] & 0x07) << 18) |
                 ((uint32_t)(p[1] & 0x3F) << 12) |
                 ((uint32_t)(p[2] & 0x3F) << 6) |
                 (uint32_t)(p[3] & 0x3F);
            p += 4;
        } else {
            cp = '?';
            p++;
        }
        type_buf[type_count++] = cp;
    }
    type_last_inject_ms = 0;  // inject first codepoint on next loop
    Serial.printf("[test] queued %d codepoints to type\n", (int)type_count);
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
    sigurdos_keyboard_inject(code);
    Serial.printf("[test] press %s (0x%02X)\n", key, code);
    // Small delay to let LVGL process the keypress before reading focus
    delay(50);
    dump_focused_widget();
}

static void cmd_picker(const char* arg) {
    if (!arg || !arg[0]) {
        Serial.println("[test] picker: usage: picker <ascii-char>");
        return;
    }
    unsigned char base = (unsigned char)arg[0];
    if (base < 0x20 || base >= 0x7F) {
        Serial.println("[test] picker: base must be one ASCII character");
        return;
    }
    sigurdos_keyboard_inject_codepoint(sigurdos_keyboard_char_picker_key(base));
    Serial.printf("[test] picker %c (0x%08lX)\n",
                  (char)base,
                  (unsigned long)sigurdos_keyboard_char_picker_key(base));
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

    sigurdos::mesh::injectMessage(from, channel, text_buf);
    if (channel && channel[0]) {
        Serial.printf("[test] injected channel msg from %s in #%s: %s\n",
                      from, channel, text_buf);
    } else {
        Serial.printf("[test] injected DM from %s: %s\n", from, text_buf);
    }
}

static void cmd_screen() {
    Serial.printf("[test] current screen: %s\n",
                  screen_name(sigurdos::ui::current_screen()));
}

static void cmd_status() {
    sample_stress_metrics();
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    Serial.printf("[test] heap=%u psram=%u lvmem_used_pct=%u lvmem_free=%u "
                  "lvmem_total=%u lvmem_frag=%u stack_hwm_words=%lu "
                  "stress_lvgl_min_free=%lu stress_lvgl_max_used_pct=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram(),
                  (unsigned)mon.used_pct,
                  (unsigned)mon.free_size,
                  (unsigned)mon.total_size,
                  (unsigned)mon.frag_pct,
                  (unsigned long)stress_min_stack_words,
                  (unsigned long)stress_min_lvgl_free,
                  (unsigned)stress_max_lvgl_used_pct);
}

static void cmd_contactstats() {
    auto* contacts = new(std::nothrow) sigurdos::mesh::ContactInfo[MAX_CONTACTS];
    if (!contacts) {
        Serial.println(F("[test] contactstats: OOM"));
        return;
    }

    int exported = sigurdos::mesh::exportContactsFull(contacts, MAX_CONTACTS);
    if (exported < 0) exported = 0;
    if (exported > MAX_CONTACTS) exported = MAX_CONTACTS;

    int repeaters = 0;
    int rooms = 0;
    int chat = 0;
    int other = 0;
    for (int i = 0; i < exported; i++) {
        if (contacts[i].type == ADV_TYPE_REPEATER) {
            repeaters++;
        } else if (contacts[i].type == ADV_TYPE_ROOM) {
            rooms++;
        } else if (contacts[i].type == ADV_TYPE_CHAT) {
            chat++;
        } else {
            other++;
        }
    }
    delete[] contacts;

    Serial.printf("[test] contactstats stored=%d exported=%d repeaters=%d rooms=%d chat=%d other=%d max=%d\n",
                  sigurdos::mesh::getContactCount(), exported,
                  repeaters, rooms, chat, other, MAX_CONTACTS);
}

static void cmd_debug(const char* arg) {
    if (!arg || !arg[0]) {
        // No args: show current state
        Serial.printf("[test] debug level: %u\n", (unsigned)sigurdos::debug::get_level());
        Serial.printf("[test] features: display=%d mesh=%d ui=%d map=%d diag=%d\n",
                      sigurdos::debug::feat_get_display() ? 1 : 0,
                      sigurdos::debug::feat_get_mesh() ? 1 : 0,
                      sigurdos::debug::feat_get_ui() ? 1 : 0,
                      sigurdos::debug::feat_get_map() ? 1 : 0,
                      sigurdos::debug::feat_get_diag() ? 1 : 0);
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
        {"display", sigurdos::debug::feat_set_display, sigurdos::debug::feat_get_display},
        {"mesh",    sigurdos::debug::feat_set_mesh,    sigurdos::debug::feat_get_mesh},
        {"ui",      sigurdos::debug::feat_set_ui,      sigurdos::debug::feat_get_ui},
        {"map",     sigurdos::debug::feat_set_map,     sigurdos::debug::feat_get_map},
        {"diag",    sigurdos::debug::feat_set_diag,    sigurdos::debug::feat_get_diag},
    };

    char buf[64];
    strncpy(buf, arg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* subcmd = strtok(buf, " ");
    char* val_str = strtok(nullptr, " ");

    // Check for "level" sub-command
    if (strcmp(subcmd, "level") == 0) {
        if (!val_str) {
            Serial.printf("[test] debug level: %u\n", (unsigned)sigurdos::debug::get_level());
            return;
        }
        char* end;
        long level = strtol(val_str, &end, 10);
        while (*end == ' ') end++;
        if (*end != '\0' || level < 1 || level > 3) {
            Serial.println("[test] debug: usage: debug level <1|2|3>  (1=quiet, 2=normal, 3=verbose)");
            return;
        }
        sigurdos::debug::set_level((uint8_t)level);
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
            sigurdos::debug::feat_set_all_mask(val_str[0] != '0');
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
        sigurdos::debug::set_level((uint8_t)level);
        return;
    }

    Serial.println("[test] debug: usage: debug <1|2|3>  |  debug <display|mesh|ui|map|diag|all> <1|0>  |  debug level <1|2|3>");
}

static void clear_emoji_grid_state() {
    emoji_grid = {};
}

static void close_emoji_grid() {
    lv_obj_t* dialog = emoji_grid.dialog;
    clear_emoji_grid_state();
    if (dialog && lv_obj_is_valid(dialog)) lv_obj_del_async(dialog);
}

static void fail_emoji_grid(const char* detail) {
    Serial.printf("[test] emoji: allocation failed (%s)\n", detail ? detail : "unknown");
    close_emoji_grid();
}

static bool render_emoji_page() {
    if (!emoji_grid.dialog || !emoji_grid.grid || !emoji_grid.title ||
        !emoji_grid.page_label || !lv_obj_is_valid(emoji_grid.dialog)) {
        return false;
    }

    const uint16_t pages = sigurdos_test_controller_emoji_page_count(emoji_grid.total);
    if (pages == 0) return false;
    if (emoji_grid.page >= pages) emoji_grid.page = pages - 1;
    const uint16_t page_items = sigurdos_test_controller_emoji_page_items(
        emoji_grid.total, emoji_grid.page);
    if (!sigurdos_test_controller_emoji_object_count_allowed(page_items)) return false;

    lv_obj_clean(emoji_grid.grid);
    const uint16_t first = emoji_grid.page * SIGURDOS_TEST_EMOJI_PAGE_SIZE;
    for (uint16_t item = 0; item < page_items; ++item) {
        const char* emoji_str = emoji_font_get_by_index(first + item);
        if (!emoji_str) continue;

        lv_obj_t* btn = lv_btn_create(emoji_grid.grid);
        if (!btn) {
            lv_obj_clean(emoji_grid.grid);
            return false;
        }
        lv_obj_set_size(btn, 28, 26);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        if (!lbl) {
            lv_obj_clean(emoji_grid.grid);
            return false;
        }
        lv_label_set_text(lbl, emoji_str);
        lv_obj_set_style_text_font(lbl, &emoji_font, 0);
        lv_obj_center(lbl);
    }

    char title[48];
    snprintf(title, sizeof(title), "Emoji Test %u-%u / %u",
             (unsigned)(first + 1), (unsigned)(first + page_items),
             (unsigned)emoji_grid.total);
    lv_label_set_text(emoji_grid.title, title);

    char page_text[24];
    snprintf(page_text, sizeof(page_text), "%u / %u",
             (unsigned)(emoji_grid.page + 1), (unsigned)pages);
    lv_label_set_text(emoji_grid.page_label, page_text);
    sample_stress_metrics();
    Serial.printf("[test] emoji page %u/%u: %u emoji, <=%u objects\n",
                  (unsigned)(emoji_grid.page + 1), (unsigned)pages,
                  (unsigned)page_items, (unsigned)SIGURDOS_TEST_EMOJI_OBJECT_LIMIT);
    return true;
}

static void emoji_page_cb(lv_event_t* e) {
    if (!e || !emoji_grid.dialog || !lv_obj_is_valid(emoji_grid.dialog)) return;
    lv_obj_t* target = (lv_obj_t*)lv_event_get_current_target(e);
    const uint16_t pages = sigurdos_test_controller_emoji_page_count(emoji_grid.total);
    if (target == emoji_grid.previous && emoji_grid.page > 0) {
        --emoji_grid.page;
    } else if (target == emoji_grid.next && emoji_grid.page + 1 < pages) {
        ++emoji_grid.page;
    } else {
        return;
    }
    if (!render_emoji_page()) fail_emoji_grid("page");
}

// Show a virtualized emoji grid for visual verification.  Only one page is
// materialized, keeping the dialog below the fixed object budget.
static void cmd_emoji() {
    lv_obj_t* parent = lv_scr_act();
    if (!parent) {
        Serial.println("[test] emoji: no active screen");
        return;
    }
    if (emoji_grid.dialog && lv_obj_is_valid(emoji_grid.dialog)) {
        Serial.println("[test] emoji: grid already open");
        return;
    }
    clear_emoji_grid_state();
    reset_stress_metrics();

    lv_obj_t* dlg = lv_obj_create(parent);
    if (!dlg) {
        Serial.println("[test] emoji: allocation failed (dialog)");
        return;
    }
    emoji_grid.dialog = dlg;
    lv_obj_add_event_cb(dlg, [](lv_event_t* e) {
        if ((lv_obj_t*)lv_event_get_target(e) == emoji_grid.dialog) {
            clear_emoji_grid_state();
        }
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_set_size(dlg, LV_PCT(100), LV_PCT(100));
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(0x0F0F0F), 0);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dlg, 0, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 4, 0);
    lv_obj_remove_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    emoji_grid.title = lv_label_create(dlg);
    if (!emoji_grid.title) { fail_emoji_grid("title"); return; }
    lv_obj_set_style_text_color(emoji_grid.title, lv_color_hex(0x00BFFF), 0);
    lv_obj_set_style_text_font(emoji_grid.title, &lv_font_montserrat_12, 0);
    lv_obj_align(emoji_grid.title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* close_btn = lv_btn_create(dlg);
    if (!close_btn) { fail_emoji_grid("close button"); return; }
    lv_obj_set_size(close_btn, 24, 20);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xCC3333), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(close_btn, 0, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    if (!close_lbl) { fail_emoji_grid("close label"); return; }
    lv_label_set_text(close_lbl, "X");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t*) { close_emoji_grid(); },
                        LV_EVENT_CLICKED, nullptr);

    emoji_grid.grid = lv_obj_create(dlg);
    if (!emoji_grid.grid) { fail_emoji_grid("grid"); return; }
    lv_obj_set_size(emoji_grid.grid, LV_PCT(96), LV_PCT(72));
    lv_obj_align(emoji_grid.grid, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_opa(emoji_grid.grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(emoji_grid.grid, 0, 0);
    lv_obj_set_style_pad_all(emoji_grid.grid, 4, 0);
    lv_obj_set_flex_flow(emoji_grid.grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(emoji_grid.grid, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(emoji_grid.grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(emoji_grid.grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(emoji_grid.grid, (lv_obj_flag_t)(
        LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
        LV_OBJ_FLAG_SCROLL_CHAIN));

    emoji_grid.previous = lv_btn_create(dlg);
    if (!emoji_grid.previous) { fail_emoji_grid("previous button"); return; }
    lv_obj_set_size(emoji_grid.previous, 54, 24);
    lv_obj_align(emoji_grid.previous, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    lv_obj_t* previous_label = lv_label_create(emoji_grid.previous);
    if (!previous_label) { fail_emoji_grid("previous label"); return; }
    lv_label_set_text(previous_label, "PREV");
    lv_obj_center(previous_label);
    lv_obj_add_event_cb(emoji_grid.previous, emoji_page_cb, LV_EVENT_CLICKED, nullptr);

    emoji_grid.page_label = lv_label_create(dlg);
    if (!emoji_grid.page_label) { fail_emoji_grid("page label"); return; }
    lv_obj_set_style_text_font(emoji_grid.page_label, &lv_font_montserrat_10, 0);
    lv_obj_align(emoji_grid.page_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    emoji_grid.next = lv_btn_create(dlg);
    if (!emoji_grid.next) { fail_emoji_grid("next button"); return; }
    lv_obj_set_size(emoji_grid.next, 54, 24);
    lv_obj_align(emoji_grid.next, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
    lv_obj_t* next_label = lv_label_create(emoji_grid.next);
    if (!next_label) { fail_emoji_grid("next label"); return; }
    lv_label_set_text(next_label, "NEXT");
    lv_obj_center(next_label);
    lv_obj_add_event_cb(emoji_grid.next, emoji_page_cb, LV_EVENT_CLICKED, nullptr);

    const int count = emoji_font_get_count();
    emoji_grid.total = count > 0 && count <= 0xFFFF ? static_cast<uint16_t>(count) : 0;
    emoji_grid.page = 0;
    if (!render_emoji_page()) fail_emoji_grid("initial page");
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

static void reset_capture_job() {
    if (capture_job.buffer) heap_caps_free(capture_job.buffer);
    capture_job = {};
    clear_diagnostic_line();
}

static void abort_capture(const char* reason, bool announce) {
    if (announce) {
        Serial.printf("[capture] ABORT: %s\n", reason ? reason : "cancelled");
    }
    reset_capture_job();
}

static void cmd_capture(const char* arg) {
    while (arg && *arg == ' ') ++arg;
    if (arg && strcmp(arg, "cancel") == 0) {
        if (capture_job.phase == CapturePhase::Idle) {
            Serial.println("[test] capture: no capture active");
        } else {
            abort_capture("cancelled", true);
        }
        return;
    }
    if (arg && arg[0]) {
        Serial.println("[test] capture: usage: capture [cancel]");
        return;
    }
    if (capture_job.phase != CapturePhase::Idle) {
        Serial.println("[test] capture: already active (use 'capture cancel')");
        return;
    }
    if (tree_job.active) {
        Serial.println("[test] capture: tree dump active (use 'tree cancel')");
        return;
    }

    lv_display_t* disp = lv_display_get_default();
    if (!disp) {
        Serial.println("[test] capture: no display");
        return;
    }
    const uint32_t width = (uint32_t)lv_display_get_horizontal_resolution(disp);
    const uint32_t height = (uint32_t)lv_display_get_vertical_resolution(disp);
    const uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    if (!sigurdos_test_controller_capture_size_allowed(width, height, stride)) {
        Serial.printf("[test] capture: frame exceeds %lu-byte bound\n",
                      (unsigned long)SIGURDOS_TEST_CAPTURE_MAX_BYTES);
        return;
    }

    const uint32_t now = millis();
    capture_job.phase = CapturePhase::Snapshot;
    capture_job.width = width;
    capture_job.height = height;
    capture_job.stride = stride;
    capture_job.total_bytes = stride * height;
    capture_job.offset = 0;
    capture_job.started_ms = now;
    capture_job.last_progress_ms = now;
    reset_stress_metrics();
    Serial.printf("[test] capture queued: %lu bytes (cancel with 'capture cancel')\n",
                  (unsigned long)capture_job.total_bytes);
}

static void service_capture(uint32_t now) {
    if (capture_job.phase == CapturePhase::Idle) return;
    if (sigurdos_test_controller_timeout_elapsed(
            now, capture_job.started_ms, CAPTURE_TOTAL_TIMEOUT_MS)) {
        abort_capture("deadline exceeded", Serial.availableForWrite() > 0);
        return;
    }
    if (sigurdos_test_controller_timeout_elapsed(
            now, capture_job.last_progress_ms, DIAGNOSTIC_STALL_TIMEOUT_MS)) {
        abort_capture("serial stalled", Serial.availableForWrite() > 0);
        return;
    }
    if (diagnostic_line_pending()) return;

    if (capture_job.phase == CapturePhase::Snapshot) {
        capture_job.buffer = (uint8_t*)heap_caps_malloc(
            capture_job.total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!capture_job.buffer) {
            abort_capture("snapshot allocation failed", true);
            return;
        }
        lv_obj_t* screen = lv_scr_act();
        if (!screen || !lv_obj_is_valid(screen)) {
            abort_capture("active screen unavailable", true);
            return;
        }

        lv_draw_buf_t snapshot;
        lv_draw_buf_init(&snapshot, capture_job.width, capture_job.height,
                         LV_COLOR_FORMAT_RGB565, capture_job.stride,
                         capture_job.buffer, capture_job.total_bytes);
        if (lv_snapshot_take_to_draw_buf(screen, LV_COLOR_FORMAT_RGB565, &snapshot) !=
            LV_RES_OK) {
            abort_capture("snapshot failed", true);
            return;
        }
        sample_stress_metrics();
        capture_job.phase = CapturePhase::Header;
        capture_job.last_progress_ms = now;
    }

    if (capture_job.phase == CapturePhase::Header) {
        const int n = snprintf(diagnostic_line.data, sizeof(diagnostic_line.data),
                               "[capture] W=%lu H=%lu S=%lu\n",
                               (unsigned long)capture_job.width,
                               (unsigned long)capture_job.height,
                               (unsigned long)capture_job.stride);
        queue_diagnostic_line(n > 0 ? static_cast<size_t>(n) : 0);
        capture_job.phase = CapturePhase::Data;
    } else if (capture_job.phase == CapturePhase::Data) {
        if (capture_job.offset >= capture_job.total_bytes) {
            capture_job.phase = CapturePhase::Stats;
        } else {
            const uint32_t remaining = capture_job.total_bytes - capture_job.offset;
            const uint32_t chunk = remaining > CAPTURE_DATA_BYTES_PER_LINE
                                       ? CAPTURE_DATA_BYTES_PER_LINE
                                       : remaining;
            size_t n = 0;
            n += static_cast<size_t>(snprintf(diagnostic_line.data + n,
                                              sizeof(diagnostic_line.data) - n,
                                              "[cdata] "));
            for (uint32_t i = 0; i < chunk; ++i) {
                n += static_cast<size_t>(snprintf(
                    diagnostic_line.data + n, sizeof(diagnostic_line.data) - n,
                    "%02X", capture_job.buffer[capture_job.offset + i]));
                if (n >= sizeof(diagnostic_line.data)) n = sizeof(diagnostic_line.data) - 1;
            }
            n += static_cast<size_t>(snprintf(diagnostic_line.data + n,
                                              sizeof(diagnostic_line.data) - n,
                                              "\n"));
            queue_diagnostic_line(n);
            capture_job.offset += chunk;
        }
    }

    if (capture_job.phase == CapturePhase::Stats && !diagnostic_line_pending()) {
        queue_diagnostic_line(format_stress_line("capture"));
        capture_job.phase = CapturePhase::End;
    } else if (capture_job.phase == CapturePhase::End && !diagnostic_line_pending()) {
        const char end[] = "[capture] END\n";
        memcpy(diagnostic_line.data, end, sizeof(end) - 1);
        queue_diagnostic_line(sizeof(end) - 1);
        capture_job.phase = CapturePhase::Done;
    } else if (capture_job.phase == CapturePhase::Done && !diagnostic_line_pending()) {
        reset_capture_job();
    }
}

static const char* widget_type_name(lv_obj_t* obj) {
    if (!obj) return "?";
    const char* type = "?";
    if (lv_obj_check_type(obj, &lv_button_class))      type = "btn";
    else if (lv_obj_check_type(obj, &lv_label_class))   type = "label";
    else if (lv_obj_check_type(obj, &lv_image_class))   type = "img";
    else if (lv_obj_check_type(obj, &lv_textarea_class)) type = "textarea";
    else if (lv_obj_check_type(obj, &lv_list_class))    type = "list";
    else if (lv_obj_check_type(obj, &lv_roller_class))  type = "roller";
    else if (lv_obj_check_type(obj, &lv_dropdown_class)) type = "dropdown";
    else if (lv_obj_check_type(obj, &lv_obj_class))     type = "obj";
    return type;
}

static size_t format_widget_tree_line(lv_obj_t* obj, uint8_t depth) {
    if (!obj || !lv_obj_is_valid(obj)) return 0;
    size_t n = 0;
    for (uint8_t i = 0; i < depth && n + 2 < sizeof(diagnostic_line.data); ++i) {
        diagnostic_line.data[n++] = ' ';
        diagnostic_line.data[n++] = ' ';
    }

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const int written = snprintf(
        diagnostic_line.data + n, sizeof(diagnostic_line.data) - n,
        "%s x=%d y=%d w=%d h=%d visible=%d",
        widget_type_name(obj), lv_obj_get_x(obj), lv_obj_get_y(obj),
        coords.x2 - coords.x1 + 1, coords.y2 - coords.y1 + 1,
        !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN));
    if (written <= 0) return 0;
    n += static_cast<size_t>(written);
    if (n >= sizeof(diagnostic_line.data)) n = sizeof(diagnostic_line.data) - 1;

    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char* text = lv_label_get_text(obj);
        if (text) {
            char buf[48];
            sigurdos::utf8_copy_truncate(buf, sizeof(buf), text);
            for (char* p = buf; *p; p++) if (*p == '\n') *p = ' ';
            const int text_written = snprintf(
                diagnostic_line.data + n, sizeof(diagnostic_line.data) - n,
                " \"%s\"", buf);
            if (text_written > 0) n += static_cast<size_t>(text_written);
            if (n >= sizeof(diagnostic_line.data)) n = sizeof(diagnostic_line.data) - 1;
        }
    }
    if (n + 1 < sizeof(diagnostic_line.data)) diagnostic_line.data[n++] = '\n';
    return n;
}

static void reset_tree_job() {
    tree_job = {};
    clear_diagnostic_line();
}

static void abort_tree(const char* reason, bool announce) {
    if (announce) Serial.printf("[test] tree: aborted (%s)\n", reason ? reason : "cancelled");
    reset_tree_job();
}

static void cmd_tree(const char* arg) {
    while (arg && *arg == ' ') ++arg;
    if (arg && strcmp(arg, "cancel") == 0) {
        if (!tree_job.active) Serial.println("[test] tree: no dump active");
        else abort_tree("cancelled", true);
        return;
    }
    if (arg && arg[0]) {
        Serial.println("[test] tree: usage: tree [cancel]");
        return;
    }
    if (tree_job.active) {
        Serial.println("[test] tree: already active (use 'tree cancel')");
        return;
    }
    if (capture_job.phase != CapturePhase::Idle) {
        Serial.println("[test] tree: capture active (use 'capture cancel')");
        return;
    }

    lv_obj_t* scr = lv_scr_act();
    if (!scr) { Serial.println("[test] tree: no active screen"); return; }
    const uint32_t now = millis();
    tree_job.active = true;
    tree_job.stack_size = 1;
    tree_job.stack[0] = {scr, 0, false};
    tree_job.started_ms = now;
    tree_job.last_progress_ms = now;
    reset_stress_metrics();
    Serial.printf("[test] tree queued: depth<=%u nodes<=%u (cancel with 'tree cancel')\n",
                  (unsigned)SIGURDOS_TEST_TREE_MAX_DEPTH,
                  (unsigned)SIGURDOS_TEST_TREE_MAX_NODES);
}

static void service_tree(uint32_t now) {
    if (!tree_job.active) return;
    if (sigurdos_test_controller_timeout_elapsed(
            now, tree_job.started_ms, TREE_TOTAL_TIMEOUT_MS)) {
        abort_tree("deadline exceeded", Serial.availableForWrite() > 0);
        return;
    }
    if (sigurdos_test_controller_timeout_elapsed(
            now, tree_job.last_progress_ms, DIAGNOSTIC_STALL_TIMEOUT_MS)) {
        abort_tree("serial stalled", Serial.availableForWrite() > 0);
        return;
    }
    if (diagnostic_line_pending()) return;

    if (tree_job.finishing) {
        if (tree_job.summary_queued) {
            reset_tree_job();
            return;
        }
        sample_stress_metrics();
        const int n = snprintf(
            diagnostic_line.data, sizeof(diagnostic_line.data),
            "[test] tree: nodes=%u max_depth=%u depth_limited=%u node_limited=%u "
            "stack_hwm_words=%lu lvgl_min_free=%lu lvgl_max_used_pct=%u\n",
            (unsigned)tree_job.nodes, (unsigned)tree_job.max_depth_seen,
            tree_job.depth_limited ? 1U : 0U, tree_job.node_limited ? 1U : 0U,
            (unsigned long)stress_min_stack_words,
            (unsigned long)stress_min_lvgl_free,
            (unsigned)stress_max_lvgl_used_pct);
        queue_diagnostic_line(n > 0 ? static_cast<size_t>(n) : 0);
        tree_job.summary_queued = true;
        return;
    }

    while (tree_job.stack_size > 0) {
        const uint8_t depth = tree_job.stack_size - 1;
        TreeFrame& frame = tree_job.stack[depth];
        if (!frame.obj || !lv_obj_is_valid(frame.obj)) {
            --tree_job.stack_size;
            continue;
        }

        if (!frame.printed) {
            if (tree_job.nodes >= SIGURDOS_TEST_TREE_MAX_NODES) {
                tree_job.node_limited = true;
                tree_job.finishing = true;
                return;
            }
            const size_t line_len = format_widget_tree_line(frame.obj, depth);
            frame.printed = true;
            ++tree_job.nodes;
            if (depth > tree_job.max_depth_seen) tree_job.max_depth_seen = depth;
            sample_stress_metrics();
            if (line_len > 0) queue_diagnostic_line(line_len);
            return;
        }

        const uint32_t child_count = lv_obj_get_child_count(frame.obj);
        if (!sigurdos_test_controller_tree_can_descend(depth)) {
            if (child_count > 0) tree_job.depth_limited = true;
            --tree_job.stack_size;
            continue;
        }
        if (frame.next_child >= child_count) {
            --tree_job.stack_size;
            continue;
        }

        lv_obj_t* child = lv_obj_get_child(frame.obj, frame.next_child++);
        if (!child || !lv_obj_is_valid(child)) continue;
        tree_job.stack[tree_job.stack_size++] = {child, 0, false};
    }

    tree_job.finishing = true;
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
    sigurdos_test_set_touch(x, y);
    Serial.printf("[test] tap %d %d\n", x, y);
}

static void cmd_backlight(const char* arg) {
    if (!arg || !arg[0]) {
        Serial.printf("[test] backlight: %s\n", sigurdos_display_is_on() ? "on" : "off");
        return;
    }
    int val = atoi(arg);
    if (val < 0 || val > 255) {
        Serial.println("[test] backlight: brightness must be 0-255");
        return;
    }
    sigurdos_display_set_brightness((uint8_t)val);
    if (val > 0) {
        sigurdos_keyboard_set_brightness(sigurdos::prefs_get().kbd_backlight);
    } else {
        sigurdos_keyboard_set_brightness(0);
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
    bool ok = sigurdos::mesh::sendChannelMessage(channel, text);
    if (ok) {
        Serial.printf("[test] sendchannel OK: #%s sent %d chars\n", channel, (int)strlen(text));
    } else {
        Serial.printf("[test] sendchannel FAILED: channel \"%s\" not found or send error\n", channel);
    }
}

// ── Send DM ─────────────────────────────────────
static void cmd_sendmessage(const char* arg) {
    if (!arg || arg[0] == '\0') {
        Serial.println("[test] sendmessage: usage: sendmessage <contact_name> <text>");
        return;
    }
    char name[64];
    const char* text;
    if (arg[0] == '"') {
        // Quoted name: "Heltec Room" text
        const char* endq = strchr(arg + 1, '"');
        if (!endq) { Serial.println("[test] sendmessage: mismatched quote"); return; }
        size_t nlen = endq - (arg + 1);
        if (nlen > 63) nlen = 63;
        memcpy(name, arg + 1, nlen);
        name[nlen] = 0;
        text = endq + 1;
    } else {
        const char* space = strchr(arg, ' ');
        if (!space) {
            Serial.println("[test] sendmessage: missing text after contact name");
            return;
        }
        size_t name_len = space - arg;
        if (name_len > 63) name_len = 63;
        memcpy(name, arg, name_len);
        name[name_len] = '\0';
        text = space + 1;
    }
    while (*text == ' ') text++;
    if (text[0] == '\0') {
        Serial.println("[test] sendmessage: missing text after contact name");
        return;
    }

    uint32_t send_ts = sigurdos::mesh::sendMessage(name, text);
    if (send_ts == 0) {
        Serial.printf("[test] sendmessage FAILED: DM to %s was not sent\n", name);
        return;
    }
    Serial.printf("[test] sendmessage QUEUED: DM to %s sent %d chars "
                  "timestamp=%lu; awaiting peer ACK\n",
                  name, (int)strlen(text), (unsigned long)send_ts);

    char dm_channel[64];
    snprintf(dm_channel, sizeof(dm_channel), "DM: %.59s", name);
    const char* own = sigurdos::mesh::getOwnName();
    sigurdos::ui::chat_screen_add_msg_at(
        dm_channel, own ? own : "self", text, send_ts, true);
}

#if !defined(SIGURDOS_REMOTE_TEST_RADIO) || !SIGURDOS_REMOTE_TEST_RADIO
// ACK injection is intentionally unavailable in radio profiles: delivery in
// those builds must only be reported after MeshCore receives a real peer ACK.
static void cmd_simulateack(const char* arg) {
    if (!arg || !arg[0]) {
        Serial.println("[test] simulateack: usage: simulateack <contact_name> <timestamp>");
        return;
    }

    const char* split = strrchr(arg, ' ');
    if (!split || split == arg || !split[1]) {
        Serial.println("[test] simulateack: usage: simulateack <contact_name> <timestamp>");
        return;
    }
    char* end = nullptr;
    const unsigned long long parsed = strtoull(split + 1, &end, 10);
    if (!end || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) {
        Serial.println("[test] simulateack: timestamp must be a non-zero uint32");
        return;
    }

    char name[64];
    size_t name_len = static_cast<size_t>(split - arg);
    if (name_len >= sizeof(name)) name_len = sizeof(name) - 1;
    memcpy(name, arg, name_len);
    name[name_len] = '\0';
    sigurdos::mesh::registerAckedMessage(name, static_cast<uint32_t>(parsed));
    Serial.printf("[test] SIMULATED ACK: DM to %s timestamp=%lu (no-radio profile)\n",
                  name, static_cast<unsigned long>(parsed));
}
#endif

// ── Open DM ──────────────────────────────────────
static void cmd_opendm(const char* arg) {
    if (!arg || arg[0] == '\0') {
        Serial.println("[test] opendm: usage: opendm <contact_name>");
        return;
    }
    sigurdos::ui::chat_screen_open_dm(arg);
    Serial.printf("[test] opendm: opened DM with %s\n", arg);
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
        bool ok = sigurdos::mesh::addChannel(name, psk);
        if (ok) {
            Serial.printf("[test] addchannel OK: #%s with PSK\n", name);
        }
        else Serial.printf("[test] addchannel FAILED: #%s\n", name);
    } else {
        // No PSK — try as hashtag channel
        bool ok = sigurdos::mesh::addHashtagChannel(arg);
        if (ok) Serial.printf("[test] addchannel OK: hashtag #%s\n", arg);
        else Serial.printf("[test] addchannel FAILED: hashtag #%s\n", arg);
    }
}

// ── Remove channel ───────────────────────────────────
static void cmd_removechannel(const char* arg) {
    if (!arg || arg[0] == '\0') {
        Serial.println("[test] removechannel: usage: removechannel <idx|name>");
        return;
    }

    int idx = -1;
    bool numeric = true;
    for (const char* p = arg; *p; ++p) {
        if (!isdigit((unsigned char)*p)) {
            numeric = false;
            break;
        }
    }
    if (numeric) {
        idx = atoi(arg);
    } else {
        char names[8][37] = {};
        const int n = sigurdos::mesh::exportChannels(names, 8);
        const char* wanted = arg[0] == '#' ? arg + 1 : arg;
        for (int i = 0; i < n; ++i) {
            const char* have = names[i][0] == '#' ? names[i] + 1 : names[i];
            if (strcmp(have, wanted) == 0) {
                idx = i;
                break;
            }
        }
    }

    if (idx < 0) {
        Serial.printf("[test] removechannel FAILED: %s not found\n", arg);
        return;
    }

    bool ok = sigurdos::mesh::removeChannel(idx);
    Serial.printf("[test] removechannel %s: idx=%d %s\n", arg, idx, ok ? "OK" : "FAILED");
}

// ── Keyboard diagnostics ───────────────────────────────
static void cmd_keydiag() {
    SigurdOSKeyboardDiag d{};
    bool ok = sigurdos_keyboard_get_diag(&d);
    char key_char = (d.last_key_mode_byte >= 0x20 && d.last_key_mode_byte < 0x7F)
        ? (char)d.last_key_mode_byte
        : '.';
    char out_char = (d.last_output_codepoint >= 0x20 && d.last_output_codepoint < 0x7F)
        ? (char)d.last_output_codepoint
        : '.';
    Serial.printf("[test] keydiag: ok=%d init=%d raw_supported=%d raw_unavailable=%d raw_valid=%d layout=%u\n",
                  ok ? 1 : 0, d.initialized ? 1 : 0, d.raw_supported ? 1 : 0,
                  d.raw_unavailable ? 1 : 0, d.raw_valid ? 1 : 0, d.layout);
    Serial.printf("[test] keydiag: keymode=0x%02X '%c' output=U+%04lX '%c' events=%lu overwrites=%lu last_ms=%lu\n",
                  d.last_key_mode_byte, key_char,
                  (unsigned long)d.last_output_codepoint, out_char,
                  (unsigned long)d.event_count,
                  (unsigned long)d.overwrite_count,
                  (unsigned long)d.last_event_ms);
    Serial.printf("[test] keydiag: raw=%02X %02X %02X %02X %02X shift=%d ctrl=%d alt=%d sym=%d mic=%d\n",
                  d.raw_matrix[0], d.raw_matrix[1], d.raw_matrix[2],
                  d.raw_matrix[3], d.raw_matrix[4],
                  d.shift ? 1 : 0, d.ctrl ? 1 : 0, d.alt ? 1 : 0,
                  d.sym_down ? 1 : 0, d.mic_down ? 1 : 0);
}

static const char* trackball_event_label(SigurdOSTrackballEvent event)
{
    switch (event) {
    case SigurdOSTrackballEvent::Up: return "up";
    case SigurdOSTrackballEvent::Down: return "down";
    case SigurdOSTrackballEvent::Left: return "left";
    case SigurdOSTrackballEvent::Right: return "right";
    case SigurdOSTrackballEvent::Click: return "click";
    case SigurdOSTrackballEvent::None:
    default: return "none";
    }
}

// ── Touch + trackball diagnostics ────────────────────────
static void cmd_inputdiag() {
    SigurdOSTouchDiag td{};
    bool touch_ok = sigurdos_touch_get_diag(&td);
    Serial.printf("[test] inputdiag touch: ok=%d init=%d attempted=%d addr=0x%02X pressed=%d edge_release=%d x=%d y=%d errors=%d\n",
                  touch_ok ? 1 : 0, td.initialized ? 1 : 0,
                  td.init_attempted ? 1 : 0, td.i2c_addr,
                  td.pressed ? 1 : 0, td.edge_release_pending ? 1 : 0,
                  td.x, td.y, td.consecutive_i2c_errors);
    Serial.printf("[test] inputdiag touch: presses=%lu releases=%lu moves=%lu last_ms=%lu\n",
                  (unsigned long)td.press_count,
                  (unsigned long)td.release_count,
                  (unsigned long)td.move_count,
                  (unsigned long)td.last_event_ms);

    SigurdOSTrackballDiag bd{};
    bool tb_ok = sigurdos_trackball_get_diag(&bd);
    Serial.printf("[test] inputdiag trackball: ok=%d init=%d queue=%u last=%s events=%lu overflows=%lu last_ms=%lu\n",
                  tb_ok ? 1 : 0, bd.initialized ? 1 : 0,
                  bd.queue_count, trackball_event_label(bd.last_event),
                  (unsigned long)bd.event_count,
                  (unsigned long)bd.overflow_count,
                  (unsigned long)bd.last_event_ms);
    Serial.printf("[test] inputdiag trackball: raw(U,D,L,R,C)=%u,%u,%u,%u,%u active=%d,%d,%d,%d,%d\n",
                  bd.raw_levels[0], bd.raw_levels[1], bd.raw_levels[2],
                  bd.raw_levels[3], bd.raw_levels[4],
                  bd.active[0] ? 1 : 0, bd.active[1] ? 1 : 0,
                  bd.active[2] ? 1 : 0, bd.active[3] ? 1 : 0,
                  bd.active[4] ? 1 : 0);
}

static const char* gps_diag_assessment()
{
    if (sigurdos_gps_active_baud() == 0 && sigurdos_gps_chars_processed() == 0) {
        return "not_initialized_or_no_uart";
    }
    if (sigurdos_gps_chars_processed() == 0) return "no_uart_chars";
    if (sigurdos_gps_sentences_received() == 0) return "partial_uart_no_lines";
    if (sigurdos_gps_valid_sentences() == 0) {
        return sigurdos_gps_checksum_failures() > 0
            ? "checksum_failures"
            : "no_valid_nmea";
    }
    if (sigurdos_gps_has_fix()) return "fix";
    if (sigurdos_gps_gsv_sentences() == 0) return "valid_nmea_no_gsv";
    if (sigurdos_gps_satellites_in_view() == 0) return "no_satellites_visible";
    if (sigurdos_gps_gsv_snr_count() == 0) return "satellites_no_snr";
    return "satellites_waiting_fix";
}

// ── GPS diagnostics ──────────────────────────────────────
static void cmd_gpsdiag() {
    const char rmc = sigurdos_gps_rmc_status() ? sigurdos_gps_rmc_status() : '-';
    Serial.printf("[test] gpsdiag: assessment=%s fix=%d qual=%u sv=%u siv=%u ft=%u rmc=%c snr_max=%u snr_count=%u\n",
                  gps_diag_assessment(),
                  sigurdos_gps_has_fix() ? 1 : 0,
                  (unsigned)sigurdos_gps_fix_quality(),
                  (unsigned)sigurdos_gps_satellites(),
                  (unsigned)sigurdos_gps_satellites_in_view(),
                  (unsigned)sigurdos_gps_fix_type(),
                  rmc,
                  (unsigned)sigurdos_gps_gsv_snr_max(),
                  (unsigned)sigurdos_gps_gsv_snr_count());
    Serial.printf("[test] gpsdiag: baud=%lu chars=%lu sent=%lu valid=%lu gga=%lu rmc_s=%lu gsv=%lu gsa=%lu csfail=%lu switches=%lu\n",
                  (unsigned long)sigurdos_gps_active_baud(),
                  (unsigned long)sigurdos_gps_chars_processed(),
                  (unsigned long)sigurdos_gps_sentences_received(),
                  (unsigned long)sigurdos_gps_valid_sentences(),
                  (unsigned long)sigurdos_gps_gga_sentences(),
                  (unsigned long)sigurdos_gps_rmc_sentences(),
                  (unsigned long)sigurdos_gps_gsv_sentences(),
                  (unsigned long)sigurdos_gps_gsa_sentences(),
                  (unsigned long)sigurdos_gps_checksum_failures(),
                  (unsigned long)sigurdos_gps_baud_switches());
    Serial.printf("[test] gpsdiag: lat=%.6f lon=%.6f alt=%.1f speed=%.1f heading=%.1f time=%02u:%02u:%02u synced=%d\n",
                  (double)sigurdos_gps_latitude(),
                  (double)sigurdos_gps_longitude(),
                  (double)sigurdos_gps_altitude_m(),
                  (double)sigurdos_gps_speed_kn(),
                  (double)sigurdos_gps_heading(),
                  (unsigned)sigurdos_gps_hour(),
                  (unsigned)sigurdos_gps_minute(),
                  (unsigned)sigurdos_gps_second(),
                  sigurdos_gps_time_synced() ? 1 : 0);
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

// ── Cmd: getrf ────────────────────────────────────────────
static void cmd_buildinfo() {
    const auto& build = sigurdos::build::info();
    Serial.printf("[test] build: git=%s|dirty=%d|mcore=%s|env=%s\n",
                  build.git_sha, build.git_dirty ? 1 : 0,
                  build.meshcore_sha, build.build_env);
}

static void cmd_getrf() {
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();

    if (!p.configured) {
        Serial.println(F("[test] getrf: radio not configured yet"));
        return;
    }

    Serial.printf("[test] getrf: profile=%s freq=%.3f SF=%d BW=%.1f CR=%d TX=%d dBm RX_BOOST=%d channels=%d\n",
#if defined(SIGURDOS_REMOTE_TEST_RX_ONLY)
                  "remote_usca_rxonly",
#elif defined(SIGURDOS_REMOTE_TEST_RADIO)
                  "remote_radio",
#elif defined(SIGURDOS_REMOTE_TEST)
                  "remote_no_radio",
#else
                  "normal",
#endif
                  p.freq, (int)p.sf, p.bw, (int)p.cr, (int)p.tx_power_dbm,
                  (int)p.rx_boosted_gain,
                  sigurdos::mesh::getChannelCount());
    Serial.printf("[test] getrf: noise=%d rssi=%d snr=%.1f tx_air_ms=%lu rx_air_ms=%lu sent_flood=%lu sent_direct=%lu recv_flood=%lu recv_direct=%lu pktlog=%d\n",
                  sigurdos::mesh::getNoiseFloor(),
                  sigurdos::mesh::getLastRSSI(),
                  (double)sigurdos::mesh::getLastSNR(),
                  (unsigned long)sigurdos::mesh::getTotalTxAirtimeMs(),
                  (unsigned long)sigurdos::mesh::getTotalRxAirtimeMs(),
                  (unsigned long)sigurdos::mesh::getNumSentFlood(),
                  (unsigned long)sigurdos::mesh::getNumSentDirect(),
                  (unsigned long)sigurdos::mesh::getNumRecvFlood(),
                  (unsigned long)sigurdos::mesh::getNumRecvDirect(),
                  sigurdos::mesh::getPacketLogCount());
    Serial.printf("[test] getrf: ack_events=%d delivery_events=%d pending_dropped=%lu pending_expired=%lu\n",
                  sigurdos::mesh::getAckCounter(),
                  sigurdos::mesh::getDeliveryCounter(),
                  (unsigned long)sigurdos::mesh::getPendingAckDropCount(),
                  (unsigned long)sigurdos::mesh::getPendingAckExpiredCount());
#if defined(SIGURDOS_REMOTE_TEST_RX_ONLY)
    Serial.println(F("[test] getrf: remote-test RX-only mode enabled; TX commands are blocked"));
#endif
}

// ── Cmd: setrf ────────────────────────────────────────────
static void cmd_setrf(const char* arg) {
    SigurdOSTestRfParams rf{};
    int parsed_fields = 0;
    const SigurdOSTestRfParseResult parse =
        sigurdos_test_controller_parse_rf_params(arg, &rf, &parsed_fields);
    if (parse != SigurdOSTestRfParseResult::Ok) {
        if (parse == SigurdOSTestRfParseResult::MissingArgs) {
            Serial.println(F("[test] setrf: usage: setrf <freq> <sf> <bw> <cr> <tx_pwr>"));
            Serial.println(F("[test] setrf: e.g. setrf 869.525 10 250 5 22"));
        } else if (parse == SigurdOSTestRfParseResult::BadArgumentCount) {
            Serial.printf("[test] setrf: expected 5 args, got %d\n", parsed_fields);
        } else if (parse == SigurdOSTestRfParseResult::FrequencyOutOfRange) {
            Serial.println("[test] setrf: freq out of range (400-1000 MHz)");
        } else if (parse == SigurdOSTestRfParseResult::SpreadingFactorOutOfRange) {
            Serial.println("[test] setrf: SF out of range (6-12)");
        } else if (parse == SigurdOSTestRfParseResult::BandwidthOutOfRange) {
            Serial.println("[test] setrf: BW out of range (7.8-500 kHz)");
        } else if (parse == SigurdOSTestRfParseResult::CodingRateOutOfRange) {
            Serial.println("[test] setrf: CR out of range (5-8)");
        } else if (parse == SigurdOSTestRfParseResult::TxPowerOutOfRange) {
            Serial.println("[test] setrf: TX power out of range (2-22 dBm)");
        }
        return;
    }

    sigurdos::NodePrefs p = sigurdos::prefs_get();
    p.freq = rf.freq;
    p.sf = rf.sf;
    p.bw = rf.bw;
    p.cr = rf.cr;
    p.tx_power_dbm = rf.tx_power_dbm;
    p.rx_boosted_gain = rf.rx_boosted_gain;
    p.configured = true;

    if (prefs_save(p)) {
        sigurdos::prefs_set(p);
        Serial.println(F("[test] setrf: radio params saved to NVS"));
        Serial.printf("[test] setrf: freq=%.3f SF=%d BW=%.1f CR=%d TX=%d dBm RX_BOOST=%d\n",
                      rf.freq, (int)rf.sf, rf.bw, (int)rf.cr, (int)rf.tx_power_dbm,
                      (int)rf.rx_boosted_gain);
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

// ── Cmd: factoryreset ─────────────────────────────────────────
static void cmd_factoryreset() {
    Serial.println(F("[test] factory reset: erasing all data and rebooting..."));
    Serial.flush();
    if (!sigurdos::mesh::factoryReset()) {
        Serial.println(F("[test] factory reset: FAILED; BLE remains disabled"));
    }
}

// ── Cmd: advert ───────────────────────────────────────────
static void cmd_advert() {
    bool ok = sigurdos::mesh::sendAdvert();
    Serial.printf("[test] advert: %s\n", ok ? "sent" : "FAILED");
}

static void finish_stress_chat() {
    const LvPoolState final_state = read_lv_pool_state();
    log_lv_pool_state(stress_chat_job.completed_cycles, "final-home", final_state);
    const bool failed = lv_pool_degraded(stress_chat_job.baseline, final_state);
    if (failed) {
        log_lv_pool_degradation(stress_chat_job.completed_cycles, "baseline",
                                stress_chat_job.baseline, final_state);
    }
    Serial.printf("[stresschat] %s cycles=%u baseline_free=%lu final_free=%lu "
                  "baseline_largest=%lu final_largest=%lu baseline_frag=%u final_frag=%u\n",
                  failed ? "FAIL" : "PASS",
                  static_cast<unsigned>(stress_chat_job.completed_cycles),
                  static_cast<unsigned long>(stress_chat_job.baseline.free),
                  static_cast<unsigned long>(final_state.free),
                  static_cast<unsigned long>(stress_chat_job.baseline.largest),
                  static_cast<unsigned long>(final_state.largest),
                  static_cast<unsigned>(stress_chat_job.baseline.frag),
                  static_cast<unsigned>(final_state.frag));
    stress_chat_job = {};
}

static void cmd_stresschat(const char* arg) {
    if (arg && strcmp(arg, "cancel") == 0) {
        if (stress_chat_job.phase == StressChatPhase::Idle) {
            Serial.println("[stresschat] no test is running");
        } else {
            Serial.printf("[stresschat] CANCELLED after %u cycles\n",
                          static_cast<unsigned>(stress_chat_job.completed_cycles));
            stress_chat_job = {};
        }
        return;
    }
    if (stress_chat_job.phase != StressChatPhase::Idle) {
        Serial.println("[stresschat] already running (use 'stresschat cancel')");
        return;
    }
    if (capture_job.phase != CapturePhase::Idle || tree_job.active || type_count > 0) {
        Serial.println("[stresschat] busy: finish capture/tree/type work first");
        return;
    }

    long cycles = STRESS_CHAT_DEFAULT_CYCLES;
    if (arg) {
        char* end = nullptr;
        cycles = strtol(arg, &end, 10);
        while (end && *end == ' ') ++end;
        if (!end || *end != '\0' || cycles < 1 || cycles > STRESS_CHAT_MAX_CYCLES) {
            Serial.printf("[stresschat] usage: stresschat [1..%u]\n",
                          static_cast<unsigned>(STRESS_CHAT_MAX_CYCLES));
            return;
        }
    }

    stress_chat_job = {};
    stress_chat_job.phase = StressChatPhase::SettleBaseline;
    stress_chat_job.requested_cycles = static_cast<uint16_t>(cycles);
    stress_chat_job.settle_passes = STRESS_CHAT_SETTLE_PASSES;
    if (sigurdos::ui::current_screen() != sigurdos::ui::Screen::Home) {
        sigurdos::ui::navigate_to(sigurdos::ui::Screen::Home);
    }
    Serial.printf("[stresschat] START cycles=%u channel_every=%u settle_passes=%u\n",
                  static_cast<unsigned>(stress_chat_job.requested_cycles),
                  static_cast<unsigned>(STRESS_CHAT_CHANNEL_INTERVAL),
                  static_cast<unsigned>(STRESS_CHAT_SETTLE_PASSES));
}

static bool stress_chat_settled() {
    // Process deferred screen deletion and normal LVGL timers without blocking
    // the firmware loop or hiding the lifecycle race behind delay().
    lv_timer_handler();
    if (stress_chat_job.settle_passes > 1) {
        --stress_chat_job.settle_passes;
        return false;
    }
    stress_chat_job.settle_passes = STRESS_CHAT_SETTLE_PASSES;
    return true;
}

static void service_stress_chat() {
    if (stress_chat_job.phase == StressChatPhase::Idle) return;

    switch (stress_chat_job.phase) {
    case StressChatPhase::SettleBaseline:
        if (!stress_chat_settled()) return;
        stress_chat_job.baseline = read_lv_pool_state();
        stress_chat_job.previous_home = stress_chat_job.baseline;
        log_lv_pool_state(0, "baseline-home", stress_chat_job.baseline);
        stress_chat_job.phase = StressChatPhase::NavigateChat;
        break;
    case StressChatPhase::NavigateChat:
        sigurdos::ui::navigate_to(sigurdos::ui::Screen::Chat);
        stress_chat_job.phase = StressChatPhase::SettleChat;
        break;
    case StressChatPhase::SettleChat:
        if (!stress_chat_settled()) return;
        stress_chat_job.phase = StressChatPhase::NavigateHome;
        break;
    case StressChatPhase::NavigateHome:
        sigurdos::ui::go_back();
        stress_chat_job.phase = StressChatPhase::SettleHome;
        break;
    case StressChatPhase::SettleHome: {
        if (!stress_chat_settled()) return;
        ++stress_chat_job.completed_cycles;
        const LvPoolState current = read_lv_pool_state();
        log_lv_pool_state(stress_chat_job.completed_cycles, "home", current);
        log_lv_pool_degradation(stress_chat_job.completed_cycles, "baseline",
                                stress_chat_job.baseline, current);
        log_lv_pool_degradation(stress_chat_job.completed_cycles, "previous-home",
                                stress_chat_job.previous_home, current);
        stress_chat_job.previous_home = current;

        if ((stress_chat_job.completed_cycles % STRESS_CHAT_CHANNEL_INTERVAL) == 0) {
            stress_chat_job.phase = StressChatPhase::NavigateChannelChat;
        } else if (stress_chat_job.completed_cycles >= stress_chat_job.requested_cycles) {
            finish_stress_chat();
        } else {
            stress_chat_job.phase = StressChatPhase::NavigateChat;
        }
        break;
    }
    case StressChatPhase::NavigateChannelChat:
        sigurdos::ui::navigate_to(sigurdos::ui::Screen::Chat);
        stress_chat_job.phase = StressChatPhase::SettleChannelList;
        break;
    case StressChatPhase::SettleChannelList:
        if (!stress_chat_settled()) return;
        log_lv_pool_state(stress_chat_job.completed_cycles, "channel-before",
                          read_lv_pool_state());
        stress_chat_job.phase = StressChatPhase::OpenChannel;
        break;
    case StressChatPhase::OpenChannel:
        // show_channel_list() resets selection to channel 0 (Public). Use the
        // real chat input path so this exercises the production LVGL state.
        sigurdos::ui::chat_screen_handle_trackball(SigurdOSTrackballEvent::Click);
        stress_chat_job.phase = StressChatPhase::SettleChannel;
        break;
    case StressChatPhase::SettleChannel:
        if (!stress_chat_settled()) return;
        log_lv_pool_state(stress_chat_job.completed_cycles, "channel-after-open",
                          read_lv_pool_state());
        if (!sigurdos::ui::chat_screen_get_input_field()) {
            Serial.println("[stresschat] FAIL channel 0 did not open");
            stress_chat_job = {};
            return;
        }
        stress_chat_job.phase = StressChatPhase::CloseChannel;
        break;
    case StressChatPhase::CloseChannel:
        sigurdos::ui::go_back();
        stress_chat_job.phase = StressChatPhase::SettleChannelHome;
        break;
    case StressChatPhase::SettleChannelHome: {
        if (!stress_chat_settled()) return;
        const LvPoolState current = read_lv_pool_state();
        log_lv_pool_state(stress_chat_job.completed_cycles, "channel-after-close", current);
        log_lv_pool_degradation(stress_chat_job.completed_cycles, "baseline",
                                stress_chat_job.baseline, current);
        stress_chat_job.previous_home = current;
        if (stress_chat_job.completed_cycles >= stress_chat_job.requested_cycles) {
            finish_stress_chat();
        } else {
            stress_chat_job.phase = StressChatPhase::NavigateChat;
        }
        break;
    }
    case StressChatPhase::Idle:
        break;
    }
}

// ── Command parsing ──────────────────────────────────────
static bool dispatch(const char* line) {
    SigurdOSTestCommandLine parsed{};
    const SigurdOSTestCommandParseResult parse_result =
        sigurdos_test_controller_parse_command(line, &parsed);
    if (parse_result == SigurdOSTestCommandParseResult::Empty) return false;
    if (parse_result != SigurdOSTestCommandParseResult::Ok) {
        Serial.printf("[test] unknown or malformed command (try 'help')\n");
        return false;
    }
    const char* cmd = parsed.command;
    char* arg = parsed.arguments[0] ? parsed.arguments : nullptr;

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
    } else if (strcmp(cmd, "picker") == 0) {
        if (!arg) { Serial.println("[test] picker: missing base character"); return true; }
        cmd_picker(arg);
    } else if (strcmp(cmd, "press") == 0) {
        if (!arg) { Serial.println("[test] press: missing key name"); return true; }
        cmd_press(arg);
    } else if (strcmp(cmd, "inject") == 0 || strcmp(cmd, "msg") == 0) {
        if (!arg) { Serial.println("[test] inject: missing args"); return true; }
        cmd_inject(arg);
    } else if (strcmp(cmd, "sendchannel") == 0) {
        if (!arg) { Serial.println("[test] sendchannel: missing args — use: sendchannel <channel_name> <text>"); return true; }
        cmd_sendchannel(arg);
    } else if (strcmp(cmd, "sendmessage") == 0 || strcmp(cmd, "senddm") == 0) {
        if (!arg) { Serial.println("[test] sendmessage: missing args — use: sendmessage <contact_name> <text>"); return true; }
        cmd_sendmessage(arg);
#if !defined(SIGURDOS_REMOTE_TEST_RADIO) || !SIGURDOS_REMOTE_TEST_RADIO
    } else if (strcmp(cmd, "simulateack") == 0) {
        cmd_simulateack(arg);
#endif
    } else if (strcmp(cmd, "opendm") == 0) {
        if (!arg) { Serial.println("[test] opendm: missing contact name"); return true; }
        cmd_opendm(arg);
    } else if (strcmp(cmd, "addchannel") == 0 || strcmp(cmd, "addchan") == 0) {
        if (!arg) { Serial.println("[test] addchannel: missing args"); return true; }
        cmd_addchannel(arg);
    } else if (strcmp(cmd, "removechannel") == 0 || strcmp(cmd, "rmchannel") == 0 ||
               strcmp(cmd, "removechan") == 0 || strcmp(cmd, "rmchan") == 0) {
        if (!arg) { Serial.println("[test] removechannel: missing args"); return true; }
        cmd_removechannel(arg);
    } else if (strcmp(cmd, "addrepeater") == 0) {
        if (!arg) { Serial.println("[test] addrepeater: missing name"); return true; }
        bool ok = sigurdos::mesh::addTestRepeater(arg);
        Serial.printf("[test] addrepeater %s: %s\n", arg, ok ? "OK" : "FAILED");
    } else if (strcmp(cmd, "addroomserver") == 0) {
        if (!arg) { Serial.println("[test] addroomserver: missing name"); return true; }
        bool ok = sigurdos::mesh::addTestRoomServer(arg);
        Serial.printf("[test] addroomserver %s: %s\n", arg, ok ? "OK" : "FAILED");

    } else if (strcmp(cmd, "login") == 0) {
        if (!arg) { Serial.println("[test] login: missing args — use: login <contact> <password>"); return true; }
        char name[64];
        const char* pw_start;
        if (arg[0] == '"') {
            // Quoted name: "Heltec Room" rest
            const char* endq = strchr(arg + 1, '"');
            if (!endq) { Serial.println("[test] login: mismatched quote"); return true; }
            size_t nlen = endq - (arg + 1);
            if (nlen > 63) nlen = 63;
            memcpy(name, arg + 1, nlen);
            name[nlen] = 0;
            pw_start = endq + 1;
        } else {
            // Unquoted name: first token
            const char* sp = strchr(arg, ' ');
            if (!sp) { Serial.println("[test] login: need name and password"); return true; }
            size_t nlen = sp - arg;
            if (nlen > 63) nlen = 63;
            memcpy(name, arg, nlen);
            name[nlen] = 0;
            pw_start = sp + 1;
        }
        while (*pw_start == ' ') pw_start++;
        if (!pw_start[0]) { Serial.println("[test] login: need name and password"); return true; }
        bool ok = sigurdos::mesh::sendLogin(name, pw_start);
        Serial.printf("[test] login %s: %s\n", name, ok ? "OK" : "FAILED");

    } else if (strcmp(cmd, "setlogin") == 0 || strcmp(cmd, "setloginguest") == 0) {
        if (!arg) { Serial.println("[test] setlogin: usage: setlogin <name> [perm] or setloginguest <name>"); return true; }
        uint8_t perm = 1; // default: admin
        if (strcmp(cmd, "setloginguest") == 0) {
            perm = 2; // guest
        } else {
            // Check for optional permission parameter (space-separated after name)
            if (arg[0] != '"') {
                const char* sp = strchr(arg, ' ');
                if (sp) {
                    perm = (uint8_t)atoi(sp + 1);
                }
            }
        }
        // Parse name (handle quoted names)
        char name[64];
        if (arg[0] == '"') {
            const char* endq = strchr(arg + 1, '"');
            if (endq) {
                size_t nlen = endq - (arg + 1);
                if (nlen > 63) nlen = 63;
                memcpy(name, arg + 1, nlen);
                name[nlen] = 0;
                // For quoted names, check for perm after the closing quote
                if (strcmp(cmd, "setlogin") == 0) {
                    const char* sp = strchr(endq, ' ');
                    if (sp) {
                        perm = (uint8_t)atoi(sp + 1);
                    }
                }
            } else {
                strncpy(name, arg, sizeof(name) - 1);
                name[sizeof(name) - 1] = 0;
            }
        } else {
            const char* sp2 = strchr(arg, ' ');
            if (sp2) {
                size_t nlen = sp2 - arg;
                if (nlen > 63) nlen = 63;
                memcpy(name, arg, nlen);
                name[nlen] = 0;
            } else {
                strncpy(name, arg, sizeof(name) - 1);
                name[sizeof(name) - 1] = 0;
            }
        }
        sigurdos::mesh::forceLoginState(name, 2, perm);
        Serial.printf("[test] setlogin %s: OK (perm=%d)\n", name, perm);
    } else if (strcmp(cmd, "screen") == 0) {
        cmd_screen();
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status();
    } else if (strcmp(cmd, "stresschat") == 0) {
        cmd_stresschat(arg);
    } else if (strcmp(cmd, "keydiag") == 0 || strcmp(cmd, "kbddiag") == 0) {
        cmd_keydiag();
    } else if (strcmp(cmd, "inputdiag") == 0 || strcmp(cmd, "touchdiag") == 0 ||
               strcmp(cmd, "trackballdiag") == 0 || strcmp(cmd, "tbdiag") == 0) {
        cmd_inputdiag();
    } else if (strcmp(cmd, "gpsdiag") == 0) {
        cmd_gpsdiag();
    } else if (strcmp(cmd, "contactstats") == 0) {
        cmd_contactstats();
    } else if (strcmp(cmd, "ble") == 0) {
        // Companion BLE control for autonomous official-app protocol testing.
        // Usage:
        //   ble status  -> available/enabled/connected/pin/name
        //   ble on|off  -> enable/disable advertising
        //   ble pin     -> print 6-digit pairing PIN only
        const char* sub = arg ? arg : "status";
        while (*sub == ' ') ++sub;
        if (strcmp(sub, "pin") == 0) {
            Serial.printf("[ble] pin=%06lu\n",
                          (unsigned long)sigurdos::mesh::companionBlePin());
        } else if (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0) {
            const bool want = (strcmp(sub, "on") == 0);
            if (!sigurdos::mesh::companionBleAvailable()) {
                Serial.println("[ble] unavailable (firmware built without SIGURDOS_COMPANION_BLE)");
            } else {
                const bool ok = sigurdos::mesh::companionBleSetEnabled(want);
                Serial.printf("[ble] set enabled=%u -> %s (now=%u connected=%u pin=%06lu)\n",
                              want ? 1u : 0u,
                              ok ? "OK" : "FAIL",
                              sigurdos::mesh::companionBleEnabled() ? 1u : 0u,
                              sigurdos::mesh::companionBleConnected() ? 1u : 0u,
                              (unsigned long)sigurdos::mesh::companionBlePin());
            }
        } else {
            // status (default)
            const char* name = sigurdos::mesh::getOwnName();
            Serial.printf(
                "[ble] available=%u enabled=%u connected=%u pin=%06lu name=MeshCore-%s last_sync=%lu\n",
                sigurdos::mesh::companionBleAvailable() ? 1u : 0u,
                sigurdos::mesh::companionBleEnabled() ? 1u : 0u,
                sigurdos::mesh::companionBleConnected() ? 1u : 0u,
                (unsigned long)sigurdos::mesh::companionBlePin(),
                (name && name[0]) ? name : "?",
                (unsigned long)sigurdos::mesh::companionBleLastSyncTime());
        }
    } else if (strcmp(cmd, "debug") == 0) {
        cmd_debug(arg);
    } else if (strcmp(cmd, "emoji") == 0) {
        cmd_emoji();
    } else if (strcmp(cmd, "emoji-ac") == 0) {
        cmd_emoji_ac(arg);
    } else if (strcmp(cmd, "capture") == 0) {
        cmd_capture(arg);
    } else if (strcmp(cmd, "acmd") == 0) {
        if (!arg) { Serial.println("[test] acmd: missing name"); return true; }
        Serial.printf("[test] acmd -> %s\n", arg);
        sigurdos::ui::admin_cmd_show(arg);
    } else if (strcmp(cmd, "loginstat") == 0) {
        if (!arg) { Serial.println("[test] loginstat: missing name"); return true; }
        char name[64];
        if (arg[0] == '"') {
            const char* endq = strchr(arg + 1, '"');
            if (endq) {
                size_t nlen = endq - (arg + 1);
                if (nlen > 63) nlen = 63;
                memcpy(name, arg + 1, nlen);
                name[nlen] = 0;
            } else {
                strncpy(name, arg, sizeof(name) - 1);
                name[sizeof(name) - 1] = 0;
            }
        } else {
            strncpy(name, arg, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
        }
        uint8_t st = sigurdos::mesh::getLoginStatus(name);
        uint8_t perm = sigurdos::mesh::getLoginPermission(name);
        Serial.printf("[test] loginstat %s: status=%d perm=%d\n", name, (int)st, (int)perm);
    } else if (strcmp(cmd, "tree") == 0) {
        cmd_tree(arg);
    } else if (strcmp(cmd, "widgets") == 0) {
        cmd_widgets();
    } else if (strcmp(cmd, "telemetry") == 0) {
        sigurdos::telemetry::cmd_telemetry(arg);
    } else if (strcmp(cmd, "query") == 0) {
        sigurdos::telemetry::cmd_query(arg);
    } else if (strcmp(cmd, "crash") == 0) {
        sigurdos::telemetry::cmd_crash(arg ? arg : "report");
    } else if (strcmp(cmd, "drift") == 0) {
        sigurdos::telemetry::cmd_drift(arg);
    } else if (strcmp(cmd, "scrolllist") == 0) {
        if (!arg) { Serial.println("[test] scrolllist: missing value (use positive = down, negative = up)"); return true; }
        cmd_scrolllist(arg);
    } else if (strcmp(cmd, "tap") == 0) {
        if (!arg) { Serial.println("[test] tap: missing args"); return true; }
        cmd_tap(arg);
    } else if (strcmp(cmd, "backlight") == 0) {
        cmd_backlight(arg);
    } else if (strcmp(cmd, "fetchmsgs") == 0) {
        if (!arg) { Serial.println("[test] fetchmsgs: usage: fetchmsgs <contact> <channel>"); return true; }
        // Parse: fetchmsgs <contact> <channel>
        char name[64], channel[32];
        const char* p = arg;
        if (*p == '"') {
            const char* end = strchr(p + 1, '"');
            if (!end) { Serial.println("[test] fetchmsgs: mismatched quote"); return true; }
            int nlen = end - (p + 1);
            if (nlen > 63) nlen = 63;
            strncpy(name, p + 1, nlen); name[nlen] = '\0';
            p = end + 1;
            while (*p == ' ') p++;
        } else {
            if (sscanf(p, "%63s", name) < 1) { Serial.println("[test] fetchmsgs: need contact name"); return true; }
            int nlen = strlen(name);
            p += nlen;
            while (*p == ' ') p++;
        }
        strncpy(channel, p[0] ? p : "0", sizeof(channel) - 1);
        channel[sizeof(channel) - 1] = '\0';
        bool ok = sigurdos::mesh::sendRoomMsgFetchRequest(name, channel);
        Serial.printf("[test] fetchmsgs %s channel=%s: %s\n", name, channel, ok ? "OK" : "FAILED");
    } else if (strcmp(cmd, "buildinfo") == 0) {
        cmd_buildinfo();
    } else if (strcmp(cmd, "getrf") == 0) {
        cmd_getrf();
    } else if (strcmp(cmd, "setrf") == 0) {
        cmd_setrf(arg);
    } else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "restart") == 0) {
        cmd_reboot();
    } else if (strcmp(cmd, "factoryreset") == 0 || strcmp(cmd, "wipe") == 0) {
        cmd_factoryreset();
    } else if (strcmp(cmd, "advert") == 0) {
        cmd_advert();
    } else {
        Serial.printf("[test] unknown command: %s (try 'help')\n", cmd);
        return false;
    }
    return true;
}

// ── Public API ───────────────────────────────────────────

bool sigurdos_test_controller_exec(const char* cmd) {
    return dispatch(cmd);
}

void sigurdos_test_controller_init() {
    reset_capture_job();
    reset_tree_job();
    clear_emoji_grid_state();
    reset_stress_metrics();
    stress_chat_job = {};
    initialized = true;
    cmd_pos = 0;
    cmd_buf[0] = '\0';
    last_poll_ms = millis();

    Serial.println();
    Serial.println(F("╔══════════════════════════════════════╗"));
    Serial.println(F("║  SigurdOS Remote Test Controller      ║"));
    Serial.println(F("║  Type 'help' for available commands ║"));
    Serial.println(F("╚══════════════════════════════════════╝"));
    Serial.println();
}

void sigurdos_test_controller_loop() {
    if (!initialized) return;

    uint32_t now = millis();
    sample_stress_metrics();
    service_stress_chat();

    // Long diagnostics own their current output line until it is fully sent,
    // preventing command echoes from splitting the capture/tree protocol.
    if (diagnostic_line_pending() && !drain_diagnostic_line(now)) {
        service_capture(now);
        service_tree(now);
        return;
    }

    // Drain type queue: inject one codepoint per loop iteration
    if (type_count > 0 && (now - type_last_inject_ms >= TYPE_CHUNK_DELAY)) {
        sigurdos_keyboard_inject_codepoint(type_buf[type_pos]);
        type_pos++;
        type_count--;
        type_last_inject_ms = now;
        if (type_count == 0) {
            Serial.printf("[test] type done: %d codepoints injected, verifying...\n", (int)type_pos);
            delay(50);
            dump_focused_widget();
        }
    }

    if (now - last_poll_ms >= CMD_POLL_MS) {
        last_poll_ms = now;

        // Read characters from Serial.  Cancellation remains available between
        // every bounded output line while mesh/UI work continues in main loop.
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

    for (uint8_t i = 0; i < DIAGNOSTIC_LINES_PER_LOOP; ++i) {
        service_capture(now);
        service_tree(now);
        if (diagnostic_line_pending() && !drain_diagnostic_line(now)) break;
        if (capture_job.phase == CapturePhase::Idle && !tree_job.active) break;
    }
}
