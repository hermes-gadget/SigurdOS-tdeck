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
#include "hal/trackball.h"
#include "hal/keyboard.h"
#include "mesh/mesh_wrapper.h"
#include "ui/navigation.h"
#include "diagnostics/debug.h"
#include "ui/screens.h"
#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#include <cctype>

// ── Constants ────────────────────────────────────────────
static constexpr uint32_t CMD_POLL_MS = 50;   // check Serial every 50ms
static constexpr size_t   CMD_BUF_SIZE = 256;
static constexpr size_t   TYPE_BUF_SIZE = 256;   // max chars in type queue
static constexpr size_t   TYPE_CHUNK_DELAY = 50; // ms between injected chars, must give LVGL time to consume

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
    Serial.println(F("║  screen      Show current screen     ║"));
    Serial.println(F("║  status      Show device state       ║"));
    Serial.println(F("║  debug <1|2|3> Set debug level       ║"));
    Serial.println(F("║  term-log    Dump terminal log       ║"));
    Serial.println(F("║  term-clear  Clear terminal log      ║"));
    Serial.println(F("║  term-submit <cmd>  Run cmd in terminal║"));
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
    if (!arg) {
        Serial.printf("[test] debug level: %u\n", (unsigned)slopos::debug::get_level());
        return;
    }
    // Skip leading whitespace
    while (*arg == ' ') arg++;
    char* end;
    long level = strtol(arg, &end, 10);
    // Allow trailing whitespace
    while (*end == ' ') end++;
    if (*end != '\0' || level < 1 || level > 3) {
        Serial.println("[test] debug: usage: debug <1|2|3>  (1=quiet, 2=normal, 3=verbose)");
        return;
    }
    slopos::debug::set_level((uint8_t)level);
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
            Serial.printf("[test] type done: %d chars\n", (int)type_pos);
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
