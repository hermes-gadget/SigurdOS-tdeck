// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Crash capture subsystem implementation.
// Recovers the previous panic's exception context from the ESP-IDF flash
// core dump and retains a compact, validated copy in RTC slow memory.

#if SIGURDOS_TELEMETRY

#include "telemetry_crash.h"
#include "telemetry_protocol.h"
#include <Arduino.h>
#include <cstring>
#include <esp_core_dump.h>
#include <esp_system.h>

#if defined(ESP32_PLATFORM)
#include <sdkconfig.h>

// Arduino is linked against a precompiled ESP-IDF, so project sdkconfig files
// cannot enable these features after the framework has been built. Fail the
// telemetry build if a future framework pin does not provide the exact crash
// capture capabilities this module requires.
#if !defined(CONFIG_ESP_COREDUMP_ENABLE) || !CONFIG_ESP_COREDUMP_ENABLE
#error "Telemetry crash recovery requires ESP-IDF core dump support"
#endif
#if !defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) || !CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#error "Telemetry crash recovery requires flash core dumps"
#endif
#if !defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF) || !CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
#error "Telemetry crash recovery requires ELF core dumps"
#endif
#if !defined(CONFIG_ESP_COREDUMP_CHECKSUM_CRC32) || !CONFIG_ESP_COREDUMP_CHECKSUM_CRC32
#error "Telemetry crash recovery requires core dump integrity checking"
#endif
#if !defined(CONFIG_ESP_COREDUMP_MAX_TASKS_NUM) || CONFIG_ESP_COREDUMP_MAX_TASKS_NUM < 16
#error "Telemetry crash recovery requires at least 16 task snapshots"
#endif
#endif

