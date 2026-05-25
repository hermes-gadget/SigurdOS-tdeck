// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SlopOS-TDeck.
//
// SlopOS-TDeck is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SlopOS-TDeck is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SlopOS-TDeck.  If not, see <https://www.gnu.org/licenses/>.


#include "display.h"
#include "touch.h"
#include "keyboard.h"
#include "prefs.h"
#include "trackball.h"
#include "tdeck_pins.h"
#include "../ui/ui.h"
#include "../ui/chat_screen.h"
#include <lvgl.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ── LovyanGFX configuration for T-Deck ST7789 ────────────
class LGFX_SlopOS : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789  _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;

public:
    LGFX_SlopOS()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host   = SPI3_HOST;
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

static LGFX_SlopOS tft;
static lv_display_t* lv_disp = nullptr;
// Full-screen PSRAM buffer: 320×240×2 = 153,600 bytes.
// Full rendering mode flushes the entire frame in one go,
// which eliminates the multiple tear lines caused by partial flushes.
static lv_color_t* draw_buf = nullptr;

// ── Debug: expose last flush area for diagnostics ────────
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
static lv_area_t dbg_last_flush_area = {0,0,0,0};
static uint32_t  dbg_flush_count = 0;
const lv_area_t* slopos_debug_last_flush_area() { return &dbg_last_flush_area; }
uint32_t slopos_debug_flush_count() { return dbg_flush_count; }
#endif

#include "../diagnostics/debug.h"

// ── Auto-off timer ──────────────────────────────────
// Based on MeshCore's AUTO_OFF_MILLIS pattern (MIT license)
static constexpr uint32_t AUTO_OFF_MS  = 30000;  // 30 seconds
static uint32_t            auto_off_at = 0;
static bool                display_on  = true;
static bool                wake_refresh_pending = false;
static constexpr uint8_t TRACKBALL_FALLBACK_QUEUE_SIZE = 8;
static SlopOSTrackballEvent trackball_fallback_queue[TRACKBALL_FALLBACK_QUEUE_SIZE];
static uint8_t trackball_fallback_head = 0;
static uint8_t trackball_fallback_tail = 0;
static uint8_t trackball_fallback_count = 0;

static void reset_auto_off() {
    auto_off_at = millis() + AUTO_OFF_MS;
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

static void queue_trackball_fallback(SlopOSTrackballEvent event)
{
    if (trackball_fallback_count >= TRACKBALL_FALLBACK_QUEUE_SIZE) return;
    trackball_fallback_queue[trackball_fallback_head] = event;
    trackball_fallback_head = (uint8_t)((trackball_fallback_head + 1) % TRACKBALL_FALLBACK_QUEUE_SIZE);
    trackball_fallback_count++;
}

static bool next_trackball_fallback(SlopOSTrackballEvent* out)
{
    if (!out || trackball_fallback_count == 0) return false;
    *out = trackball_fallback_queue[trackball_fallback_tail];
    trackball_fallback_tail = (uint8_t)((trackball_fallback_tail + 1) % TRACKBALL_FALLBACK_QUEUE_SIZE);
    trackball_fallback_count--;
    return true;
}

static void dispatch_trackball_events()
{
    SlopOSTrackballEvent event = SlopOSTrackballEvent::None;
    while (slopos_trackball_next_event(&event)) {
        slopos_display_wake();
        if (!slopos::ui::handle_trackball_event(event)) {
            queue_trackball_fallback(event);
        }
    }
}

// ── LVGL flush callback ─────────────────────────────────
static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
    dbg_last_flush_area = *area;
    dbg_flush_count++;
    if (slopos::debug::get_level() >= 2) {
        Serial.printf("[flush] #%lu  area=(%ld,%ld,%ld,%ld) w=%ld h=%ld pixels=%ld\n",
                      (unsigned long)dbg_flush_count,
                      (long)area->x1, (long)area->y1, (long)area->x2, (long)area->y2,
                      (long)(area->x2 - area->x1 + 1), (long)(area->y2 - area->y1 + 1),
                      (long)((area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1)));
    }
#endif
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    static constexpr uint32_t BYTES_PER_PX = 2;  // LV_COLOR_DEPTH=16, RGB565
    uint32_t row_bytes = w * BYTES_PER_PX;
    uint32_t stride = ((row_bytes + LV_DRAW_BUF_STRIDE_ALIGN - 1) /
                       LV_DRAW_BUF_STRIDE_ALIGN) * LV_DRAW_BUF_STRIDE_ALIGN;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    if (stride == row_bytes) {
        tft.writePixels((lgfx::rgb565_t*)px_map, w * h);
    } else {
        uint8_t* line = px_map;
        for (uint32_t i = 0; i < h; i++) {
            tft.writePixels((lgfx::rgb565_t*)line, w);
            line += stride;
        }
    }
    tft.endWrite();
    lv_display_flush_ready(disp);
}

