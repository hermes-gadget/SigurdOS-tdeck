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


#include "display.h"
#include "touch.h"
#include "keyboard.h"
#include "prefs.h"
#include "trackball.h"
#include "tdeck_pins.h"
#include "../ui/ui.h"
#include "../ui/chat_screen.h"
#include "../ui/navigation.h"
#include "../mesh/mesh_wrapper.h"
#include "../diagnostics/debug_cfg.h"
#include "../diagnostics/debug.h"
#if SIGURDOS_TELEMETRY
#include "../diagnostics/telemetry.h"
#endif
#include <lvgl.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ── LovyanGFX configuration for T-Deck ST7789 ────────────
class LGFX_SigurdOS : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789  _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;

public:
    LGFX_SigurdOS()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk   = PIN_TFT_SCL;
            cfg.pin_mosi   = PIN_TFT_SDA;
            cfg.pin_miso   = -1;
            cfg.pin_dc     = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs    = PIN_TFT_CS;
            cfg.pin_rst   = PIN_TFT_RST;
            cfg.panel_width  = 240;   // ST7789 native portrait
            cfg.panel_height = 320;
            cfg.offset_x  = 0;
            cfg.offset_y  = 0;
            cfg.offset_rotation = 0;
            cfg.invert    = true;
            cfg.rgb_order = false;
            cfg.memory_width  = 240;  // must match native portrait
            cfg.memory_height = 320;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = PIN_TFT_BL;
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 0;
            _light.config(cfg);
        }
        _panel.setLight(&_light);
        setPanel(&_panel);
    }
};

static LGFX_SigurdOS tft;
static lv_display_t* lv_disp = nullptr;
// Full-screen PSRAM buffer: 320×240×2 = 153,600 bytes.
// Full rendering mode flushes the entire frame in one go,
// which eliminates the multiple tear lines caused by partial flushes.
static uint8_t* draw_buf = nullptr;

// ── Debug: expose last flush area for diagnostics ────────
#if SIGURDOS_DEBUG_DISPLAY
static lv_area_t dbg_last_flush_area = {0,0,0,0};
static uint32_t  dbg_flush_count = 0;
const lv_area_t* sigurdos_debug_last_flush_area() { return &dbg_last_flush_area; }
uint32_t sigurdos_debug_flush_count() { return dbg_flush_count; }
#endif

// ── Auto-off timer ──────────────────────────────────
// Based on MeshCore's AUTO_OFF_MILLIS pattern (MIT license)
static uint32_t            auto_off_at = 0;
static bool                display_on  = true;
static bool                wake_refresh_pending = false;
static constexpr uint8_t TRACKBALL_FALLBACK_QUEUE_SIZE = 8;
static SigurdOSTrackballEvent trackball_fallback_queue[TRACKBALL_FALLBACK_QUEUE_SIZE];
static uint8_t trackball_fallback_head = 0;
static uint8_t trackball_fallback_tail = 0;
static uint8_t trackball_fallback_count = 0;

static void reset_auto_off() {
    uint16_t sec = sigurdos::prefs_get().auto_off_timeout;
    auto_off_at = (sec > 0) ? (millis() + (uint32_t)sec * 1000) : UINT32_MAX;
}

static void restore_display_after_sleep()
{
    tft.setRotation(1);  // Reassert ST7789 landscape state after display auto-off.
    tft.fillScreen(TFT_BLACK);

    lv_obj_t* active = lv_scr_act();
    if (active) {
        lv_obj_invalidate(active);
    }
}

static void queue_trackball_fallback(SigurdOSTrackballEvent event)
{
    if (trackball_fallback_count >= TRACKBALL_FALLBACK_QUEUE_SIZE) return;
    trackball_fallback_queue[trackball_fallback_head] = event;
    trackball_fallback_head = (uint8_t)((trackball_fallback_head + 1) % TRACKBALL_FALLBACK_QUEUE_SIZE);
    trackball_fallback_count++;
}

static bool next_trackball_fallback(SigurdOSTrackballEvent* out)
{
    if (!out || trackball_fallback_count == 0) return false;
    *out = trackball_fallback_queue[trackball_fallback_tail];
    trackball_fallback_tail = (uint8_t)((trackball_fallback_tail + 1) % TRACKBALL_FALLBACK_QUEUE_SIZE);
    trackball_fallback_count--;
    return true;
}

