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
#include "telemetry_drift.h"
#include "telemetry_policy.h"

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

// ── Heartbeat snapshots ───────────────────────────────
struct HeartbeatSnapshot {
    uint32_t free_heap;
    uint32_t min_heap;
    uint32_t free_psram;
    uint8_t  batt_pct;
    int16_t  rssi_x4;
    uint16_t widget_count;
    bool     widget_count_truncated;
    bool     valid;
};

static HeartbeatSnapshot s_prev;

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
    return screen_name(static_cast<sigurdos::ui::Screen>(scr));
}

// ── Drift detection ───────────────────────────────────
static DriftDetector s_heap_drift;
static DriftDetector s_psram_drift;
static DriftDetector s_rssi_drift;
static uint32_t s_drift_window_ms = 60000;  // 60s window

// ── Helpers ───────────────────────────────────────────

static uint32_t now_ms() { return millis(); }

static WidgetTreeCount count_lvgl_widgets() {
    lv_obj_t* act = lv_scr_act();
    return count_widget_tree(
        act,
        [](lv_obj_t* obj) { return lv_obj_get_child_count(obj); },
        [](lv_obj_t* obj, uint32_t index) {
            return lv_obj_get_child(obj, static_cast<int32_t>(index));
        });
}

static HeartbeatSnapshot capture_heartbeat_snapshot() {
    HeartbeatSnapshot snapshot{};
    snapshot.free_heap = ESP.getFreeHeap();
    snapshot.min_heap = ESP.getMinFreeHeap();
    snapshot.free_psram = ESP.getFreePsram();
    snapshot.batt_pct = sigurdos_battery_pct();
    snapshot.rssi_x4 = static_cast<int16_t>(sigurdos::mesh::getLastRSSI() * 4);
    const WidgetTreeCount widgets = count_lvgl_widgets();
    snapshot.widget_count = widgets.count;
    snapshot.widget_count_truncated = widgets.truncated;
    snapshot.valid = true;
    return snapshot;
}

static void retain_heartbeat(const HeartbeatSnapshot& snapshot, bool diff) {
    HbRingEntry entry{};
    entry.t_s = millis() / 1000;
    entry.h = snapshot.free_heap;
    entry.hm = snapshot.min_heap;
    entry.p = snapshot.free_psram;
    entry.b = snapshot.batt_pct;
    entry.rssi = snapshot.rssi_x4;
    entry.wt = snapshot.widget_count;
    entry.flags = diff ? 0x01U : 0U;
    hb_ring_push(entry);
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
    emit_sep(); emit_kv_s(key::BUILD_SOURCE, build.build_source);
    emit_sep(); emit_kv_s(key::RUN_ID, build.actions_run_id);
    emit_sep(); emit_kv_s(key::RUN_ATTEMPT, build.actions_run_attempt);
    emit_sep(); emit_kv_s(key::REF, build.actions_ref);
    emit_sep(); emit_kv_s(key::RUN_URL, build.actions_run_url);
    emit_end();
}

// ── Mesh packet content log (64-entry ring buffer) ─────
static constexpr uint32_t PKTLOG_SIZE = 32;

