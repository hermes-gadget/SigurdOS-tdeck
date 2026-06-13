// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Telemetry engine — heartbeat tick, diff comparison, drift detection,
// agent query handlers.

#if SIGURDOS_TELEMETRY

#include "telemetry.h"
#include "telemetry_protocol.h"
#include "telemetry_crash.h"
#include "build_info.h"
#include "../hal/battery.h"
#include "../mesh/mesh_wrapper.h"
#include "../ui/navigation.h"  // for Screen enum and screen name mapping
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include "telemetry_collectors.h"
#include "telemetry_input.h"
#include "telemetry_hb_ring.h"

namespace sigurdos {
namespace telemetry {

using namespace sigurdos::telemetry;

// ── Configuration ─────────────────────────────────────
static uint32_t s_heartbeat_ms     = SIGURDOS_TELEMETRY_HEARTBEAT_MS;
static bool     s_enabled          = true;
static bool     s_diff_enabled     = SIGURDOS_TELEMETRY_DIFF ? true : false;
static uint8_t  s_full_sync_every  = 12;   // every N ticks, send full @hb
static uint32_t s_last_tick_ms     = 0;
static uint32_t s_tick_count       = 0;

// ── Previous snapshot for diff ────────────────────────
static struct {
    uint32_t free_heap;
    uint32_t min_heap;
    uint32_t free_psram;
    uint32_t min_psram;
    uint8_t  batt_pct;
    int16_t  rssi_x4;
    int16_t  snr_x4;
    uint16_t widget_count;
    uint16_t evq_depth;
    uint16_t task_count;
    bool     valid;
} s_prev;

// ── Diff thresholds ───────────────────────────────────
static constexpr int32_t  HEAP_DIFF_THRESHOLD   = 512;
static constexpr int32_t  PSRAM_DIFF_THRESHOLD  = 1024;
static constexpr int32_t  BATT_DIFF_THRESHOLD   = 2;
static constexpr int32_t  RSSI_DIFF_THRESHOLD   = 12;  // ×4 => 3 dBm

// ── Phase 1 Static State ─────────────────────────────
static uint32_t s_last_loop_us      = 0;
static uint32_t s_peak_loop_us      = 0;
static uint8_t  s_active_screen     = 0;
static uint8_t  s_last_screen       = 0;
static uint32_t s_screen_birth_ms   = 0;
static uint16_t s_screen_transitions = 0;
static bool     s_display_on        = true;
static uint16_t s_wake_count        = 0;
static uint16_t s_sleep_count       = 0;

// ── Screen name mapping ──────────────────────────────
static const char* screen_name_str(uint8_t scr) {
    using namespace sigurdos::ui;
    switch (static_cast<Screen>(scr)) {
        case Screen::Home:              return "Home";
        case Screen::Chat:              return "Chat";
        case Screen::Contacts:          return "Contacts";
        case Screen::Channels:          return "Channels";
        case Screen::Network:           return "Network";
        case Screen::Heard:             return "Heard";
        case Screen::Map:               return "Map";
        case Screen::Advertise:         return "Advertise";
        case Screen::Settings:          return "Settings";
        case Screen::Trace:             return "Trace";
        case Screen::Terminal:          return "Terminal";
        case Screen::Signal:            return "Signal";
        case Screen::RadioSetup:        return "RadioSetup";
        case Screen::Repeaters:         return "Repeaters";
        case Screen::Onboarding:        return "Onboarding";
        case Screen::SettingsRadio:     return "SettingsRadio";
        case Screen::SettingsGPS:       return "SettingsGPS";
        case Screen::SettingsDisplay:   return "SettingsDisplay";
        case Screen::SettingsSystem:    return "SettingsSystem";
        case Screen::NodeStats:         return "NodeStats";
        case Screen::Telemetry:         return "Telemetry";
        case Screen::NodeStatus:        return "NodeStatus";
        case Screen::WiFiNetworks:      return "WiFiNetworks";
        case Screen::Regions:          return "Regions";
        default:                        return "?";
    }
}

// ── Drift detection ───────────────────────────────────
struct DriftDetector {
    int32_t  baseline;
    int32_t  current;
    int32_t  min_val;
    int32_t  max_val;
    uint32_t window_start_ms;
    uint32_t window_ms;
    bool     active;
    int32_t  threshold;  // emit @drift when delta exceeds this

    void reset(int32_t val, uint32_t now_ms) {
        baseline = current = min_val = max_val = val;
        window_start_ms = now_ms;
    }

    void update(int32_t val, uint32_t now_ms) {
        current = val;
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
    }

    bool should_report() const {
        if (!active || threshold == 0) return false;
        int32_t d = baseline - current;
        return d > threshold;
    }