namespace sigurdos {
namespace telemetry {
namespace crash {

// ── RTC memory for crash record ────────────────────────
// RTC_NOINIT_ATTR prevents startup code from overwriting a record retained
// across a reset. Version, length, bounds, and CRC reject random/stale bytes.
static RTC_NOINIT_ATTR RtcCrashRecord g_crash_record;

static bool is_crash_reset(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            return true;
        default:
            return false;
    }
}

static bool reset_has_core_dump(esp_reset_reason_t reason) {
    return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
}

static const char* reset_reason_description(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "unknown";
        case ESP_RST_POWERON:   return "power-on reset";
        case ESP_RST_EXT:       return "external reset";
        case ESP_RST_SW:        return "software reset";
        case ESP_RST_PANIC:     return "panic/exception";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "SDIO reset";
        default:                return "unknown";
    }
}

static void seal_record(RtcCrashRecord& record) {
    record.magic = RTC_CRASH_MAGIC;
    record.version = RTC_CRASH_VERSION;
    record.length = sizeof(record);
    record.exception_task[RTC_CRASH_TASK_NAME_CAPACITY - 1] = '\0';
    record.app_elf_sha256[RTC_CRASH_ELF_SHA_CAPACITY - 1] = '\0';
    record.crc32 = crash_record_crc32(record);
}

static void recover_crash_record(esp_reset_reason_t reason) {
    RtcCrashRecord recovered{};
    recovered.reset_reason = static_cast<uint8_t>(reason);

    if (reset_has_core_dump(reason)) {
        esp_core_dump_summary_t summary{};
        if (esp_core_dump_get_summary(&summary) == ESP_OK) {
            recovered.flags |= RTC_CRASH_FLAG_CORE_DUMP_VALID;
            if (summary.exc_bt_info.corrupted) {
                recovered.flags |= RTC_CRASH_FLAG_BACKTRACE_CORRUPTED;
            }

            recovered.crash_pc = summary.exc_pc;
            recovered.exception_tcb = summary.exc_tcb;
            recovered.exception_cause = summary.ex_info.exc_cause;
            recovered.exception_vaddr = summary.ex_info.exc_vaddr;
            recovered.core_dump_version = summary.core_dump_version;
            memcpy(recovered.exception_task, summary.exc_task,
                   sizeof(recovered.exception_task));
            memcpy(recovered.app_elf_sha256, summary.app_elf_sha256,
                   sizeof(recovered.app_elf_sha256));
            memcpy(recovered.exception_a, summary.ex_info.exc_a,
                   sizeof(recovered.exception_a));

            recovered.backtrace_count = bounded_backtrace_count(
                summary.exc_bt_info.depth);
            for (uint8_t i = 0; i < recovered.backtrace_count; ++i) {
                recovered.backtrace_pcs[i] = summary.exc_bt_info.bt[i];
            }
        }
    }

    seal_record(recovered);
    g_crash_record = recovered;
}

static uint32_t emit_crash_record() {
    const esp_reset_reason_t reason =
        static_cast<esp_reset_reason_t>(g_crash_record.reset_reason);
    const bool has_context =
        (g_crash_record.flags & RTC_CRASH_FLAG_CORE_DUMP_VALID) != 0;

    emit_tag(tag::CRASH);
    emit_sep();
    emit_kv_u(key::REASON, g_crash_record.reset_reason);
    emit_sep();
    emit_kv_s(key::DESC, reset_reason_description(reason));
    emit_sep();
    emit_kv_u("core_dump", has_context ? 1U : 0U);
    if (has_context) {
        emit_sep();
        emit_kv_u(key::PC, g_crash_record.crash_pc);
        emit_sep();
        emit_kv_s(key::TASK_NAME, g_crash_record.exception_task);
        emit_sep();
        emit_kv_u("tcb", g_crash_record.exception_tcb);
        emit_sep();
        emit_kv_u("cause", g_crash_record.exception_cause);
        emit_sep();
        emit_kv_u("vaddr", g_crash_record.exception_vaddr);
        emit_sep();
        emit_kv_u("core_ver", g_crash_record.core_dump_version);
        emit_sep();
        emit_kv_s("elf_sha", g_crash_record.app_elf_sha256);
        emit_sep();
        emit_kv_u("bt_bad",
                  (g_crash_record.flags & RTC_CRASH_FLAG_BACKTRACE_CORRUPTED)
                      ? 1U : 0U);
    }
    emit_end();

    uint32_t line_count = 1;
    if (!has_context) return line_count;

    const uint8_t bt_count =
        bounded_backtrace_count(g_crash_record.backtrace_count);
    for (uint8_t i = 0; i < bt_count; ++i) {
        emit_tag(tag::BT);
        emit_sep();
        emit_kv_u(key::I, i);
        emit_sep();
        emit_kv_u(key::PC, g_crash_record.backtrace_pcs[i]);
        emit_end();
        ++line_count;
    }

    for (uint8_t i = 0; i < RTC_CRASH_REGISTER_COUNT; ++i) {
        emit_tag("reg");
        emit_sep();
        emit_kv_u(key::I, i);
        emit_sep();
        emit_kv_u("value", g_crash_record.exception_a[i]);
        emit_end();
        ++line_count;
    }

    return line_count;
}

// ── Public API ─────────────────────────────────────────

void init() {
    const esp_reset_reason_t reason = esp_reset_reason();
    if (is_crash_reset(reason)) {
        // ESP-IDF writes the exception frame to flash before reboot. Recover it
        // now, when flash and the normal runtime are safe to use.
        recover_crash_record(reason);
    }

    if (has_record()) {
        emit_crash_record();
        // Keep the record until an explicit `query crash clear` command.
    }
}

void query() {
    if (!has_record()) {
        emit_record1_s(tag::CRASH, key::DESC, "no crash recorded");
        emit_end_resp("crash", 1);
        return;
    }

    emit_end_resp("crash", emit_crash_record());
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
    return crash_record_valid(g_crash_record);
}

RtcCrashRecord* get_record() {
    return &g_crash_record;
}

}  // namespace crash
}  // namespace telemetry
}  // namespace sigurdos

#endif // SIGURDOS_TELEMETRY