struct PktLogEntry {
    uint32_t timestamp;
    char     direction[4];
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

void push_packet_log(const char* direction, const char* sender,
                     const char* channel, const char* text, int rssi) {
    if (!s_pktlog) return;
    const char* sender_safe = packet_log_field_or_empty(sender);
    const char* channel_safe = packet_log_field_or_empty(channel);
    const char* text_safe = packet_log_field_or_empty(text);
    uint32_t idx = s_pktlog_head;
    s_pktlog[idx].timestamp = millis() / 1000;
    packet_log_copy_field(s_pktlog[idx].direction,
                          sizeof(s_pktlog[idx].direction), direction);
    packet_log_copy_field(s_pktlog[idx].sender, sizeof(s_pktlog[idx].sender), sender_safe);
    packet_log_copy_field(s_pktlog[idx].channel, sizeof(s_pktlog[idx].channel), channel_safe);
    packet_log_copy_field(s_pktlog[idx].text, sizeof(s_pktlog[idx].text),
                          packet_log_text_by_policy(text_safe));
    s_pktlog[idx].rssi = rssi;
    s_pktlog_head = (s_pktlog_head + 1) % PKTLOG_SIZE;
    if (s_pktlog_count < UINT32_MAX) s_pktlog_count++;
}

static uint32_t emit_pktlog() {
    if (!s_pktlog) {
        emit_tag(tag::PKT);
        emit_sep();
        emit_kv_u(key::N, 0);
        emit_end();
        return 1;
    }
    uint32_t total = s_pktlog_count;
    if (total == 0) {
        emit_tag(tag::PKT);
        emit_sep();
        emit_kv_u(key::N, 0);
        emit_end();
        return 1;
    }
    uint32_t start = total > 20 ? total - 20 : 0;
    uint32_t emitted = 0;
    for (uint32_t idx = start; idx < total; idx++) {
        uint32_t phys = idx % PKTLOG_SIZE;
        const PktLogEntry& e = s_pktlog[phys];
        emit_tag(tag::PKT);
        emit_sep();
        emit_kv_u(key::I, idx);
        emit_sep();
        emit_kv_s(key::TYPE, e.direction);
        emit_sep();
        emit_kv_s(key::SRC, e.sender);
        emit_sep();
        emit_kv_s(key::CHAN, e.channel);
        emit_sep();
        emit_kv(key::RSSI, e.rssi);
        emit_sep();
        emit_kv_s(key::TEXT, e.text);
        emit_end();
        emitted++;
    }
    return emitted;
}

static uint32_t emit_mesh_chan_stats() {
    int nch = sigurdos::mesh::getChannelCount();
    if (nch <= 0) return 0;
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
    return got > 0 ? static_cast<uint32_t>(got) : 0;
}

static void emit_full_heartbeat() {
    static uint32_t s_hb_peripheral_ticks = 0;
    s_hb_peripheral_ticks++;
    const HeartbeatSnapshot snapshot = capture_heartbeat_snapshot();

    emit_tag(tag::HB);
    emit_sep();
    emit_kv_u(key::T, millis() / 1000);
    emit_sep();
    emit_kv_u(key::H, snapshot.free_heap);
    emit_sep();
    emit_kv_u(key::HM, snapshot.min_heap);
    emit_sep();
    emit_kv_u(key::P, snapshot.free_psram);
    emit_sep();
    emit_kv_u(key::PM, ESP.getMinFreePsram());
    emit_sep();
    emit_kv_u(key::B, snapshot.batt_pct);
    emit_sep();
    emit_kv_u(key::MV, sigurdos_battery_mv());
    emit_sep();
    emit_kv(key::RSSI, snapshot.rssi_x4 / 4);
    emit_sep();
    emit_kv_u(key::WIDGETS, snapshot.widget_count);
    emit_sep();
    emit_kv_u(key::WIDGETS_TRUNCATED,
              snapshot.widget_count_truncated ? 1U : 0U);
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
    // Phase 2+3+4 — render count
    emit_sep();
    emit_kv_u(key::RENDERS, input::get_lvgl_render_count());
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
        if (nvs.valid) {
            emit_sep();
            emit_kv_u(key::NVS_USED, nvs.used_entries);
        }
    }
    emit_end();

    // Phase 2+3+4 — alerts (emitted as separate @alert records)
    {
        uint8_t batt = snapshot.batt_pct;
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
        uint32_t heap = snapshot.free_heap;
        if (heap < 51200) {
            emit_tag(tag::ALERT);
            emit_sep();
            emit_kv_s(key::DESC, "heap_low");
            emit_sep();
            emit_kv_u(key::H, heap);
            emit_end();
        }
    }

    s_prev = snapshot;
    retain_heartbeat(snapshot, false);
}