    const char* trend_str() const {
        int32_t d = current - baseline;
        if (d > threshold / 2)  return "rising";
        if (d < -threshold / 2) return "falling";
        return "stable";
    }
};

static DriftDetector s_heap_drift;
static DriftDetector s_psram_drift;
static DriftDetector s_rssi_drift;
static uint32_t s_drift_window_ms = 60000;  // 60s window

// ── Helpers ───────────────────────────────────────────

static uint32_t now_ms() { return millis(); }

static uint16_t count_lvgl_widgets() {
    lv_obj_t* act = lv_scr_act();
    if (!act) return 0;
    // Count all children recursively (shallow enough for embedded)
    uint32_t count = 1;  // screen itself
    uint32_t child_cnt = lv_obj_get_child_count(act);
    count += child_cnt;
    for (uint32_t i = 0; i < child_cnt && i < 100; i++) {
        lv_obj_t* child = lv_obj_get_child(act, i);
        if (child) count += lv_obj_get_child_count(child);
    }
    // Cap to avoid counting too deep in pathological cases
    return count > 65535U ? 65535U : static_cast<uint16_t>(count);
}

static uint16_t count_tasks() {
    return (uint16_t)uxTaskGetNumberOfTasks();
}

static void emit_build_info() {
    const auto& build = sigurdos::build::info();

    emit_tag(tag::BUILD);
    emit_sep(); emit_kv_s(key::FW, build.firmware_version);
    emit_sep(); emit_kv_s(key::GIT, build.git_sha);
    emit_sep(); emit_kv_u(key::DIRTY, build.git_dirty ? 1U : 0U);
    emit_sep(); emit_kv_s(key::MESHCORE, build.meshcore_sha);
    emit_sep(); emit_kv_s(key::ENV, build.build_env);
    emit_sep(); emit_kv_s(key::PART, build.partitions);
    emit_sep(); emit_kv_s(key::BOARD, build.board);
    emit_sep(); emit_kv_s(key::MCU, build.mcu);
    emit_end();
}

// ── Mesh packet content log (64-entry ring buffer) ─────
static constexpr uint32_t PKTLOG_SIZE = 32;

struct PktLogEntry {
    uint32_t timestamp;
    char     sender[32];
    char     channel[32];
    char     text[64];
    int      rssi;
};

static PktLogEntry* s_pktlog     = nullptr;
static uint32_t      s_pktlog_head  = 0;
static uint32_t      s_pktlog_count = 0;

static void init_pktlog() {
    s_pktlog = (PktLogEntry*)heap_caps_malloc(
        PKTLOG_SIZE * sizeof(PktLogEntry),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pktlog) {
        s_pktlog = (PktLogEntry*)malloc(PKTLOG_SIZE * sizeof(PktLogEntry));
    }
    if (s_pktlog) {
        memset(s_pktlog, 0, PKTLOG_SIZE * sizeof(PktLogEntry));
    }
}

void push_packet_log(const char* sender, const char* channel,
                     const char* text, int rssi) {
    if (!s_pktlog) return;
    const char* sender_safe = packet_log_field_or_empty(sender);
    const char* channel_safe = packet_log_field_or_empty(channel);
    const char* text_safe = packet_log_field_or_empty(text);
    uint32_t idx = s_pktlog_head;
    s_pktlog[idx].timestamp = millis() / 1000;
    strncpy(s_pktlog[idx].sender,  sender_safe,  sizeof(s_pktlog[idx].sender) - 1);
    s_pktlog[idx].sender[sizeof(s_pktlog[idx].sender) - 1] = '\0';
    strncpy(s_pktlog[idx].channel, channel_safe, sizeof(s_pktlog[idx].channel) - 1);
    s_pktlog[idx].channel[sizeof(s_pktlog[idx].channel) - 1] = '\0';
    strncpy(s_pktlog[idx].text,    text_safe,    sizeof(s_pktlog[idx].text) - 1);
    s_pktlog[idx].text[sizeof(s_pktlog[idx].text) - 1] = '\0';
    s_pktlog[idx].rssi = rssi;
    s_pktlog_head = (s_pktlog_head + 1) % PKTLOG_SIZE;
    if (s_pktlog_count < UINT32_MAX) s_pktlog_count++;
}

static void emit_pktlog() {
    if (!s_pktlog) {
        emit_tag(tag::PKT);
        emit_sep();
        emit_kv_u(key::N, 0);
        emit_end();
        return;
    }
    uint32_t total = s_pktlog_count;
    if (total == 0) {
        emit_tag(tag::PKT);
        emit_sep();
        emit_kv_u(key::N, 0);
        emit_end();
        return;
    }
    uint32_t start = total > 20 ? total - 20 : 0;
    for (uint32_t idx = start; idx < total; idx++) {
        uint32_t phys = idx % PKTLOG_SIZE;
        const PktLogEntry& e = s_pktlog[phys];
        emit_tag(tag::PKT);
        emit_sep();
        emit_kv_u(key::I, idx);
        emit_sep();
        emit_kv_s(key::SRC, e.sender);
        emit_sep();
        emit_kv_s(key::CHAN, e.channel);
        emit_sep();
        emit_kv(key::RSSI, e.rssi);
        emit_sep();
        emit_kv_s(key::TEXT, e.text);
        emit_end();
    }
}

static void emit_mesh_chan_stats() {
    int nch = sigurdos::mesh::getChannelCount();
    if (nch <= 0) return;
    constexpr int MAX_CH = 32;
    char names[MAX_CH][37];
    int got = sigurdos::mesh::exportChannels(names, MAX_CH);
    for (int i = 0; i < got; i++) {
        emit_tag(tag::MESH_CHAN);
        emit_sep();
        emit_kv_u(key::I, (uint32_t)i);
        emit_sep();
        emit_kv_s(key::CHAN, names[i]);
        emit_end();
    }
}

static void emit_full_heartbeat() {
    static uint32_t s_hb_peripheral_ticks = 0;
    s_hb_peripheral_ticks++;

    emit_tag(tag::HB);
    emit_sep();
    emit_kv_u(key::T, millis() / 1000);
    emit_sep();
    emit_kv_u(key::H, ESP.getFreeHeap());
    emit_sep();
    emit_kv_u(key::HM, ESP.getMinFreeHeap());
    emit_sep();
    emit_kv_u(key::P, ESP.getFreePsram());
    emit_sep();
    emit_kv_u(key::PM, ESP.getMinFreePsram());
    emit_sep();
    emit_kv_u(key::B, sigurdos_battery_pct());
    emit_sep();
    emit_kv_u(key::MV, sigurdos_battery_mv());
    emit_sep();
    emit_kv_u(key::RSSI, sigurdos::mesh::getLastRSSI());
    emit_sep();
    emit_kv_u(key::WIDGETS, count_lvgl_widgets());
    // Phase 1 heartbeat fields — screen, loop, display
    emit_sep();
    emit_kv_s(key::SCREEN, screen_name_str(s_active_screen));
    emit_sep();
    emit_kv_u(key::SCREEN_MS, s_screen_birth_ms ? (millis() - s_screen_birth_ms) : 0);
    emit_sep();
    emit_kv_u(key::LOOP_US, s_last_loop_us);
    emit_sep();
    emit_kv_u(key::PEAK_US, s_peak_loop_us);
    emit_sep();
    emit_kv_u(key::DISP, (uint32_t)(s_display_on ? 1 : 0));
    emit_sep();
    emit_kv_u(key::DISP_WAKE, (uint32_t)s_wake_count);
    emit_sep();
    emit_kv_u(key::DISP_SLEEP, (uint32_t)s_sleep_count);
    // Phase 2+3+4 — render count and event queue depth
    emit_sep();
    emit_kv_u(key::RENDERS, input::get_lvgl_render_count());
    emit_sep();
    emit_kv_u(key::EVQ, input::get_lvgl_evq_depth());
    // Phase 2+3+4 — peripheral state (every 6th heartbeat)
    if (s_hb_peripheral_ticks % 6 == 0) {
        collectors::WifiSnapshot wifi = collectors::collect_wifi();
        emit_sep();
        emit_kv_u(key::WIFI_CONN, wifi.sta_connected ? 1 : 0);
        if (wifi.sta_connected) {
            emit_sep();
            emit_kv(key::W_RSSI, wifi.sta_rssi);
        }
        collectors::SdCardSnapshot sd = collectors::collect_sd();
        emit_sep();
        emit_kv_u(key::SD_MOUNT, sd.mounted ? 1 : 0);
        float temp_c = collectors::collect_temp();
        emit_sep();
        emit_kv(key::TEMP_VAL, (int32_t)(temp_c * 10.0f));
        collectors::NvsSnapshot nvs = collectors::collect_nvs();
        emit_sep();
        emit_kv_u(key::NVS_USED, nvs.used_bytes);
    }
    emit_end();

    // Phase 2+3+4 — alerts (emitted as separate @alert records)
    {
        uint8_t batt = sigurdos_battery_pct();
        if (batt < 10) {
            emit_tag(tag::ALERT);
            emit_sep();
            emit_kv_s(key::DESC, "battery_critical");
            emit_sep();
            emit_kv_u(key::B, batt);
            emit_end();
        }
    }
    {
        uint32_t heap = ESP.getFreeHeap();
        if (heap < 51200) {
            emit_tag(tag::ALERT);
            emit_sep();
            emit_kv_s(key::DESC, "heap_low");
            emit_sep();
            emit_kv_u(key::H, heap);
            emit_end();
        }
    }

    // Update previous snapshot
    s_prev.free_heap    = ESP.getFreeHeap();
    s_prev.min_heap     = ESP.getMinFreeHeap();
    s_prev.free_psram   = ESP.getFreePsram();
    s_prev.min_psram    = ESP.getMinFreePsram();
    s_prev.batt_pct     = sigurdos_battery_pct();
    s_prev.rssi_x4      = (int16_t)(sigurdos::mesh::getLastRSSI() * 4);
    s_prev.snr_x4       = (int16_t)(sigurdos::mesh::getLastSNR() * 4);
    s_prev.widget_count = count_lvgl_widgets();
    s_prev.evq_depth    = 0;
    s_prev.task_count   = count_tasks();
    s_prev.valid        = true;

    // Push to hb-ring
    HbRingEntry ring_entry;
    ring_entry.t_s   = millis() / 1000;
    ring_entry.h     = s_prev.free_heap;
    ring_entry.hm    = s_prev.min_heap;
    ring_entry.p     = s_prev.free_psram;
    ring_entry.b     = s_prev.batt_pct;
    ring_entry.rssi  = s_prev.rssi_x4;
    ring_entry.wt    = s_prev.widget_count;
    ring_entry.flags = 0;  // full heartbeat
    hb_ring_push(ring_entry);
}

static void emit_diff_heartbeat() {
    uint32_t h  = ESP.getFreeHeap();
    uint32_t hm = ESP.getMinFreeHeap();
    uint32_t p  = ESP.getFreePsram();
    uint8_t  b  = sigurdos_battery_pct();
    int16_t  rx = (int16_t)(sigurdos::mesh::getLastRSSI() * 4);
    uint16_t wt = count_lvgl_widgets();

    bool any_changed = false;

    emit_tag(tag::HB_DIFF);
    emit_sep();
    emit_kv_u(key::T, millis() / 1000);

    if (s_prev.valid) {
        int32_t dh = (int32_t)s_prev.free_heap - (int32_t)h;
        if (dh < -HEAP_DIFF_THRESHOLD || dh > HEAP_DIFF_THRESHOLD) {
            emit_sep(); emit_kv_u(key::H, h); any_changed = true;
        }
        if (s_prev.batt_pct != b) {
            emit_sep(); emit_kv_u(key::B, b); any_changed = true;
        }
        if (s_prev.widget_count != wt) {
            emit_sep(); emit_kv_u(key::WIDGETS, wt); any_changed = true;
        }
        int32_t dr = (int32_t)s_prev.rssi_x4 - (int32_t)rx;
        if (dr < -RSSI_DIFF_THRESHOLD || dr > RSSI_DIFF_THRESHOLD) {
            emit_sep(); emit_kv_u(key::RSSI, (uint32_t)(rx / 4)); any_changed = true;
        }
    } else {
        // No previous snapshot — emit all fields
        emit_sep(); emit_kv_u(key::H, h);
        emit_sep(); emit_kv_u(key::P, p);
        emit_sep(); emit_kv_u(key::B, b);
        emit_sep(); emit_kv_u(key::RSSI, (uint32_t)(rx / 4));
        any_changed = true;
    }

    emit_end();

    // Update previous
    s_prev.free_heap    = h;
    s_prev.min_heap     = hm;
    s_prev.free_psram   = p;
    s_prev.batt_pct     = b;
    s_prev.rssi_x4      = rx;
    s_prev.widget_count = wt;
    s_prev.valid        = true;

    (void)any_changed;  // future: if nothing changed, we could suppress entirely
}

// ── Drift update ──────────────────────────────────────

static void update_drift(uint32_t now, uint32_t heap, uint32_t psram, int16_t rssi_x4) {
    // Initialise drift windows if needed
    if (!s_heap_drift.active) {
        s_heap_drift.active = true;
        s_heap_drift.threshold = 4096;  // 4 KB heap leak threshold
        s_heap_drift.window_ms = s_drift_window_ms;
        s_heap_drift.reset((int32_t)heap, now);
    }
    if (!s_psram_drift.active) {
        s_psram_drift.active = true;
        s_psram_drift.threshold = 16384;  // 16 KB PSRAM leak threshold
        s_psram_drift.window_ms = s_drift_window_ms;
        s_psram_drift.reset((int32_t)psram, now);
    }
    if (!s_rssi_drift.active) {
        s_rssi_drift.active = true;
        s_rssi_drift.threshold = 48;  // 12 dBm drift threshold (×4)
        s_rssi_drift.window_ms = s_drift_window_ms;
        s_rssi_drift.reset((int32_t)rssi_x4, now);
    }

    s_heap_drift.update((int32_t)heap, now);
    s_psram_drift.update((int32_t)psram, now);
    s_rssi_drift.update((int32_t)rssi_x4, now);

    // Check for drift reports
    if (s_heap_drift.should_report()) {
        emit_tag(tag::DRIFT);
        emit_sep();
        emit_kv_s(key::METRIC, "heap");
        emit_sep(); emit_kv_u(key::BASELINE, (uint32_t)s_heap_drift.baseline);
        emit_sep(); emit_kv_u(key::CURRENT,  (uint32_t)s_heap_drift.current);
        emit_sep(); emit_kv_u(key::DELTA,    (uint32_t)(s_heap_drift.baseline - s_heap_drift.current));
        emit_sep(); emit_kv_s(key::TREND,    s_heap_drift.trend_str());
        emit_end();
        s_heap_drift.reset((int32_t)heap, now);
    }
    if (s_psram_drift.should_report()) {
        emit_tag(tag::DRIFT);
        emit_sep();
        emit_kv_s(key::METRIC, "psram");
        emit_sep(); emit_kv_u(key::BASELINE, (uint32_t)s_psram_drift.baseline);
        emit_sep(); emit_kv_u(key::CURRENT,  (uint32_t)s_psram_drift.current);
        emit_sep(); emit_kv_u(key::DELTA,    (uint32_t)(s_psram_drift.baseline - s_psram_drift.current));
        emit_sep(); emit_kv_s(key::TREND,    s_psram_drift.trend_str());
        emit_end();
        s_psram_drift.reset((int32_t)psram, now);
    }
    if (s_rssi_drift.should_report()) {
        emit_tag(tag::DRIFT);
        emit_sep();
        emit_kv_s(key::METRIC, "rssi");
        emit_sep(); emit_kv_u(key::BASELINE, (uint32_t)(s_rssi_drift.baseline / 4));
        emit_sep(); emit_kv_u(key::CURRENT,  (uint32_t)(s_rssi_drift.current / 4));
        emit_sep(); emit_kv_u(key::DELTA,    (uint32_t)((s_rssi_drift.baseline - s_rssi_drift.current) / 4));
        emit_sep(); emit_kv_s(key::TREND,    s_rssi_drift.trend_str());
        emit_end();
        s_rssi_drift.reset((int32_t)rssi_x4, now);
    }
}

// ── Public API ────────────────────────────────────────

void init() {
    // Initialise packet log buffer in PSRAM (DRAM fallback)
    init_pktlog();

    // Initialise crash subsystem (checks RTC memory for previous crash)
    crash::init();

    // Announce telemetry active
    emit_record1_u(tag::OK, (char*)"init", 0);
    emit_build_info();
    // Initial full heartbeat
    emit_full_heartbeat();
}

void loop() {
    if (!s_enabled) return;

    uint32_t now = now_ms();
    if (now - s_last_tick_ms >= s_heartbeat_ms) {
        s_last_tick_ms = now;
        s_tick_count++;

        // Update drift detectors
        int16_t rssi_x4 = (int16_t)(sigurdos::mesh::getLastRSSI() * 4);
        update_drift(now, ESP.getFreeHeap(), ESP.getFreePsram(), rssi_x4);

        if (s_diff_enabled && s_tick_count % s_full_sync_every != 0) {
            emit_diff_heartbeat();
        } else {
            emit_full_heartbeat();
        }

        // WDT detection: emit @alert if loop exceeds 500ms
        if (s_last_loop_us > 500000) {
            emit_tag(tag::ALERT);
            emit_sep();
            emit_kv_s(key::DESC, "wdt_pending");
            emit_sep();
            emit_kv_u(key::LOOP_US, s_last_loop_us);
            emit_end();
        }
    }
}

// ── Command handlers ──────────────────────────────────

// ── Phase 1 Telemetry Hooks ──────────────────────────

void report_screen_transition(uint8_t from, uint8_t to, uint32_t now_ms) {
    (void)from;
    s_last_screen = s_active_screen;
    s_active_screen = to;
    s_screen_birth_ms = now_ms;
    s_screen_transitions++;
}

void report_loop_timing(uint32_t elapsed_us) {
    s_last_loop_us = elapsed_us;
    if (elapsed_us > s_peak_loop_us) {
        s_peak_loop_us = elapsed_us;
    }
    // Auto-emit alert if loop exceeds 500ms
    if (elapsed_us > 500000) {
        emit_tag(tag::ALERT);
        emit_sep();
        emit_kv_s(key::DESC, "loop_blocked");
        emit_sep();
        emit_kv_u(key::LOOP_US, elapsed_us);
        emit_end();
    }
}

void report_display_wake() {
    s_display_on = true;
    s_wake_count++;
    emit_tag(tag::ALERT);
    emit_sep();
    emit_kv_s(key::DESC, "display_wake");
    emit_sep();
    emit_kv_u(key::DISP_WAKE, (uint32_t)s_wake_count);
    emit_end();
}

void report_display_sleep() {
    s_display_on = false;
    s_sleep_count++;
    emit_tag(tag::ALERT);
    emit_sep();
    emit_kv_s(key::DESC, "display_sleep");
    emit_sep();
    emit_kv_u(key::DISP_SLEEP, (uint32_t)s_sleep_count);
    emit_end();
}

// ── Phase 2+3+4 hook implementations ───────────────────

void report_key_event(uint8_t keycode) {
    input::report_key_event(keycode);
}

void report_touch_event(uint16_t x, uint16_t y) {
    input::report_touch_event(x, y);
}

void report_trackball_event(uint8_t direction) {
    input::report_trackball_event(direction);
}

void report_render_flush() {
    input::increment_render_count();
}

// ── Command handlers ──────────────────────────────────

void cmd_telemetry(const char* arg) {
    if (!arg || !arg[0]) {
        // Show status
        emit_tag(tag::OK);
        emit_sep(); emit_kv_s(key::CMD, "telemetry");
        emit_sep(); emit_kv_u(key::H, (uint32_t)(s_enabled ? 1 : 0));
        emit_sep(); emit_kv_u(key::T, s_heartbeat_ms);
        emit_sep(); emit_kv_u(key::I, s_tick_count);
        emit_end();
        return;
    }

    if (strcmp(arg, "on") == 0) {
        s_enabled = true;
        emit_record1_s(tag::OK, key::CMD, "telemetry on");
    } else if (strcmp(arg, "off") == 0) {
        s_enabled = false;
        emit_record1_s(tag::OK, key::CMD, "telemetry off");
    } else if (strncmp(arg, "diff ", 5) == 0) {
        const char* val = arg + 5;
        s_diff_enabled = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
        emit_record1_s(tag::OK, key::CMD, s_diff_enabled ? "diff on" : "diff off");
    } else if (strncmp(arg, "hb ", 3) == 0) {
        int ms = atoi(arg + 3);
        if (ms >= 1000 && ms <= 60000) {
            s_heartbeat_ms = (uint32_t)ms;
        }
        emit_record1_u(tag::OK, key::T, s_heartbeat_ms);
    } else if (strcmp(arg, "full") == 0) {
        emit_full_heartbeat();
    } else {
        emit_err("telemetry", "Unknown subcommand");
    }
}

void cmd_query(const char* arg) {
    if (!arg || !arg[0]) {
        emit_err("query", "Missing subcommand (build|state|heap|lvgl|mesh|crash|drift|screen|wifi|gps|radio|sd|nvs|temp|task|hb-ring|pktlog|full)");
        return;
    }

    uint32_t start = micros();
    uint32_t n = 0;

    emit_ok(arg, 0);  // cost_us filled after

    // ── query full: emit ALL state in one shot ────────────
    if (strcmp(arg, "full") == 0) {
        emit_build_info();
        n++;

        // @heap (existing heap/lvgl/mesh state)
        emit_tag(tag::HEAP);
        emit_sep(); emit_kv_u(key::H, ESP.getFreeHeap());
        emit_sep(); emit_kv_u(key::HM, ESP.getMinFreeHeap());
        emit_sep(); emit_kv_u(key::P, ESP.getFreePsram());
        emit_sep(); emit_kv_u(key::PM, ESP.getMinFreePsram());
        emit_sep(); emit_kv_u(key::B, sigurdos_battery_pct());
        emit_sep(); emit_kv_u(key::MV, sigurdos_battery_mv());
        emit_sep(); emit_kv_u(key::RSSI, (uint32_t)sigurdos::mesh::getLastRSSI());
        emit_sep(); emit_kv_u(key::SNR, (uint32_t)(sigurdos::mesh::getLastSNR() * 10));
        emit_sep(); emit_kv_u(key::WIDGETS, count_lvgl_widgets());
        emit_end();
        n++;

        // @lvgl
        emit_tag(tag::LVGL);
        {   uint16_t wt = count_lvgl_widgets();
            emit_sep(); emit_kv_u(key::WIDGETS, wt);
#if !LV_MEM_CUSTOM
            lv_mem_monitor_t mon;
            lv_mem_monitor(&mon);
            emit_sep(); emit_kv_u(key::P, mon.free_size);
            emit_sep(); emit_kv_u(key::B, mon.used_pct);
#endif
            emit_end();
        }
        n++;

        // @mesh
        emit_tag(tag::MESH);
        emit_sep(); emit_kv_u(key::RSSI, (uint32_t)sigurdos::mesh::getLastRSSI());
        emit_sep(); emit_kv_u(key::SNR, (uint32_t)(sigurdos::mesh::getLastSNR() * 10));
        emit_sep(); emit_kv_u(key::NOISE, (uint32_t)sigurdos::mesh::getNoiseFloor());
        emit_sep(); emit_kv_u(key::TX, sigurdos::mesh::getNumSentFlood() + sigurdos::mesh::getNumSentDirect());
        emit_sep(); emit_kv_u(key::RX, sigurdos::mesh::getNumRecvFlood() + sigurdos::mesh::getNumRecvDirect());
        emit_end();
        n++;

        // @mesh_chan — per-channel mesh stats
        emit_mesh_chan_stats();

        // @screen
        emit_tag(tag::SCREEN);
        emit_sep(); emit_kv_s(key::NAME, screen_name_str(s_active_screen));
        emit_sep(); emit_kv_u(key::SCREEN_MS, s_screen_birth_ms ? (millis() - s_screen_birth_ms) : 0);
        emit_sep(); emit_kv_u(key::SCREEN_CNT, (uint32_t)s_screen_transitions);
        emit_end();
        n++;

        // Phase 2+3+4 — peripheral state queries
        collectors::collect_and_emit_wifi();       n++;
        collectors::collect_and_emit_gps();        n++;
        collectors::collect_and_emit_radio();      n++;
        collectors::collect_and_emit_sd();         n++;
        collectors::collect_and_emit_nvs();        n++;
        collectors::collect_and_emit_temp();       n++;
        collectors::collect_and_emit_tasks();      n++;

        // @hb (vitals from existing heartbeat emission)
        emit_tag(tag::HB);
        emit_sep(); emit_kv_u(key::T, millis() / 1000);
        emit_sep(); emit_kv_u(key::H, ESP.getFreeHeap());
        emit_sep(); emit_kv_u(key::HM, ESP.getMinFreeHeap());
        emit_sep(); emit_kv_u(key::P, ESP.getFreePsram());
        emit_sep(); emit_kv_u(key::PM, ESP.getMinFreePsram());
        emit_sep(); emit_kv_u(key::B, sigurdos_battery_pct());
        emit_sep(); emit_kv_u(key::MV, sigurdos_battery_mv());
        emit_sep(); emit_kv_u(key::RSSI, (uint32_t)sigurdos::mesh::getLastRSSI());
        emit_sep(); emit_kv_u(key::WIDGETS, count_lvgl_widgets());
        emit_sep(); emit_kv_s(key::SCREEN, screen_name_str(s_active_screen));
        emit_sep(); emit_kv_u(key::SCREEN_MS, s_screen_birth_ms ? (millis() - s_screen_birth_ms) : 0);
        emit_sep(); emit_kv_u(key::LOOP_US, s_last_loop_us);
        emit_sep(); emit_kv_u(key::PEAK_US, s_peak_loop_us);
        emit_sep(); emit_kv_u(key::DISP, (uint32_t)(s_display_on ? 1 : 0));
        emit_sep(); emit_kv_u(key::DISP_WAKE, (uint32_t)s_wake_count);
        emit_sep(); emit_kv_u(key::DISP_SLEEP, (uint32_t)s_sleep_count);
        emit_end();
        n++;

        // @end|cmd=full|n=<count>
        uint32_t cost = micros() - start;
        emit_tag(tag::END);
        emit_sep(); emit_kv_s(key::CMD, "full");
        emit_sep(); emit_kv_u(key::N, n);
        emit_end();
        emit_ok(arg, cost);
        return;
    }

    if (strcmp(arg, "build") == 0) {
        emit_build_info();
        n = 1;
    }

    if (strcmp(arg, "state") == 0 || strcmp(arg, "heap") == 0) {
        emit_tag(tag::HEAP);
        emit_sep(); emit_kv_u(key::H, ESP.getFreeHeap());
        emit_sep(); emit_kv_u(key::HM, ESP.getMinFreeHeap());
        emit_sep(); emit_kv_u(key::P, ESP.getFreePsram());
        emit_sep(); emit_kv_u(key::PM, ESP.getMinFreePsram());
        emit_sep(); emit_kv_u(key::B, sigurdos_battery_pct());
        emit_sep(); emit_kv_u(key::MV, sigurdos_battery_mv());
        emit_sep(); emit_kv_u(key::RSSI, (uint32_t)sigurdos::mesh::getLastRSSI());
        emit_sep(); emit_kv_u(key::SNR, (uint32_t)(sigurdos::mesh::getLastSNR() * 10));
        emit_sep(); emit_kv_u(key::WIDGETS, count_lvgl_widgets());
        emit_end();
        n = 1;
    }

    if (strcmp(arg, "state") == 0 || strcmp(arg, "lvgl") == 0) {
        emit_tag(tag::LVGL);
        uint16_t wt = count_lvgl_widgets();
        emit_sep(); emit_kv_u(key::WIDGETS, wt);
#if !LV_MEM_CUSTOM
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        emit_sep(); emit_kv_u(key::P, mon.free_size);
        emit_sep(); emit_kv_u(key::B, mon.used_pct);
#endif
        emit_end();
        n = (strcmp(arg, "state") == 0) ? 2 : 1;
    }

    if (strcmp(arg, "mesh") == 0 || strcmp(arg, "state") == 0) {
        emit_tag(tag::MESH);
        emit_sep(); emit_kv_u(key::RSSI, (uint32_t)sigurdos::mesh::getLastRSSI());
        emit_sep(); emit_kv_u(key::SNR, (uint32_t)(sigurdos::mesh::getLastSNR() * 10));
        emit_sep(); emit_kv_u(key::NOISE, (uint32_t)sigurdos::mesh::getNoiseFloor());
        emit_sep(); emit_kv_u(key::TX, sigurdos::mesh::getNumSentFlood() + sigurdos::mesh::getNumSentDirect());
        emit_sep(); emit_kv_u(key::RX, sigurdos::mesh::getNumRecvFlood() + sigurdos::mesh::getNumRecvDirect());
        emit_end();
        n = (strcmp(arg, "mesh") == 0) ? 1 : 3;
        // Per-channel stats for explicit \"mesh\" query
        if (strcmp(arg, "mesh") == 0) {
            emit_mesh_chan_stats();
        }
    }

    if (strcmp(arg, "drift") == 0) {
        if (s_heap_drift.active) {
            emit_tag(tag::DRIFT);
            emit_sep(); emit_kv_s(key::METRIC, "heap");
            emit_sep(); emit_kv_u(key::BASELINE, (uint32_t)s_heap_drift.baseline);
            emit_sep(); emit_kv_u(key::CURRENT,  (uint32_t)s_heap_drift.current);
            emit_sep(); emit_kv_u(key::DELTA,    (uint32_t)(s_heap_drift.baseline - s_heap_drift.current));
            emit_sep(); emit_kv_s(key::TREND,    s_heap_drift.trend_str());
            emit_end();
            n++;
        }
        if (s_psram_drift.active) {
            emit_tag(tag::DRIFT);
            emit_sep(); emit_kv_s(key::METRIC, "psram");
            emit_sep(); emit_kv_u(key::BASELINE, (uint32_t)s_psram_drift.baseline);
            emit_sep(); emit_kv_u(key::CURRENT,  (uint32_t)s_psram_drift.current);
            emit_sep(); emit_kv_u(key::DELTA,    (uint32_t)(s_psram_drift.baseline - s_psram_drift.current));
            emit_sep(); emit_kv_s(key::TREND,    s_psram_drift.trend_str());
            emit_end();
            n++;
        }
    }

    if (strcmp(arg, "screen") == 0) {
        emit_tag(tag::SCREEN);
        emit_sep(); emit_kv_s(key::NAME, screen_name_str(s_active_screen));
        emit_sep(); emit_kv_u(key::SCREEN_MS, s_screen_birth_ms ? (millis() - s_screen_birth_ms) : 0);
        emit_sep(); emit_kv_u(key::SCREEN_CNT, (uint32_t)s_screen_transitions);
        emit_end();
        n = 1;
    }

    // ── Phase 2+3+4 query subcommands ────────────────────
    if (strcmp(arg, "wifi") == 0) {
        collectors::collect_and_emit_wifi();
        n = 1;
    }
    if (strcmp(arg, "gps") == 0) {
        collectors::collect_and_emit_gps();
        n = 1;
    }
    if (strcmp(arg, "radio") == 0) {
        collectors::collect_and_emit_radio();
        n = 1;
    }
    if (strcmp(arg, "sd") == 0) {
        collectors::collect_and_emit_sd();
        n = 1;
    }
    if (strcmp(arg, "nvs") == 0) {
        collectors::collect_and_emit_nvs();
        n = 1;
    }
    if (strcmp(arg, "temp") == 0) {
        collectors::collect_and_emit_temp();
        n = 1;
    }
    if (strcmp(arg, "task") == 0) {
        collectors::collect_and_emit_tasks();
        n = 1;
    }
    if (strcmp(arg, "hb-ring") == 0) {
        hb_ring_query(20);
        n = 0;  // hb_ring_query emits its own records
    }
    if (strcmp(arg, "pktlog") == 0) {
        emit_pktlog();
        n = 0;  // emit_pktlog emits its own records
    }

    if (strcmp(arg, "crash") == 0) {
        crash::query();
        n = 0;  // crash::query() calls emit_end_resp itself; we skip the final emit_end_resp
    } else if (strncmp(arg, "crash ", 6) == 0) {
        const char* sub = arg + 6;
        if (strcmp(sub, "clear") == 0) {
            crash::clear();
            n = 0;  // clear() already emitted @ok
        } else if (strcmp(sub, "test") == 0) {
            crash::test();
            n = 0;  // test triggers immediate crash, no response needed
        } else {
            emit_err("crash", "Unknown subcommand (clear, test)");
            n = 0;
        }
    }

    if (n > 0) {
        uint32_t cost = micros() - start;
        emit_end_resp(arg, n);
        // Re-emit ok with actual cost (overwrites the placeholder)
        emit_ok(arg, cost);
    }
}

void cmd_crash(const char* arg) {
    if (!arg || strcmp(arg, "report") == 0) {
        crash::query();
    } else if (strcmp(arg, "clear") == 0) {
        crash::clear();
    } else if (strcmp(arg, "test") == 0) {
        crash::test();
    } else {
        emit_err("crash", "Unknown subcommand");
    }
}

void cmd_drift(const char*) {
    cmd_query("drift");
}

bool is_enabled()  { return s_enabled; }
uint32_t tick_count() { return s_tick_count; }
uint32_t uptime_s()   { return millis() / 1000; }

}  // namespace telemetry
}  // namespace sigurdos

#endif // SIGURDOS_TELEMETRY
