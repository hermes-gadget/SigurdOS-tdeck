// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Telemetry wire protocol constants and serial helpers.
// Every telemetry line starts with '@' — no freeform printf.
// Format: @<tag>|<key>=<value>|...\n
// String values are percent-encoded. '%', '|', '=', '*', CR, LF, and other
// ASCII control bytes are emitted as %HH; '*' by itself means missing/empty.

#ifndef SIGURDOS_TELEMETRY_PROTOCOL_H
#define SIGURDOS_TELEMETRY_PROTOCOL_H

#include <cstdint>
#include <cstddef>

namespace sigurdos {
namespace telemetry {

// ── Record tag constants (PROGMEM) ────────────────────
namespace tag {
    extern const char HB[];       // @hb  — full heartbeat
    extern const char HB_DIFF[];  // @hb+ — diff heartbeat
    extern const char HEAP[];     // @heap
    extern const char BATT[];     // @batt
    extern const char PINS[];     // @pins
    extern const char LVGL[];     // @lvgl
    extern const char MESH[];     // @mesh
    extern const char DRIFT[];    // @drift
    extern const char ALERT[];    // @alert
    extern const char CRASH[];    // @crash
    extern const char BT[];       // @bt   — backtrace entry
    extern const char TASK[];     // @task
    extern const char HB_RING[];  // @hb-ring
    extern const char OK[];       // @ok   — query ack
    extern const char ERR[];      // @err  — query error
    extern const char END[];      // @end  — multi-line response end
    extern const char SCREEN[];   // @screen — screen telemetry
    extern const char BUILD[];    // @build — firmware build identity

    // Phase 2+3+4 tags
    extern const char WIFI[];      // @wifi — WiFi STA state
    extern const char GPS[];       // @gps  — GPS fix data
    extern const char RADIO[];     // @radio — LoRa radio config & counters
    extern const char SD[];        // @sd   — SD card state
    extern const char NVS[];       // @nvs  — NVS usage
    extern const char TEMP[];      // @temp — internal temperature
    extern const char PKT[];       // @pkt  — packet event
    extern const char MESH_CHAN[]; // @mesh_chan — per-channel mesh stats
}

// ── Field key constants (PROGMEM) ─────────────────────
namespace key {
    extern const char T[];        // t    — uptime seconds
    extern const char H[];        // h    — free heap
    extern const char HM[];       // hm   — min free heap
    extern const char P[];        // p    — free PSRAM
    extern const char PM[];       // pm   — min PSRAM
    extern const char B[];        // b    — battery %
    extern const char MV[];       // mv   — battery mV
    extern const char FEAT[];     // feat — feature mask
    extern const char RSSI[];     // rssi
    extern const char SNR[];      // snr
    extern const char NOISE[];    // noise
    extern const char TX[];       // pk_tx
    extern const char RX[];       // pk_rx
    extern const char ERR_CNT[];  // err
    extern const char WIDGETS[];  // wt
    extern const char RENDERS[];  // rd
    extern const char LOOP_US[];  // loop_us
    extern const char STACK[];    // stack HWM
    extern const char METRIC[];   // metric name (drift)
    extern const char BASELINE[]; // baseline value
    extern const char CURRENT[];  // current value
    extern const char DELTA[];    // delta
    extern const char TREND[];    // trend (stable/rising/falling)
    extern const char REASON[];   // crash reason code
    extern const char DESC[];     // description
    extern const char PC[];       // program counter
    extern const char I[];        // index
    extern const char CMD[];      // command name
    extern const char COST_US[];  // query cost µs
    extern const char N[];        // n    — count

    // Phase 1 telemetry keys
    extern const char PEAK_US[];    // peak_us
    extern const char SCREEN[];      // scr
    extern const char SCREEN_MS[];   // sc_ms
    extern const char SCREEN_CNT[];  // sc_cnt
    extern const char DISP[];        // disp
    extern const char DISP_WAKE[];   // disp_w
    extern const char DISP_SLEEP[];  // disp_s
    extern const char NAME[];        // name

