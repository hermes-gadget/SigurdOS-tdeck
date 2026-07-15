// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Peripheral state collectors — read hardware state and emit
// structured telemetry records. All code is gated behind
// SIGURDOS_TELEMETRY.

#include "telemetry_collectors.h"
#include "telemetry_protocol.h"
#include "debug_cfg.h"

#if SIGURDOS_TELEMETRY

#include <Arduino.h>
#include <cstring>

// Hardware abstraction headers
#include "../hal/wifi_ota.h"     // sigurdos::wifi_sta, sigurdos::ota
#include "../hal/gps.h"          // sigurdos_gps_*()
#include "../hal/sdcard.h"       // sigurdos_sdcard_*()
#include "../hal/prefs.h"        // sigurdos::prefs_get(), NodePrefs
#include "../mesh/mesh_wrapper.h" // sigurdos::mesh::get*()

// ESP-IDF / FreeRTOS
#include <nvs.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace sigurdos {
namespace telemetry {
namespace collectors {

// ── Collector implementations ──────────────────────────

WifiSnapshot collect_wifi() {
    WifiSnapshot snap = {};
    snap.sta_connected = sigurdos::wifi_sta::isConnected();
    snap.sta_rssi = snap.sta_connected ? sigurdos::wifi_sta::getRSSI() : 0;
    snap.ota_active = sigurdos::ota::isActive();
    return snap;
}

GpsSnapshot collect_gps() {
    GpsSnapshot snap = {};
    snap.has_fix     = sigurdos_gps_has_fix();
    snap.fix_quality = sigurdos_gps_fix_quality();
    snap.satellites  = sigurdos_gps_satellites();
    snap.latitude    = sigurdos_gps_latitude();
    snap.longitude   = sigurdos_gps_longitude();
    snap.altitude_m  = sigurdos_gps_altitude_m();
    snap.speed_kn    = sigurdos_gps_speed_kn();
    snap.heading     = sigurdos_gps_heading();
    snap.active_baud = sigurdos_gps_active_baud();
    snap.chars_processed = sigurdos_gps_chars_processed();
    snap.sentences_received = sigurdos_gps_sentences_received();
    snap.valid_sentences = sigurdos_gps_valid_sentences();
    snap.checksum_failures = sigurdos_gps_checksum_failures();
    snap.baud_switches = sigurdos_gps_baud_switches();
    return snap;
}

SdCardSnapshot collect_sd() {
    SdCardSnapshot snap = {};
    snap.mounted = sigurdos_sdcard_mounted();
    if (snap.mounted) {
        snap.capacity_bytes = sigurdos_sdcard_capacity_bytes();
        snap.free_bytes     = sigurdos_sdcard_free_bytes();
    }
    return snap;
}

LoRaSnapshot collect_radio() {
    LoRaSnapshot snap = {};
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    snap.frequency_mhz     = p.freq;
    snap.bandwidth_khz     = p.bw;
    snap.spreading_factor  = p.sf;
    snap.coding_rate       = p.cr;
    snap.tx_power_dbm      = p.tx_power_dbm;
    snap.total_tx_airtime_ms = sigurdos::mesh::getTotalTxAirtimeMs();
    snap.total_rx_airtime_ms = sigurdos::mesh::getTotalRxAirtimeMs();
    snap.tx_packets        = sigurdos::mesh::getNumSentFlood()
                           + sigurdos::mesh::getNumSentDirect();
    snap.rx_packets        = sigurdos::mesh::getNumRecvFlood()
                           + sigurdos::mesh::getNumRecvDirect();
    return snap;
}

NvsSnapshot collect_nvs() {
    NvsSnapshot snap = {};
    nvs_stats_t stats;
    esp_err_t err = nvs_get_stats(NULL, &stats);
    if (err == ESP_OK) {
        snap.valid         = true;
        snap.used_entries  = stats.used_entries;
        snap.total_entries = stats.total_entries;
        snap.free_entries  = stats.free_entries;
    }
    return snap;
}

float collect_temp() {
    // ESP32 internal temperature sensor via Arduino framework
    return temperatureRead();
}

bool collect_current_task_watermark(TaskWatermark* out) {
    // uxTaskGetSystemState requires trace facilities that are not enabled in
    // this build. Report the current telemetry task explicitly instead.
    if (!task_watermark_output_valid(out)) return false;
    
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    strncpy(out[0].name, pcTaskGetName(self), sizeof(out[0].name) - 1);
    out[0].name[sizeof(out[0].name) - 1] = '\0';
    out[0].stack_hwm = uxTaskGetStackHighWaterMark(self);
    out[0].priority  = uxTaskPriorityGet(self);
    out[0].state     = (uint8_t)eTaskGetState(self);
    return true;
}

// ── Emission functions ─────────────────────────────────

void collect_and_emit_wifi() {
    WifiSnapshot wifi = collect_wifi();
    emit_tag(tag::WIFI);
    emit_sep();
    emit_kv_u(key::WIFI_CONN, wifi.sta_connected ? 1 : 0);
    if (wifi.sta_connected) {
        emit_sep();
        emit_kv(key::W_RSSI, wifi.sta_rssi);
    }
    emit_sep();
    emit_kv_u("ota", wifi.ota_active ? 1 : 0);
    emit_end();
}

void collect_and_emit_gps() {
    GpsSnapshot gps = collect_gps();
    emit_tag(tag::GPS);
    emit_sep();
    emit_kv_u(key::GPS_FIX, gps.has_fix ? 1 : 0);
    emit_sep();
    emit_kv_u("qual", gps.fix_quality);
    emit_sep();
    emit_kv_u(key::GPS_SV, gps.satellites);
    emit_sep();
    emit_kv_u("baud", gps.active_baud);
    emit_sep();
    emit_kv_u("chars", gps.chars_processed);
    emit_sep();
    emit_kv_u("sent", gps.sentences_received);
    emit_sep();
    emit_kv_u("valid", gps.valid_sentences);
    emit_sep();
    emit_kv_u("csfail", gps.checksum_failures);
    emit_sep();
    emit_kv_u("sw", gps.baud_switches);
    if (gps.has_fix) {
        emit_sep();
        emit_kv_f(key::GPS_LAT, gps.latitude, 4);
        emit_sep();
        emit_kv_f(key::GPS_LON, gps.longitude, 4);
        emit_sep();
        emit_kv_f(key::GPS_ALT, gps.altitude_m, 1);
        emit_sep();
        emit_kv_f("spd", gps.speed_kn, 1);
        emit_sep();
        emit_kv_f("hdg", gps.heading, 1);
    }
    emit_end();
}

void collect_and_emit_radio() {
    LoRaSnapshot radio = collect_radio();
    emit_tag(tag::RADIO);
    emit_sep();
    emit_kv_f(key::RADIO_FREQ, radio.frequency_mhz, 3);
    emit_sep();
    emit_kv_f(key::RADIO_BW, radio.bandwidth_khz, 1);
    emit_sep();
    emit_kv_u(key::RADIO_SF, radio.spreading_factor);
    emit_sep();
    emit_kv_u("cr", radio.coding_rate);
    emit_sep();
    emit_kv(key::RADIO_TXP, radio.tx_power_dbm);
    emit_sep();
    emit_kv_u(key::RADIO_AIRTXTOTAL, radio.total_tx_airtime_ms);
    emit_sep();
    emit_kv_u("air_rx", radio.total_rx_airtime_ms);
    emit_sep();
    emit_kv_u("pk_tx", radio.tx_packets);
    emit_sep();
    emit_kv_u("pk_rx", radio.rx_packets);
    emit_end();
}

void collect_and_emit_sd() {
    SdCardSnapshot sd = collect_sd();
    emit_tag(tag::SD);
    emit_sep();
    emit_kv_u(key::SD_MOUNT, sd.mounted ? 1 : 0);
    if (sd.mounted) {
        emit_sep();
        emit_kv_u64(key::SD_FREE, sd.free_bytes);
        emit_sep();
        emit_kv_u64("cap", sd.capacity_bytes);
    }
    emit_end();
}

void collect_and_emit_nvs() {
    NvsSnapshot nvs = collect_nvs();
    emit_tag(tag::NVS);
    emit_sep();
    emit_kv_u("valid", nvs.valid ? 1U : 0U);
    if (nvs.valid) {
        emit_sep();
        emit_kv_u(key::NVS_USED, nvs.used_entries);
        emit_sep();
        emit_kv_u(key::NVS_TOTAL, nvs.total_entries);
        emit_sep();
        emit_kv_u(key::NVS_FREE, nvs.free_entries);
    }
    emit_end();
}

void collect_and_emit_temp() {
    float temp_c = collect_temp();
    // Emit as integer: temp * 10 (e.g. 32.5°C -> 325)
    int32_t temp_int = (int32_t)(temp_c * 10.0f);
    emit_tag(tag::TEMP);
    emit_sep();
    emit_kv(key::TEMP_VAL, temp_int);
    emit_end();
}

void collect_and_emit_tasks() {
    TaskWatermark task{};
    if (!collect_current_task_watermark(&task)) return;

    emit_tag(tag::TASK);
    emit_sep();
    emit_kv_s("scope", "current");
    emit_sep();
    emit_kv_s(key::TASK_NAME, task.name);
    emit_sep();
    emit_kv_u(key::STACK, task.stack_hwm);
    emit_sep();
    emit_kv_u(key::TASK_PRIO, task.priority);
    emit_sep();
    emit_kv_u(key::TASK_STATE, task.state);
    emit_end();
}

void collect_and_emit_all_peripherals() {
    collect_and_emit_wifi();
    collect_and_emit_gps();
    collect_and_emit_radio();
    collect_and_emit_sd();
    collect_and_emit_nvs();
    collect_and_emit_temp();
    collect_and_emit_tasks();
}

}  // namespace collectors
}  // namespace telemetry
}  // namespace sigurdos

#endif // SIGURDOS_TELEMETRY
