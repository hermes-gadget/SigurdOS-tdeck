// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Crash capture subsystem — stores reset reason and backtrace in RTC slow
// memory so the record survives deep sleep and soft resets.
//
// Gated behind SIGURDOS_TELEMETRY. When 0, all functions are no-ops.

#ifndef SIGURDOS_TELEMETRY_CRASH_H
#define SIGURDOS_TELEMETRY_CRASH_H

#include <cstdint>

namespace sigurdos {
namespace telemetry {
namespace crash {

// ── RTC Crash Record ───────────────────────────────────
// Stored in RTC slow memory, survives deep sleep.
struct __attribute__((packed)) RtcCrashRecord {
    uint32_t magic;           // 0xCR4SH to validate
    uint8_t  reset_reason;    // ESP reset reason code
    uint32_t crash_pc;        // Program counter at crash
    uint32_t crash_timestamp; // millis() at crash
    uint16_t backtrace_pcs[8]; // Backtrace frames (truncated)
    uint8_t  backtrace_count;
    uint8_t  reserved[9];
};

// ── Public API ─────────────────────────────────────────

// Call once at boot. Checks RTC memory for a valid crash record
// and auto-reports it if found. Registers the shutdown handler.
void init();

// Emit the crash record as @crash + @bt lines.
void query();

// Clear (reset) the crash record.
void clear();

// Trigger a controlled crash for testing purposes.
void test();

// Returns true if a valid crash record exists in RTC memory.
bool has_record();

// Get pointer to the shared RTC record (for init/clear).
RtcCrashRecord* get_record();

}  // namespace crash
}  // namespace telemetry
}  // namespace sigurdos

#endif // SIGURDOS_TELEMETRY_CRASH_H