// ── Touch read callback ──────────────────────────────────
static void lvgl_touch_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    int x, y;
    bool pressed = false;
    if (slopos_touch_get(&x, &y, &pressed) && pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        slopos_display_wake();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ── Keyboard read callback ───────────────────────────────
static void lvgl_kb_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    slopos_keyboard_scan();   // force a fresh poll (catches first key after focus)
    int key = slopos_keyboard_get_key();
    if (key > 0 && slopos_keyboard_has_new_event()) {
        // Always route keyboard input to the chat textarea when the chat
        // messaging view is active, so the text box stays ready to type in.
        lv_obj_t* chat_input = slopos::ui::chat_screen_get_input_field();
        if (chat_input) {
            lv_group_t* g = lv_group_get_default();
            if (g) lv_group_focus_obj(chat_input);
        }

        if (key == 0x08) data->key = LV_KEY_BACKSPACE;
        else if (key == 0x0D) data->key = LV_KEY_ENTER;
        else if (key == 0x09) data->key = LV_KEY_NEXT;
        else data->key = (uint32_t)key;
        data->state = LV_INDEV_STATE_PRESSED;
        slopos_display_wake();

        // Single-shot: clear the key immediately so the next LVGL read gets RELEASED.
        // This prevents the last character from repeating forever.
        slopos_keyboard_consume_key();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ── Trackball read callback ─────────────────────────────
static void lvgl_trackball_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_RELEASED;

    SlopOSTrackballEvent event = SlopOSTrackballEvent::None;
    if (!next_trackball_fallback(&event)) return;

    switch (event) {
    case SlopOSTrackballEvent::Up:
    case SlopOSTrackballEvent::Left:
        data->enc_diff = -1;
        break;
    case SlopOSTrackballEvent::Down:
    case SlopOSTrackballEvent::Right:
        data->enc_diff = 1;
        break;
    case SlopOSTrackballEvent::Click:
        data->state = LV_INDEV_STATE_PRESSED;
        break;
    case SlopOSTrackballEvent::None:
    default:
        break;
    }
}

// ── Public API ───────────────────────────────────────────
#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
static void lvgl_invalidate_cb(lv_event_t* e)
{
    if (slopos::debug::get_level() < 2) return;
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    if (area) {
        Serial.printf("[inv] area=(%ld,%ld,%ld,%ld) w=%ld h=%ld\n",
                      (long)area->x1, (long)area->y1, (long)area->x2, (long)area->y2,
                      (long)(area->x2 - area->x1 + 1), (long)(area->y2 - area->y1 + 1));
    }
}
#endif

bool slopos_display_init()
{
    tft.init();
    tft.setRotation(1);  // 90° CW: native portrait (240×320) → landscape (320×240)
    tft.setBrightness(255);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb(slopos_display_millis);
    lv_disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    lv_display_set_flush_cb(lv_disp, lvgl_flush_cb);

    draw_buf = (lv_color_t*)heap_caps_malloc(
        TFT_WIDTH * TFT_HEIGHT * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM);
    if (draw_buf) {
        lv_display_set_buffers(lv_disp, draw_buf, nullptr,
                               TFT_WIDTH * TFT_HEIGHT * sizeof(lv_color_t),
                               LV_DISPLAY_RENDER_MODE_FULL);
    } else {
        // Fallback: partial mode with a smaller DRAM buffer
        static lv_color_t fallback_buf[TFT_WIDTH * 80];
        lv_display_set_buffers(lv_disp, fallback_buf, nullptr,
                               sizeof(fallback_buf),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
    }

#if defined(SLOPOS_DEBUG) && SLOPOS_DEBUG
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
    if (!slopos_touch_init()) {
        // Touch init failed — device works with keyboard only
    }

    // Initialize keyboard matrix scanner
    if (!slopos_keyboard_init()) {
        // Keyboard init failed — device works with touch only
    }

    // Initialize trackball GPIO input
    slopos_trackball_init();

    display_on = true;
    reset_auto_off();

    // Backlight pulse: brief off→on to confirm display is alive
    tft.setBrightness(0);
    delay(50);
    tft.setBrightness(255);

    return true;
}

void slopos_display_loop()
{
    slopos_touch_loop();
    slopos_keyboard_scan();
    slopos_trackball_scan();
    dispatch_trackball_events();

    if (wake_refresh_pending) {
        wake_refresh_pending = false;
        restore_display_after_sleep();
    }

    // Auto-off: turn off backlight after inactivity (disabled in debug builds)
#if !defined(SLOPOS_DEBUG) || !SLOPOS_DEBUG
    if (display_on && millis() > auto_off_at) {
        tft.setBrightness(0);
        slopos_keyboard_set_brightness(0);
        display_on = false;
    }
#endif

    uint32_t next = lv_timer_handler();
    delay(next > 5 ? 5 : next);
}

uint32_t slopos_display_millis()
{
    return millis();
}

void slopos_display_wake()
{
    if (!display_on) {
        tft.setBrightness(255);
        slopos_keyboard_set_brightness(slopos::prefs_get().kbd_backlight);
        display_on = true;
        wake_refresh_pending = true;
    }
    reset_auto_off();
}

bool slopos_display_is_on()
{
    return display_on;
}