    // Phase 2+3+4 keys
    extern const char WIFI_CONN[];        // con     — WiFi connected 0/1
    extern const char W_RSSI[];           // w_rssi  — WiFi RSSI dBm
    extern const char GPS_FIX[];          // fix     — GPS has fix 0/1
    extern const char GPS_SV[];           // sv      — GPS satellites in view
    extern const char GPS_LAT[];          // lat     — GPS latitude
    extern const char GPS_LON[];          // lon     — GPS longitude
    extern const char GPS_ALT[];          // alt     — GPS altitude (m)
    extern const char SD_MOUNT[];         // mnt     — SD mounted 0/1
    extern const char SD_FREE[];          // free    — SD free bytes
    extern const char NVS_USED[];         // used_entries — used NVS entries
    extern const char NVS_TOTAL[];        // total_entries — total NVS entries
    extern const char NVS_FREE[];         // free_entries — free NVS entries
    extern const char TEMP_VAL[];         // val     — temperature * 10 as int
    extern const char TASK_NAME[];        // name    — FreeRTOS task name
    extern const char TASK_PRIO[];        // prio    — task priority
    extern const char TASK_STATE[];       // state   — task state enum
    extern const char RADIO_FREQ[];       // freq    — frequency MHz
    extern const char RADIO_SF[];         // sf      — spreading factor
    extern const char RADIO_BW[];         // bw      — bandwidth kHz
    extern const char RADIO_TXP[];        // txp     — TX power dBm
    extern const char RADIO_AIRTXTOTAL[]; // air_tx  — total TX airtime ms
    extern const char CHAN[];             // chan    — channel name
    extern const char SRC[];              // src     — source node
    extern const char TEXT[];             // text    — message text
    extern const char TYPE[];             // type    — event type
    extern const char FW[];               // fw      — firmware version
    extern const char GIT[];              // git     — firmware git SHA
    extern const char DIRTY[];            // dirty   — git tree dirty 0/1
    extern const char MESHCORE[];         // mcore   — MeshCore submodule SHA
    extern const char ENV[];              // env     — PlatformIO environment
    extern const char PART[];             // part    — partition table
    extern const char BOARD[];            // board   — PlatformIO board
    extern const char MCU[];              // mcu     — target MCU
}

// ── Serial emission helpers ───────────────────────────

// Start a telemetry record: emits "@tag"
void emit_tag(const char* tag);

// Emit a key=value pair with integer value (no leading '|')
void emit_kv(const char* key, int32_t val);

// Emit a key=value pair with unsigned integer
void emit_kv_u(const char* key, uint32_t val);

// Emit a key=value pair with 64-bit unsigned integer
void emit_kv_u64(const char* key, uint64_t val);

// Emit a key=value pair with string value
void emit_kv_s(const char* key, const char* val);

// Emit a key=value pair with float value (formatted to 1 decimal place)
void emit_kv_f(const char* key, float val, int precision = 1);

// Emit field separator '|'. Call after emit_tag, before each emit_kv.
void emit_sep();

// End the current record: emits "\n"
void emit_end();

// ── Composite emitters ────────────────────────────────

// Emit a single-field record: "@tag|key=val\n"
void emit_record1(const char* tag, const char* key, int32_t val);

// Emit a single-field unsigned record
void emit_record1_u(const char* tag, const char* key, uint32_t val);

// Emit a single-field string record
void emit_record1_s(const char* tag, const char* key, const char* val);

// Emit "@ok|cmd=<cmd>|cost_us=<us>\n"
void emit_ok(const char* cmd, uint32_t cost_us);

// Emit "@err|cmd=<cmd>|desc=<desc>\n"
void emit_err(const char* cmd, const char* desc);

// Emit the single terminal success record for a query.
// Format: "@end|cmd=<cmd>|n=<count>|cost_us=<us>\n"
void emit_end_resp(const char* cmd, uint32_t count, uint32_t cost_us = 0);

}  // namespace telemetry
}  // namespace sigurdos

#endif