static void emit_diff_heartbeat() {
    const HeartbeatSnapshot snapshot = capture_heartbeat_snapshot();
    bool any_changed = false;

    if (s_prev.valid) {
        int32_t dh = (int32_t)s_prev.free_heap - (int32_t)snapshot.free_heap;
        if (dh < -HEAP_DIFF_THRESHOLD || dh > HEAP_DIFF_THRESHOLD) {
            any_changed = true;
        }
        int32_t dp = (int32_t)s_prev.free_psram - (int32_t)snapshot.free_psram;
        if (dp < -PSRAM_DIFF_THRESHOLD || dp > PSRAM_DIFF_THRESHOLD) {
            any_changed = true;
        }
        int32_t db = (int32_t)s_prev.batt_pct - (int32_t)snapshot.batt_pct;
        if (db <= -BATT_DIFF_THRESHOLD || db >= BATT_DIFF_THRESHOLD) {
            any_changed = true;
        }
        if (s_prev.widget_count != snapshot.widget_count) any_changed = true;
        int32_t dr = (int32_t)s_prev.rssi_x4 - (int32_t)snapshot.rssi_x4;
        if (dr < -RSSI_DIFF_THRESHOLD || dr > RSSI_DIFF_THRESHOLD) {
            any_changed = true;
        }
    } else {
        any_changed = true;
    }

    if (any_changed) {
        emit_tag(tag::HB_DIFF);
        emit_sep(); emit_kv_u(key::T, millis() / 1000);
        emit_sep(); emit_kv_u(key::H, snapshot.free_heap);
        emit_sep(); emit_kv_u(key::P, snapshot.free_psram);
        emit_sep(); emit_kv_u(key::B, snapshot.batt_pct);
        emit_sep(); emit_kv(key::RSSI, snapshot.rssi_x4 / 4);
        emit_sep(); emit_kv_u(key::WIDGETS, snapshot.widget_count);
        emit_sep(); emit_kv_u(key::WIDGETS_TRUNCATED,
                              snapshot.widget_count_truncated ? 1U : 0U);
        emit_end();
    }

    s_prev = snapshot;
    retain_heartbeat(snapshot, true);
}

// ── Drift update ──────────────────────────────────────

static void update_drift(uint32_t now, uint32_t heap, uint32_t psram, int16_t rssi_x4) {
    if (!s_heap_drift.active) {
        s_heap_drift.begin((int32_t)heap, now, s_drift_window_ms, 4096);
    }
    if (!s_psram_drift.active) {
        s_psram_drift.begin((int32_t)psram, now, s_drift_window_ms, 16384);
    }
    if (!s_rssi_drift.active) {
        s_rssi_drift.begin((int32_t)rssi_x4, now, s_drift_window_ms, 48);
    }

    s_heap_drift.update((int32_t)heap);
    s_psram_drift.update((int32_t)psram);
    s_rssi_drift.update((int32_t)rssi_x4);

    if (s_heap_drift.window_elapsed(now)) {
        if (s_heap_drift.should_report(now)) {
            emit_tag(tag::DRIFT);
            emit_sep(); emit_kv_s(key::METRIC, "heap");
            emit_sep(); emit_kv_u(key::BASELINE, (uint32_t)s_heap_drift.baseline);
            emit_sep(); emit_kv_u(key::CURRENT, (uint32_t)s_heap_drift.current);
            emit_sep(); emit_kv_u(key::DELTA, (uint32_t)s_heap_drift.delta());
            emit_sep(); emit_kv_s(key::TREND, s_heap_drift.trend_str());
            emit_end();
        }
        s_heap_drift.rotate(now);
    }
    if (s_psram_drift.window_elapsed(now)) {
        if (s_psram_drift.should_report(now)) {
            emit_tag(tag::DRIFT);
            emit_sep(); emit_kv_s(key::METRIC, "psram");
            emit_sep(); emit_kv_u(key::BASELINE, (uint32_t)s_psram_drift.baseline);
            emit_sep(); emit_kv_u(key::CURRENT, (uint32_t)s_psram_drift.current);
            emit_sep(); emit_kv_u(key::DELTA, (uint32_t)s_psram_drift.delta());
            emit_sep(); emit_kv_s(key::TREND, s_psram_drift.trend_str());
            emit_end();
        }
        s_psram_drift.rotate(now);
    }
    if (s_rssi_drift.window_elapsed(now)) {
        if (s_rssi_drift.should_report(now)) {
            emit_tag(tag::DRIFT);
            emit_sep(); emit_kv_s(key::METRIC, "rssi");
            emit_sep(); emit_kv(key::BASELINE, s_rssi_drift.baseline / 4);
            emit_sep(); emit_kv(key::CURRENT, s_rssi_drift.current / 4);
            emit_sep(); emit_kv(key::DELTA, s_rssi_drift.delta() / 4);
            emit_sep(); emit_kv_s(key::TREND, s_rssi_drift.trend_str());
            emit_end();
        }
        s_rssi_drift.rotate(now);
    }
}

// ── Public API ────────────────────────────────────────

