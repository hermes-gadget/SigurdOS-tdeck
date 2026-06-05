// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Peripheral state collectors for structured telemetry.
// Each collector takes a snapshot of its subsystem and emits
// structured @tag|key=value records via the telemetry protocol.
//
// Gated behind SIGURDOS_TELEMETRY — no-ops when disabled.

#ifndef SIGURDOS_TELEMETRY_COLLECTORS_H
#define SIGURDOS_TELEMETRY_COLLECTORS_H

#include <cstdint>
#include <cstddef>

namespace sigurdos {
namespace telemetry {
namespace collectors {

// ── Snapshot Structures ─────────────────────────────────

struct GpsSnapshot {
    bool     has_fix;
    uint8_t  fix_quality;
    uint8_t  satellites;
    float    latitude;
    float    longitude;
    float    altitude_m;
    float    speed_kn;
    float    heading;
    uint32_t active_baud;
    uint32_t chars_processed;
    uint32_t sentences_received;
    uint32_t valid_sentences;
    uint32_t checksum_failures;
    uint32_t baud_switches;
};

struct SdCardSnapshot {
    bool     mounted;
    uint64_t capacity_bytes;
    uint64_t free_bytes;
};

struct WifiSnapshot {
    bool     sta_connected;
    int      sta_rssi;
    bool     ota_active;
};

struct LoRaSnapshot {
    float    frequency_mhz;
    float    bandwidth_khz;
    uint8_t  spreading_factor;
    uint8_t  coding_rate;
    int8_t   tx_power_dbm;
    uint32_t total_tx_airtime_ms;
    uint32_t total_rx_airtime_ms;
    uint32_t tx_packets;
    uint32_t rx_packets;
};

struct NvsSnapshot {
    uint32_t used_bytes;
    uint32_t total_bytes;
    uint32_t entry_count;
};

struct TaskWatermark {
    char     name[24];
    uint32_t stack_hwm;
    uint8_t  priority;
    uint8_t  state;
};

inline bool task_watermark_output_valid(const TaskWatermark* out, uint8_t max) {
    return out && max > 0;
}

// ── Collectors (fill a snapshot from hardware state) ──

WifiSnapshot   collect_wifi();
GpsSnapshot    collect_gps();
SdCardSnapshot collect_sd();
LoRaSnapshot   collect_radio();
NvsSnapshot    collect_nvs();
float          collect_temp();   // returns °C
uint8_t        collect_task_watermarks(TaskWatermark* out, uint8_t max);

// ── Emission functions (emit telemetry records) ────────

void collect_and_emit_wifi();
void collect_and_emit_gps();
void collect_and_emit_radio();
void collect_and_emit_sd();
void collect_and_emit_nvs();
void collect_and_emit_temp();
void collect_and_emit_tasks();
void collect_and_emit_all_peripherals();

}  // namespace collectors
}  // namespace telemetry
}  // namespace sigurdos

#endif // SIGURDOS_TELEMETRY_COLLECTORS_H