static void dispatch_trackball_events()
{
    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    while (sigurdos_trackball_next_event(&event)) {
        sigurdos_display_wake();
        if (!sigurdos::ui::handle_trackball_event(event)) {
            queue_trackball_fallback(event);
        }
    }
}

// ── LVGL flush callback ─────────────────────────────────
static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_render_flush();
#endif
#if SIGURDOS_DEBUG_DISPLAY
    dbg_last_flush_area = *area;
    dbg_flush_count++;
    // Runtime level + feature check only when full debug module is compiled
#if defined(SIGURDOS_DEBUG) && SIGURDOS_DEBUG
    if (sigurdos::debug::get_level() >= 2 && sigurdos::debug::feat_get_display())
#endif
    {
        Serial.printf("[flush] #%lu  area=(%ld,%ld,%ld,%ld) w=%ld h=%ld pixels=%ld\n",
                      (unsigned long)dbg_flush_count,
                      (long)area->x1, (long)area->y1, (long)area->x2, (long)area->y2,
                      (long)(area->x2 - area->x1 + 1), (long)(area->y2 - area->y1 + 1),
                      (long)((area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1)));
    }
#endif
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    const lv_color_format_t cf = lv_display_get_color_format(disp);
    const uint32_t bpp = lv_color_format_get_size(cf);
    uint32_t row_bytes = w * bpp;
    uint32_t stride = lv_draw_buf_width_to_stride(w, cf);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    if (cf == LV_COLOR_FORMAT_RGB565 && stride == row_bytes) {
        tft.writePixels((lgfx::rgb565_t*)px_map, w * h);
    } else if (cf == LV_COLOR_FORMAT_RGB565) {
        uint8_t* line = px_map;
        for (uint32_t i = 0; i < h; i++) {
            tft.writePixels((lgfx::rgb565_t*)line, w);
            line += stride;
        }
    } else {
        // Safety fallback if LVGL is built with a non-RGB565 native format:
        // convert each row to RGB565 before writing to the ST7789.
        static lgfx::rgb565_t line565[TFT_WIDTH];
        uint8_t* line = px_map;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint8_t r = 0, g = 0, b = 0;
                const uint8_t* px = line + x * bpp;
                if (cf == LV_COLOR_FORMAT_RGB888 && bpp >= 3) {
                    r = px[0]; g = px[1]; b = px[2];
                } else if (cf == LV_COLOR_FORMAT_XRGB8888 && bpp >= 4) {
                    r = px[1]; g = px[2]; b = px[3];
                } else {
                    r = g = b = px[0];
                }
                line565[x] = lgfx::color565(r, g, b);
            }
            tft.writePixels(line565, w);
            line += stride;
        }
    }
    tft.endWrite();
    lv_display_flush_ready(disp);
}

// ── Touch read callback ──────────────────────────────────

#if defined(SIGURDOS_REMOTE_TEST)
// Test touch injection for remote test controller
static lv_point_t test_touch_point = {0, 0};
static bool test_touch_pressed = false;
static uint32_t test_release_tick = 0;
#endif

static void lvgl_touch_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
#if defined(SIGURDOS_REMOTE_TEST)
    if (test_touch_pressed) {
        data->point = test_touch_point;
        data->state = LV_INDEV_STATE_PRESSED;
        test_touch_pressed = false;
        test_release_tick = lv_tick_get() + 80;  // release after 80ms
        sigurdos_display_wake();
        return;
    }
    if (test_release_tick && lv_tick_get() >= test_release_tick) {
        data->point = test_touch_point;
        data->state = LV_INDEV_STATE_RELEASED;
        test_release_tick = 0;
        return;
    }