void init() {
    // Initialise packet log buffer in PSRAM (DRAM fallback)
    init_pktlog();

    const bool hb_ring_ready = hb_ring_init();

    // Initialise crash subsystem (checks RTC memory for previous crash)
    crash::init();

    // Announce telemetry active
    if (hb_ring_ready) {
        emit_record1_s(tag::OK, key::CMD, "init");
    } else {
        emit_err("init", "hb_ring_alloc_failed");
    }
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
    if (s_enabled && elapsed_us > 500000) {
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
    if (!s_enabled) return;
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
    if (!s_enabled) return;
    emit_tag(tag::ALERT);
    emit_sep();
    emit_kv_s(key::DESC, "display_sleep");
    emit_sep();
    emit_kv_u(key::DISP_SLEEP, (uint32_t)s_sleep_count);
    emit_end();
}

// ── Phase 2+3+4 hook implementations ───────────────────

void report_key_event(uint8_t keycode) {
    input::report_key_event(keycode, s_enabled);
}

void report_touch_event(uint16_t x, uint16_t y) {
    input::report_touch_event(x, y, s_enabled);
}

void report_trackball_event(uint8_t direction) {
    input::report_trackball_event(direction, s_enabled);
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
    if (!is_supported_query(arg)) {
        emit_err(arg, "unknown_query");
        return;
    }

    uint32_t start = micros();
    uint32_t n = 0;

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
        emit_sep(); emit_kv(key::RSSI, sigurdos::mesh::getLastRSSI());
        emit_sep(); emit_kv(key::SNR, (int32_t)(sigurdos::mesh::getLastSNR() * 10));
        const WidgetTreeCount heap_widgets = count_lvgl_widgets();
        emit_sep(); emit_kv_u(key::WIDGETS, heap_widgets.count);
        emit_sep(); emit_kv_u(key::WIDGETS_TRUNCATED,
                              heap_widgets.truncated ? 1U : 0U);
        emit_end();
        n++;

        // @lvgl
        emit_tag(tag::LVGL);
        {   const WidgetTreeCount widgets = count_lvgl_widgets();
            emit_sep(); emit_kv_u(key::WIDGETS, widgets.count);
            emit_sep(); emit_kv_u(key::WIDGETS_TRUNCATED,
                                  widgets.truncated ? 1U : 0U);
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
        emit_sep(); emit_kv(key::RSSI, sigurdos::mesh::getLastRSSI());
        emit_sep(); emit_kv(key::SNR, (int32_t)(sigurdos::mesh::getLastSNR() * 10));
        emit_sep(); emit_kv(key::NOISE, sigurdos::mesh::getNoiseFloor());
        emit_sep(); emit_kv_u(key::TX, sigurdos::mesh::getNumSentFlood() + sigurdos::mesh::getNumSentDirect());
        emit_sep(); emit_kv_u(key::RX, sigurdos::mesh::getNumRecvFlood() + sigurdos::mesh::getNumRecvDirect());
        emit_end();
        n++;

        // @mesh_chan — per-channel mesh stats
        n += emit_mesh_chan_stats();

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
        emit_sep(); emit_kv(key::RSSI, sigurdos::mesh::getLastRSSI());
        const WidgetTreeCount hb_widgets = count_lvgl_widgets();
        emit_sep(); emit_kv_u(key::WIDGETS, hb_widgets.count);
        emit_sep(); emit_kv_u(key::WIDGETS_TRUNCATED,
                              hb_widgets.truncated ? 1U : 0U);
        emit_sep(); emit_kv_s(key::SCREEN, screen_name_str(s_active_screen));
        emit_sep(); emit_kv_u(key::SCREEN_MS, s_screen_birth_ms ? (millis() - s_screen_birth_ms) : 0);
        emit_sep(); emit_kv_u(key::LOOP_US, s_last_loop_us);
        emit_sep(); emit_kv_u(key::PEAK_US, s_peak_loop_us);
        emit_sep(); emit_kv_u(key::DISP, (uint32_t)(s_display_on ? 1 : 0));
        emit_sep(); emit_kv_u(key::DISP_WAKE, (uint32_t)s_wake_count);
        emit_sep(); emit_kv_u(key::DISP_SLEEP, (uint32_t)s_sleep_count);
        emit_end();
        n++;

        emit_end_resp(arg, n, micros() - start);
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
        emit_sep(); emit_kv(key::RSSI, sigurdos::mesh::getLastRSSI());
        emit_sep(); emit_kv(key::SNR, (int32_t)(sigurdos::mesh::getLastSNR() * 10));
        const WidgetTreeCount heap_widgets = count_lvgl_widgets();
        emit_sep(); emit_kv_u(key::WIDGETS, heap_widgets.count);
        emit_sep(); emit_kv_u(key::WIDGETS_TRUNCATED,
                              heap_widgets.truncated ? 1U : 0U);
        emit_end();
        n = 1;
    }

    if (strcmp(arg, "state") == 0 || strcmp(arg, "lvgl") == 0) {
        emit_tag(tag::LVGL);
        const WidgetTreeCount widgets = count_lvgl_widgets();
        emit_sep(); emit_kv_u(key::WIDGETS, widgets.count);
        emit_sep(); emit_kv_u(key::WIDGETS_TRUNCATED,
                              widgets.truncated ? 1U : 0U);
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
        emit_sep(); emit_kv(key::RSSI, sigurdos::mesh::getLastRSSI());
        emit_sep(); emit_kv(key::SNR, (int32_t)(sigurdos::mesh::getLastSNR() * 10));
        emit_sep(); emit_kv(key::NOISE, sigurdos::mesh::getNoiseFloor());
        emit_sep(); emit_kv_u(key::TX, sigurdos::mesh::getNumSentFlood() + sigurdos::mesh::getNumSentDirect());
        emit_sep(); emit_kv_u(key::RX, sigurdos::mesh::getNumRecvFlood() + sigurdos::mesh::getNumRecvDirect());
        emit_end();
        n = (strcmp(arg, "mesh") == 0) ? 1 : 3;
        // Per-channel stats for explicit \"mesh\" query
        if (strcmp(arg, "mesh") == 0) {
            n += emit_mesh_chan_stats();
        }
    }

    if (strcmp(arg, "drift") == 0) {
        if (s_heap_drift.active) {
            emit_tag(tag::DRIFT);
            emit_sep(); emit_kv_s(key::METRIC, "heap");
            emit_sep(); emit_kv_u(key::BASELINE, (uint32_t)s_heap_drift.baseline);
            emit_sep(); emit_kv_u(key::CURRENT,  (uint32_t)s_heap_drift.current);
            emit_sep(); emit_kv(key::DELTA, s_heap_drift.delta());
            emit_sep(); emit_kv_s(key::TREND,    s_heap_drift.trend_str());
            emit_end();
            n++;
        }
        if (s_psram_drift.active) {
            emit_tag(tag::DRIFT);
            emit_sep(); emit_kv_s(key::METRIC, "psram");
            emit_sep(); emit_kv_u(key::BASELINE, (uint32_t)s_psram_drift.baseline);
            emit_sep(); emit_kv_u(key::CURRENT,  (uint32_t)s_psram_drift.current);
            emit_sep(); emit_kv(key::DELTA, s_psram_drift.delta());
            emit_sep(); emit_kv_s(key::TREND,    s_psram_drift.trend_str());
            emit_end();
            n++;
        }
        if (s_rssi_drift.active) {
            emit_tag(tag::DRIFT);
            emit_sep(); emit_kv_s(key::METRIC, "rssi");
            emit_sep(); emit_kv(key::BASELINE, s_rssi_drift.baseline / 4);
            emit_sep(); emit_kv(key::CURRENT, s_rssi_drift.current / 4);
            emit_sep(); emit_kv(key::DELTA, s_rssi_drift.delta() / 4);
            emit_sep(); emit_kv_s(key::TREND, s_rssi_drift.trend_str());
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
        n = hb_ring_query(20);
    }
    if (strcmp(arg, "pktlog") == 0) {
        n = emit_pktlog();
    }

    if (strcmp(arg, "crash") == 0) {
        crash::query();
        return;
    } else if (strncmp(arg, "crash ", 6) == 0) {
        const char* sub = arg + 6;
        if (strcmp(sub, "clear") == 0) {
            crash::clear();
            emit_end_resp("crash clear", 1, micros() - start);
            return;
        } else if (strcmp(sub, "test") == 0) {
            crash::test();
            return;
        }
    }

    emit_end_resp(arg, n, micros() - start);
}

void cmd_crash(const char* arg) {
    if (!arg || strcmp(arg, "report") == 0) {
        crash::query();
    } else if (strcmp(arg, "clear") == 0) {
        crash::clear();
        emit_end_resp("crash clear", 1);
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
