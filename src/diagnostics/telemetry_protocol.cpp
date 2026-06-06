// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Telemetry protocol serial output — minimal, fast, no heap allocation.

#include "telemetry_protocol.h"
#include <Arduino.h>

namespace sigurdos {
namespace telemetry {

// ── Tag constants ─────────────────────────────────────
namespace tag {
    const char HB[]      = "hb";
    const char HB_DIFF[] = "hb+";
    const char HEAP[]    = "heap";
    const char BATT[]    = "batt";
    const char PINS[]    = "pins";
    const char LVGL[]    = "lvgl";
    const char MESH[]    = "mesh";
    const char DRIFT[]   = "drift";
    const char ALERT[]   = "alert";
    const char CRASH[]   = "crash";
    const char BT[]      = "bt";
    const char TASK[]    = "task";
    const char HB_RING[] = "hb-ring";
    const char OK[]      = "ok";
    const char ERR[]     = "err";
    const char END[]     = "end";
    const char SCREEN[]  = "screen";
    const char BUILD[]   = "build";

    // Phase 2+3+4 tags
    const char WIFI[]      = "wifi";
    const char GPS[]       = "gps";
    const char RADIO[]     = "radio";
    const char SD[]        = "sd";
    const char NVS[]       = "nvs";
    const char TEMP[]      = "temp";
    const char PKT[]       = "pkt";
    const char MESH_CHAN[] = "mesh_chan";
}


namespace key {
    const char T[]       = "t";
    const char H[]       = "h";
    const char HM[]      = "hm";
    const char P[]       = "p";
    const char PM[]      = "pm";
    const char B[]       = "b";
    const char MV[]      = "mv";
    const char FEAT[]    = "feat";
    const char RSSI[]    = "rssi";
    const char SNR[]     = "snr";
    const char NOISE[]   = "noise";
    const char TX[]      = "pk_tx";
    const char RX[]      = "pk_rx";
    const char ERR_CNT[] = "err";
    const char WIDGETS[] = "wt";
    const char EVQ[]     = "evq";
    const char RENDERS[] = "rd";
    const char LOOP_US[] = "loop_us";
    const char STACK[]   = "stack";
    const char METRIC[]  = "metric";
    const char BASELINE[] = "baseline";
    const char CURRENT[]  = "current";
    const char DELTA[]    = "delta";
    const char TREND[]    = "trend";
    const char REASON[]   = "reason";
    const char DESC[]     = "desc";
    const char PC[]       = "pc";
    const char I[]        = "i";
    const char CMD[]      = "cmd";
    const char COST_US[]  = "cost_us";
    const char N[]        = "n";

    // Phase 1 keys
    const char PEAK_US[]    = "peak_us";
    const char SCREEN[]     = "scr";
    const char SCREEN_MS[]  = "sc_ms";
    const char SCREEN_CNT[] = "sc_cnt";
    const char DISP[]       = "disp";
    const char DISP_WAKE[]  = "disp_w";
    const char DISP_SLEEP[] = "disp_s";
    const char NAME[]       = "name";

    // Phase 2+3+4 keys
    const char WIFI_CONN[]        = "con";
    const char W_RSSI[]           = "w_rssi";
    const char GPS_FIX[]          = "fix";
    const char GPS_SV[]           = "sv";
    const char GPS_LAT[]          = "lat";
    const char GPS_LON[]          = "lon";
    const char GPS_ALT[]          = "alt";
    const char SD_MOUNT[]         = "mnt";
    const char SD_FREE[]          = "free";
    const char NVS_USED[]         = "nvs_used";
    const char NVS_TOTAL[]        = "nvs_total";
    const char TEMP_VAL[]         = "val";
    const char TASK_NAME[]        = "name";
    const char TASK_PRIO[]        = "prio";
    const char TASK_STATE[]       = "state";
    const char RADIO_FREQ[]       = "freq";
    const char RADIO_SF[]         = "sf";
    const char RADIO_BW[]         = "bw";
    const char RADIO_TXP[]        = "txp";
    const char RADIO_AIRTXTOTAL[] = "air_tx";
    const char CHAN[]             = "chan";
    const char SRC[]              = "src";
    const char TEXT[]             = "text";
    const char TYPE[]             = "type";
    const char FW[]               = "fw";
    const char GIT[]              = "git";
    const char DIRTY[]            = "dirty";
    const char MESHCORE[]         = "mcore";
    const char ENV[]              = "env";
    const char PART[]             = "part";
    const char BOARD[]            = "board";
    const char MCU[]              = "mcu";
}

// ── Helpers ───────────────────────────────────────────

void emit_tag(const char* tag) {
    Serial.print('@');
    Serial.print(tag);
}

void emit_sep() {
    Serial.print('|');
}

void emit_end() {
    Serial.println();
}

void emit_kv(const char* key, int32_t val) {
    Serial.print(key);
    Serial.print('=');
    Serial.print(val);
}

void emit_kv_u(const char* key, uint32_t val) {
    Serial.print(key);
    Serial.print('=');
    Serial.print(val);
}

void emit_kv_s(const char* key, const char* val) {
    Serial.print(key);
    Serial.print('=');
    if (val && val[0]) {
        Serial.print(val);
    } else {
        Serial.print('*');
    }
}

void emit_kv_f(const char* key, float val, int precision) {
    Serial.print(key);
    Serial.print('=');
    // Format float with requested precision (default 1 decimal)
    if (precision == 0) {
        Serial.print((int)val);
    } else {
        // Simple fixed-point formatting without heap allocation
        int32_t scale = 1;
        for (int i = 0; i < precision; i++) scale *= 10;
        const bool neg = (val < 0.0f);
        const float abs_val = neg ? -val : val;
        int32_t whole = (int32_t)abs_val;
        uint32_t frac = (uint32_t)((abs_val - whole) * scale);
        if (neg) Serial.print('-');
        Serial.print(whole);
        Serial.print('.');
        // Zero-pad fractional part
        uint32_t div = scale / 10;
        while (div > frac && div > 1) {
            Serial.print('0');
            div /= 10;
        }
        Serial.print(frac);
    }
}

// ── Composite emitters ────────────────────────────────

void emit_record1(const char* tag, const char* key, int32_t val) {
    emit_tag(tag);
    emit_sep();
    emit_kv(key, val);
    emit_end();
}

void emit_record1_u(const char* tag, const char* key, uint32_t val) {
    emit_tag(tag);
    emit_sep();
    emit_kv_u(key, val);
    emit_end();
}

void emit_record1_s(const char* tag, const char* key, const char* val) {
    emit_tag(tag);
    emit_sep();
    emit_kv_s(key, val);
    emit_end();
}

void emit_ok(const char* cmd, uint32_t cost_us) {
    emit_tag(tag::OK);
    emit_sep();
    emit_kv_s(key::CMD, cmd);
    emit_sep();
    emit_kv_u(key::COST_US, cost_us);
    emit_end();
}

void emit_err(const char* cmd, const char* desc) {
    emit_tag(tag::ERR);
    emit_sep();
    emit_kv_s(key::CMD, cmd);
    emit_sep();
    emit_kv_s(key::DESC, desc);
    emit_end();
}

void emit_end_resp(const char* cmd, uint32_t count) {
    emit_tag(tag::END);
    emit_sep();
    emit_kv_s(key::CMD, cmd);
    emit_sep();
    emit_kv_u(key::N, count);
    emit_end();
}

}  // namespace telemetry
}  // namespace sigurdos