#endif
    int x, y;
    bool pressed = false;
    if (sigurdos_touch_get(&x, &y, &pressed) && pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        sigurdos_display_wake();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ── Keyboard read callback ───────────────────────────────
static void lvgl_kb_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    sigurdos_keyboard_scan();   // force a fresh poll (catches first key after focus)
    int key = sigurdos_keyboard_get_key();
    if (key > 0 && sigurdos_keyboard_consume_event()) {
        // ── Global shortcut: Alt+C = channel quick-action menu ──
        // The T-Deck keyboard's ESP32-C3 swallows modifier keys internally
        // and only emits a finished byte for a few combos. Alt+C is the one
        // free, non-typing code it sends (0x0C / form feed) — Alt+Space and
        // the Mic key (NULL in the C3 keymap) produce nothing the host can
        // see. Opens per-chat private scope controls plus channel actions
        // when the chat messaging view is active.
        if (key == 0x0C) {
            lv_obj_t* ci = sigurdos::ui::chat_screen_get_input_field();
            if (ci && lv_obj_is_valid(ci) && lv_obj_get_screen(ci) == lv_scr_act()) {
                sigurdos::ui::chat_screen_show_channel_menu();
            }
            sigurdos_keyboard_consume_key();
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }

        // While a chat overlay (channel menu / scope picker) is open, deliver
        // keys straight to the focused overlay widget. Skipping the chat
        // refocus heuristics below lets the scope picker's custom-scope field
        // actually receive typing instead of the message box stealing it.
        if (sigurdos::ui::chat_screen_overlay_active()) {
            if (key == 0x08)      data->key = LV_KEY_BACKSPACE;
            else if (key == 0x0D) data->key = LV_KEY_ENTER;
            else if (key == 0x09) data->key = LV_KEY_NEXT;
            else                  data->key = (uint32_t)key;
            data->state = LV_INDEV_STATE_PRESSED;
            sigurdos_display_wake();
            sigurdos_keyboard_consume_key();
            return;
        }

        // Route keyboard input to the chat textarea only when the chat
        // screen is the active screen — never steal focus from other
        // textareas (WiFi password dialog, etc.).
        lv_obj_t* chat_input = sigurdos::ui::chat_screen_get_input_field();
        if (chat_input && lv_obj_is_valid(chat_input)) {
            lv_obj_t* chat_scr = lv_obj_get_screen(chat_input);
            lv_obj_t* act_scr = lv_scr_act();
            if (chat_scr == act_scr) {
                lv_group_t* g = lv_group_get_default();
                if (g) lv_group_focus_obj(chat_input);
            }
        }

        // For printable characters, ensure focus is on a textarea.
        // The trackball (ENCODER indev) can accidentally move group
        // focus to a button — if focus isn't a textarea, find one
        // on the active screen and refocus it so keystrokes land.
        if (key > 0x20 && key != 0x7F) {  // printable ASCII
            lv_group_t* g = lv_group_get_default();
            lv_obj_t* focused = g ? lv_group_get_focused(g) : nullptr;
            if (!focused || !lv_obj_check_type(focused, &lv_textarea_class)) {
                // Focus lost — walk the screen tree for any textarea
                lv_obj_t* act_scr = lv_scr_act();
                if (act_scr) {
                    uint32_t cnt = lv_obj_get_child_cnt(act_scr);
                    for (uint32_t i = 0; i < cnt; i++) {
                        lv_obj_t* c = lv_obj_get_child(act_scr, i);
                        // Check direct child
                        if (lv_obj_check_type(c, &lv_textarea_class)) {
                            lv_group_focus_obj(c);
                            break;
                        }
                        // Check grandchildren (textareas in dialogs)
                        uint32_t gc = lv_obj_get_child_cnt(c);
                        for (uint32_t j = 0; j < gc; j++) {
                            lv_obj_t* gc_obj = lv_obj_get_child(c, j);
                            if (lv_obj_check_type(gc_obj, &lv_textarea_class)) {
                                lv_group_focus_obj(gc_obj);
                                goto found;
                            }
                            // Check great-grandchildren
                            uint32_t ggc = lv_obj_get_child_cnt(gc_obj);
                            for (uint32_t k = 0; k < ggc; k++) {
                                lv_obj_t* ggc_obj = lv_obj_get_child(gc_obj, k);
                                if (lv_obj_check_type(ggc_obj, &lv_textarea_class)) {
                                    lv_group_focus_obj(ggc_obj);
                                    goto found;
                                }
                            }
                        }
                    }
                    found:;
                }
            }
        }

        if (key == 0x08) data->key = LV_KEY_BACKSPACE;
        else if (key == 0x0D) data->key = LV_KEY_ENTER;
        else if (key == 0x09) data->key = LV_KEY_NEXT;
        else data->key = (uint32_t)key;
        data->state = LV_INDEV_STATE_PRESSED;
        sigurdos_display_wake();

        // Single-shot: clear the key immediately so the next LVGL read gets RELEASED.
        // This prevents the last character from repeating forever.
        sigurdos_keyboard_consume_key();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ── Trackball read callback ─────────────────────────────
static void lvgl_trackball_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_RELEASED;

    SigurdOSTrackballEvent event = SigurdOSTrackballEvent::None;
    if (!next_trackball_fallback(&event)) return;

    switch (event) {
    case SigurdOSTrackballEvent::Up:
    case SigurdOSTrackballEvent::Left:
        data->enc_diff = -1;
        break;
    case SigurdOSTrackballEvent::Down:
    case SigurdOSTrackballEvent::Right:
        data->enc_diff = 1;
        break;
    case SigurdOSTrackballEvent::Click:
        data->state = LV_INDEV_STATE_PRESSED;
        break;
    case SigurdOSTrackballEvent::None:
    default:
        break;
    }
}

// ── Public API ───────────────────────────────────────────
#if SIGURDOS_DEBUG_DISPLAY
static void lvgl_invalidate_cb(lv_event_t* e)
{
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
#if defined(SIGURDOS_DEBUG) && SIGURDOS_DEBUG
    if (area && sigurdos::debug::get_level() >= 2 && sigurdos::debug::feat_get_display())
#else
    if (area)
#endif
    {
        Serial.printf("[inv] area=(%ld,%ld,%ld,%ld) w=%ld h=%ld\n",
                      (long)area->x1, (long)area->y1, (long)area->x2, (long)area->y2,
                      (long)(area->x2 - area->x1 + 1), (long)(area->y2 - area->y1 + 1));
    }
}
#endif

bool sigurdos_display_init()
{
    tft.init();
    tft.setRotation(1);  // 90° CW: native portrait (240×320) → landscape (320×240)
    tft.setBrightness(sigurdos::prefs_get().display_brightness);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb(sigurdos_display_millis);
    lv_disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    lv_display_set_color_format(lv_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(lv_disp, lvgl_flush_cb);

    const lv_color_format_t cf = lv_display_get_color_format(lv_disp);
    const uint32_t full_stride = lv_draw_buf_width_to_stride(TFT_WIDTH, cf);
    const uint32_t full_buf_bytes = full_stride * TFT_HEIGHT;

    draw_buf = (uint8_t*)heap_caps_malloc(full_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (draw_buf) {
        lv_display_set_buffers(lv_disp, draw_buf, nullptr,
                               full_buf_bytes,
                               LV_DISPLAY_RENDER_MODE_FULL);
    } else {
        // Fallback: partial mode with a smaller DRAM buffer
        // Allocate dynamically so the memory is only reserved when PSRAM fails,
        // not permanently in BSS/DRAM on every boot.
        const uint32_t partial_buf_bytes = full_stride * 80;
        uint8_t* fallback_buf = (uint8_t*)heap_caps_malloc(
            partial_buf_bytes,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (fallback_buf) {
            lv_display_set_buffers(lv_disp, fallback_buf, nullptr,
                                   partial_buf_bytes,
                                   LV_DISPLAY_RENDER_MODE_PARTIAL);
        } else {
            // Emergency fallback: tiny DRAM buffer — at least LVGL can render
            static uint8_t emergency_buf[TFT_WIDTH * 20 * 2];
            uint32_t emergency_bytes = full_stride * 20;
            if (emergency_bytes > sizeof(emergency_buf)) {
                emergency_bytes = sizeof(emergency_buf);
            }
            lv_display_set_buffers(lv_disp, emergency_buf, nullptr,
                                   emergency_bytes,
                                   LV_DISPLAY_RENDER_MODE_PARTIAL);
            Serial.printf("[disp] WARNING: using emergency draw buffer\n");
        }
    }

#if SIGURDOS_DEBUG_DISPLAY
    lv_display_add_event_cb(lv_disp, lvgl_invalidate_cb, LV_EVENT_INVALIDATE_AREA, nullptr);
    Serial.println("[debug] LVGL invalidate area tracking enabled");
#endif

    lv_indev_t* touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, lvgl_touch_cb);

    lv_indev_t* kb = lv_indev_create();
    lv_indev_set_type(kb, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kb, lvgl_kb_cb);
    lv_timer_set_period(lv_indev_get_read_timer(kb), 10);  // 10ms vs ~33ms default

    lv_indev_t* trackball = lv_indev_create();
    lv_indev_set_type(trackball, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(trackball, lvgl_trackball_cb);

    // Set default group so keyboard input reaches focused widgets
    lv_group_t* g = lv_group_create();
    lv_indev_set_group(kb, g);
    lv_indev_set_group(trackball, g);
    lv_group_set_default(g);

    // Initialize touch controller
    if (!sigurdos_touch_init()) {
        // Touch init failed — device works with keyboard only
    }

    // Initialize keyboard matrix scanner
    if (!sigurdos_keyboard_init()) {
        // Keyboard init failed — device works with touch only
    }

    // Initialize trackball GPIO input
    sigurdos_trackball_init();

    display_on = true;
    reset_auto_off();

    // Backlight pulse: brief off→on to confirm display is alive
    uint8_t saved_brightness = sigurdos::prefs_get().display_brightness;
    tft.setBrightness(0);
    delay(50);
    tft.setBrightness(255);
    // Restore saved brightness after the pulse
    sigurdos_display_set_brightness(saved_brightness);

    return true;
}

void sigurdos_display_loop()
{
    // Serial screenshot/nav commands are disabled unless explicitly enabled.
    // In SIGURDOS_REMOTE_TEST builds the test controller's serial handler
    // owns all Serial reads; this block would steal characters from commands.
#if SIGURDOS_SERIAL_DEBUG_COMMANDS_ACTIVE
    if (Serial.available()) {
        static char cmd_buf[64];
        static uint8_t cmd_pos = 0;
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_pos] = '\0';
            if (strcmp(cmd_buf, "SCREENSHOT") == 0) {
                sigurdos_display_capture_framebuffer();
            } else if (strncmp(cmd_buf, "SEND ", 5) == 0) {
                // SEND <channel_name> <text>
                // Serial-accessible channel message send for debugging.
                // Useful when the test controller isn't available (non-remote-test builds).
                const char* rest = cmd_buf + 5;
                const char* space = strchr(rest, ' ');
                if (space && space[1]) {
                    char ch_name[32];
                    size_t ch_len = (size_t)(space - rest);
                    if (ch_len > 31) ch_len = 31;
                    memcpy(ch_name, rest, ch_len);
                    ch_name[ch_len] = '\0';
                    const char* text = space + 1;
                    bool ok = sigurdos::mesh::sendChannelMessage(ch_name, text);
                    Serial.printf("[serial] SEND ch=%s text=%s -> %s\n",
                                  ch_name, text, ok ? "OK" : "FAILED");
                } else {
                    Serial.println("[serial] SEND syntax: SEND <channel> <text>");
                }
            } else if (strncmp(cmd_buf, "NAV ", 4) == 0) {
                // NAV <screen_name> — programmatic screen navigation
                // Screen names: home, chat, contacts, channels, network, heard,
                // map, advertise, settings, trace, terminal, signal, radio,
                // repeaters, onboarding
                const char* n = cmd_buf + 4;
                while (*n == ' ') ++n;
                struct { const char* name; sigurdos::ui::Screen scr; } tbl[] = {
                    {"home",      sigurdos::ui::Screen::Home},
                    {"chat",      sigurdos::ui::Screen::Chat},
                    {"contacts",  sigurdos::ui::Screen::Contacts},
                    {"channels",  sigurdos::ui::Screen::Channels},
                    {"network",   sigurdos::ui::Screen::Network},
                    {"heard",     sigurdos::ui::Screen::Heard},
                    {"map",       sigurdos::ui::Screen::Map},
                    {"advertise", sigurdos::ui::Screen::Advertise},
                    {"settings",  sigurdos::ui::Screen::Settings},
                    {"trace",     sigurdos::ui::Screen::Trace},
                    {"terminal",  sigurdos::ui::Screen::Terminal},
                    {"signal",    sigurdos::ui::Screen::Signal},
                    {"radio",     sigurdos::ui::Screen::RadioSetup},
                    {"repeaters", sigurdos::ui::Screen::Repeaters},
                    {"onboarding",sigurdos::ui::Screen::Onboarding},
                    {"s-radio",   sigurdos::ui::Screen::SettingsRadio},
                    {"s-gps",     sigurdos::ui::Screen::SettingsGPS},
                    {"s-display", sigurdos::ui::Screen::SettingsDisplay},
                    {"s-system",  sigurdos::ui::Screen::SettingsSystem},
                    {"packets",   sigurdos::ui::Screen::Network},
                    {"node-status", sigurdos::ui::Screen::NodeStatus},
                    {"telemetry",   sigurdos::ui::Screen::Telemetry},
                };
                bool found = false;
                for (auto& e : tbl) {
                    if (strcasecmp(n, e.name) == 0) {
                        sigurdos::ui::navigate_to(e.scr);
                        found = true;
                        break;
                    }
                }
                Serial.printf("[serial] NAV %s -> %s\n",
                              n, found ? "OK" : "unknown screen");
            }
            cmd_pos = 0;
        } else if (cmd_pos < sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_pos++] = c;
        }
    }
#endif

    sigurdos_touch_loop();
    sigurdos_keyboard_scan();
    sigurdos_trackball_scan();
    dispatch_trackball_events();

    if (wake_refresh_pending) {
        wake_refresh_pending = false;
        restore_display_after_sleep();
    }

    // Auto-off: turn off backlight after inactivity
    // Disabled in display debug builds — the screen must stay on for observation
#if !SIGURDOS_DEBUG_DISPLAY
    if (display_on && millis() > auto_off_at) {
        tft.setBrightness(0);
        sigurdos_keyboard_set_brightness(0);
        display_on = false;
#if SIGURDOS_TELEMETRY
        sigurdos::telemetry::report_display_sleep();
#endif
    }
#endif

    uint32_t next = lv_timer_handler();
    delay(next > 5 ? 5 : next);
}

uint32_t sigurdos_display_millis()
{
    return millis();
}

void sigurdos_display_wake()
{
    if (!display_on) {
        tft.setBrightness(sigurdos::prefs_get().display_brightness);
        sigurdos_keyboard_set_brightness(sigurdos::prefs_get().kbd_backlight);
        display_on = true;
        wake_refresh_pending = true;
#if SIGURDOS_TELEMETRY
        sigurdos::telemetry::report_display_wake();
#endif
    }
    reset_auto_off();
}

bool sigurdos_display_is_on()
{
    return display_on;
}

void sigurdos_display_set_brightness(uint8_t brightness)
{
    tft.setBrightness(brightness);
}

void sigurdos_display_reset_auto_off()
{
    reset_auto_off();
}

#if defined(SIGURDOS_REMOTE_TEST)
void sigurdos_test_set_touch(int x, int y)
{
    test_touch_point.x = x;
    test_touch_point.y = y;
    test_touch_pressed = true;
}
#endif

// ════════════════════════════════════════════════════════
// Screenshot capture (all builds)
// ════════════════════════════════════════════════════════
void* sigurdos_display_get_buffer()
{
    return draw_buf;
}

uint32_t sigurdos_display_get_width()
{
    return TFT_WIDTH;
}

uint32_t sigurdos_display_get_height()
{
    return TFT_HEIGHT;
}

void sigurdos_display_capture_framebuffer()
{
    // Force a full LVGL render into the buffer
    lv_refr_now(NULL);

    lv_display_t* disp = lv_display_get_default();
    if (!disp) {
        Serial.println("[capture] ERROR: no display");
        return;
    }

    uint32_t w = TFT_WIDTH;
    uint32_t h = TFT_HEIGHT;
    uint32_t stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
    uint32_t total_bytes = stride * h;

    // Capture a stable LVGL snapshot instead of dumping the live draw buffer.
    // The live full-screen PSRAM buffer can contain stale regions between
    // refreshes; snapshot renders the active object tree into a clean buffer.
    uint8_t* snap_buf = (uint8_t*)heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snap_buf) {
        snap_buf = (uint8_t*)heap_caps_malloc(total_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!snap_buf) {
        Serial.println("[capture] ERROR: no memory");
        return;
    }

    lv_draw_buf_t snap_db;
    lv_draw_buf_init(&snap_db, w, h, LV_COLOR_FORMAT_RGB565, stride, snap_buf, total_bytes);
    if (lv_snapshot_take_to_draw_buf(lv_scr_act(), LV_COLOR_FORMAT_RGB565, &snap_db) != LV_RES_OK) {
        Serial.println("[capture] ERROR: snapshot failed");
        heap_caps_free(snap_buf);
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
    }

    heap_caps_free(snap_buf);
    Serial.println("[capture] END");
}
