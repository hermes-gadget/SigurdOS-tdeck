// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Crash capture subsystem — recovers the panic core-dump summary into RTC
// slow memory so the record survives subsequent deep sleep and soft resets.
//
// Gated behind SIGURDOS_TELEMETRY. When 0, all functions are no-ops.

#ifndef SIGURDOS_TELEMETRY_CRASH_H
#define SIGURDOS_TELEMETRY_CRASH_H

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace telemetry {
namespace crash {

static constexpr uint8_t RTC_CRASH_BACKTRACE_CAPACITY = 8;
static constexpr uint8_t RTC_CRASH_REGISTER_COUNT = 16;
static constexpr uint8_t RTC_CRASH_TASK_NAME_CAPACITY = 16;
static constexpr uint8_t RTC_CRASH_ELF_SHA_CAPACITY = 17;

static constexpr uint32_t RTC_CRASH_MAGIC = 0x43523453U;  // "CR4S"
static constexpr uint16_t RTC_CRASH_VERSION = 2;

static constexpr uint8_t RTC_CRASH_FLAG_CORE_DUMP_VALID = 0x01U;
static constexpr uint8_t RTC_CRASH_FLAG_BACKTRACE_CORRUPTED = 0x02U;

// ── RTC Crash Record ───────────────────────────────────
// Stored in RTC slow memory, survives deep sleep.
struct alignas(4) RtcCrashRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint8_t reset_reason;  // esp_reset_reason_t value
    uint8_t backtrace_count;
    uint8_t flags;
    uint8_t reserved0;
    uint32_t crash_pc;
    uint32_t exception_tcb;
    uint32_t exception_cause;
    uint32_t exception_vaddr;
    uint32_t core_dump_version;
    char exception_task[RTC_CRASH_TASK_NAME_CAPACITY];
    char app_elf_sha256[RTC_CRASH_ELF_SHA_CAPACITY];
    uint8_t reserved1[3];
    uint32_t exception_a[RTC_CRASH_REGISTER_COUNT];
    uint32_t backtrace_pcs[RTC_CRASH_BACKTRACE_CAPACITY];
    uint32_t crc32;
};

static_assert(sizeof(RtcCrashRecord) == 168,
              "RTC crash record layout changed; bump its version");
static_assert(alignof(RtcCrashRecord) == 4,
              "RTC crash record must preserve aligned 32-bit accesses");

inline uint8_t bounded_backtrace_count(uint32_t count) {
    return (count > RTC_CRASH_BACKTRACE_CAPACITY)
        ? RTC_CRASH_BACKTRACE_CAPACITY
        : static_cast<uint8_t>(count);
}

inline uint32_t crash_record_crc32(const RtcCrashRecord& record) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < offsetof(RtcCrashRecord, crc32); ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

inline bool crash_record_valid(const RtcCrashRecord& record) {
    return record.magic == RTC_CRASH_MAGIC &&
           record.version == RTC_CRASH_VERSION &&
           record.length == sizeof(RtcCrashRecord) &&
           record.backtrace_count <= RTC_CRASH_BACKTRACE_CAPACITY &&
           record.exception_task[RTC_CRASH_TASK_NAME_CAPACITY - 1] == '\0' &&
           record.app_elf_sha256[RTC_CRASH_ELF_SHA_CAPACITY - 1] == '\0' &&
           record.crc32 == crash_record_crc32(record);
}

// ── Public API ─────────────────────────────────────────

// Call once at boot. Recovers the previous panic's flash core-dump summary,
// then auto-reports the retained RTC record if present.
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
