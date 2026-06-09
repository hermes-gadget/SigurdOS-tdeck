// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Crash capture subsystem implementation.
// Uses RTC slow memory for crash record persistence across deep sleep.

#if SIGURDOS_TELEMETRY

#include "telemetry_crash.h"
#include "telemetry_protocol.h"
#include <Arduino.h>
#include <cstring>
#include <esp_system.h>

namespace sigurdos {
namespace telemetry {
namespace crash {

// ── RTC memory for crash record ────────────────────────
// RTC_DATA_ATTR places the record in RTC slow memory which survives
// deep sleep and soft resets (but not power-on reset or brownout).
static RTC_DATA_ATTR RtcCrashRecord g_crash_record;

static constexpr uint32_t CRASH_MAGIC = 0x43523453;  // "CR4S" little-endian

// ── Backtrace capture using frame pointer walk ─────────
// Walks the Xtensa/RISC-V frame pointer chain to capture return addresses.
// Stores low 16 bits of each PC (address space is well below 0x1FFFF for
// embedded flash, so the high bits are recoverable from the binary).
//
// On ESP32-S3 (Xtensa): a1 = stack pointer, a0 = return address
// Frame layout: [prev_sp, return_addr, ...]
// We walk: sp = *sp, pc = *(sp+4) (or sp[1] on 32-bit)
//
// Returns number of frames captured (max 8).
static int capture_backtrace(uint16_t* out_pcs, int max_frames) {
    if (!out_pcs || max_frames <= 0) return 0;

    int count = 0;
    uint32_t* sp;

    // Read stack pointer register
#if defined(__XTENSA__)
    __asm__ volatile("mov %0, a1" : "=r"(sp));
#else
    // Fallback: use a dummy frame
    sp = (uint32_t*)__builtin_frame_address(0);
#endif

    // Walk up to max_frames levels
    for (int i = 0; i < max_frames && count < max_frames; i++) {
        // Sanity check: SP should be in DRAM region (0x3FC8_0000 - 0x3FFF_FFFF)
        // or internal SRAM (0x3FF0_0000+)
        uint32_t sp_addr = (uint32_t)sp;
        if (sp_addr < 0x3F000000 || sp_addr > 0x40000000) break;
        // Xtensa requires 4-byte aligned stack pointer — unaligned
        // access triggers Hardware Exception (double-fault) in crash handler
        if (sp_addr & 0x3) break;

        // The return address is at sp[1] (caller's a0 saved by ENTRY instruction)
        uint32_t ret_addr = sp[1];
        if (ret_addr == 0 || ret_addr == 0xFFFFFFFF) break;

        out_pcs[count++] = (uint16_t)(ret_addr & 0xFFFF);

        // Move to previous frame: sp[0] is the previous frame pointer
        sp = (uint32_t*)(sp[0]);

        // Check we're making progress (avoid infinite loops)
        if ((uint32_t)sp <= sp_addr) break;
    }

    return count;
}

// ── Shutdown handler ───────────────────────────────────
// Registered via esp_register_shutdown_handler().
// Called before esp_restart() — captures backtrace and saves to RTC memory.
//
// ⚠️ LIMITATION: This is a SHUTDOWN handler, not a PANIC handler.
// It fires AFTER the default panic handler has already processed the
// exception and called esp_restart(). The captured PC and backtrace
// reflect the shutdown/restart path, NOT the actual crash site.
// Debugging from telemetry crash records is therefore unreliable.
//
// FIXME: Replace with esp_panic_handler_register_with_id() to capture
// the actual exception context (ESP-IDF v5.1+ API).
// See: ESP-IDF Panic Handler documentation.
// Requires: #include <esp_private/panic_reason.h> or similar.
static void IRAM_ATTR crash_shutdown_handler(void) {
    g_crash_record.magic = CRASH_MAGIC;
    g_crash_record.reset_reason = (uint8_t)esp_reset_reason();
    g_crash_record.crash_pc = (uint32_t)__builtin_return_address(0);
    g_crash_record.crash_timestamp = (uint32_t)(esp_timer_get_time() / 1000);  // hw timer, safe during shutdown
    g_crash_record.backtrace_count = (uint8_t)capture_backtrace(
        g_crash_record.backtrace_pcs, RTC_CRASH_BACKTRACE_CAPACITY);
}

// ── Public API ─────────────────────────────────────────

void init() {
    // Register shutdown handler (non-critical; if it fails, crash capture
    // simply won't work — we continue booting)
    esp_err_t err = esp_register_shutdown_handler(&crash_shutdown_handler);
    if (err != ESP_OK) {
        // Shutdown handler registration failed — most likely already registered
        // or called too late. Non-fatal, continue booting.
    }

    // Check if we have a valid crash record from a previous run
    if (has_record()) {
        // Auto-report the existing crash record on boot
        emit_tag(tag::CRASH);
        emit_sep();
        emit_kv_s(key::DESC, "previous crash recovered");
        emit_sep();
        emit_kv_u(key::REASON, g_crash_record.reset_reason);
        emit_sep();
        emit_kv_u(key::PC, g_crash_record.crash_pc);
        emit_end();

        // Emit backtrace frames
        uint8_t bt_count = bounded_backtrace_count(g_crash_record.backtrace_count);
        for (uint8_t i = 0; i < bt_count; i++) {
            emit_tag(tag::BT);
            emit_sep();
            emit_kv_u(key::I, i);
            emit_sep();
            emit_kv_u(key::PC, g_crash_record.backtrace_pcs[i]);
            emit_end();
        }

        // Do NOT clear the record on auto-report — agent can query it again
        // and clear it explicitly with 'query crash clear'
    }
}

void query() {
    if (!has_record()) {
        emit_record1_s(tag::CRASH, key::DESC, "no crash recorded");
        emit_end_resp("crash", 1);
        return;
    }

    // Map reset reason code to human-readable string
    const char* reason_desc = "unknown";
    switch (g_crash_record.reset_reason) {
        case 1:  reason_desc = "power-on reset";    break;
        case 2:  reason_desc = "external reset";     break;
        case 3:  reason_desc = "software reset";     break;
        case 4:  reason_desc = "panic/exception";    break;
        case 5:  reason_desc = "deep sleep wake";    break;
        case 6:  reason_desc = "brownout";           break;
        case 7:  reason_desc = "watchdog";           break;
        default: reason_desc = "unknown";            break;
    }

    emit_tag(tag::CRASH);
    emit_sep();
    emit_kv_u(key::REASON, g_crash_record.reset_reason);
    emit_sep();
    emit_kv_s(key::DESC, reason_desc);
    emit_sep();
    emit_kv_u(key::PC, g_crash_record.crash_pc);
    emit_end();

    uint32_t n = 1;  // CRASH line

    // Emit backtrace frames
    uint8_t bt_count = bounded_backtrace_count(g_crash_record.backtrace_count);
    for (uint8_t i = 0; i < bt_count; i++) {
        emit_tag(tag::BT);
        emit_sep();
        emit_kv_u(key::I, i);
        emit_sep();
        emit_kv_u(key::PC, g_crash_record.backtrace_pcs[i]);
        emit_end();
        n++;
    }

    emit_end_resp("crash", n);
}

void clear() {
    memset(&g_crash_record, 0, sizeof(g_crash_record));
    g_crash_record.magic = 0;
    emit_record1_s(tag::OK, key::CMD, "crash clear");
}

void test() {
    Serial.println("[telemetry] crash test triggered — forcing null pointer dereference");
    Serial.flush();
    // Force a controlled crash via null pointer dereference
    volatile uint32_t* p = (uint32_t*)0;
    *p = 0xDEAD;
}

bool has_record() {
    return g_crash_record.magic == CRASH_MAGIC;
}

RtcCrashRecord* get_record() {
    return &g_crash_record;
}

}  // namespace crash
}  // namespace telemetry
}  // namespace sigurdos

#endif // SIGURDOS_TELEMETRY
