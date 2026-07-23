// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstdint>

namespace sigurdos {
namespace hal {

#ifndef SIGURDOS_SETUP_WATCHDOG_TIMEOUT_SEC
#define SIGURDOS_SETUP_WATCHDOG_TIMEOUT_SEC 60
#endif

#ifndef SIGURDOS_RUNTIME_WATCHDOG_TIMEOUT_SEC
#define SIGURDOS_RUNTIME_WATCHDOG_TIMEOUT_SEC 10
#endif

constexpr uint32_t SETUP_WATCHDOG_TIMEOUT_SEC =
    SIGURDOS_SETUP_WATCHDOG_TIMEOUT_SEC;
constexpr uint32_t RUNTIME_WATCHDOG_TIMEOUT_SEC =
    SIGURDOS_RUNTIME_WATCHDOG_TIMEOUT_SEC;

enum class RuntimeWatchdogOwner : uint8_t {
    LoopTask,
};

// loopTask is the sole runtime-watchdog owner. OTA workers are deliberately
// not subscribed: synchronous TLS and the framework multipart parser may wait
// on a slow peer, while loopTask must continue feeding only after a complete
// application loop (LVGL, mesh, input, battery, and telemetry).
constexpr RuntimeWatchdogOwner RUNTIME_WATCHDOG_OWNER =
    RuntimeWatchdogOwner::LoopTask;
constexpr bool OTA_WORKER_OWNS_TRANSPORT_AND_FLASH = true;
constexpr bool OTA_WORKER_SUBSCRIBES_RUNTIME_WATCHDOG = false;

static_assert(SETUP_WATCHDOG_TIMEOUT_SEC > RUNTIME_WATCHDOG_TIMEOUT_SEC,
              "setup watchdog must allow more time than runtime");
static_assert(RUNTIME_WATCHDOG_TIMEOUT_SEC > 0,
              "runtime watchdog timeout must be non-zero");
static_assert(OTA_WORKER_OWNS_TRANSPORT_AND_FLASH &&
                  !OTA_WORKER_SUBSCRIBES_RUNTIME_WATCHDOG,
              "OTA transport must not block the loopTask watchdog owner");

constexpr uint32_t BOOT_WATCHDOG_MAGIC = 0x42574431u;  // "BWD1"
constexpr uint8_t BOOT_WATCHDOG_VERSION = 1;

enum class BootStage : uint8_t {
    None = 0,
    Starting,
    Board,
    Battery,
    Display,
    Ui,
    Storage,
    Settings,
    Input,
    Gps,
    SdCard,
    Radio,
    Chats,
    Diagnostics,
    Map,
    Wifi,
    Telemetry,
    Runtime,
    MapDiscovery,
    Count,
};

struct BootWatchdogRecord {
    uint32_t magic;
    uint8_t version;
    uint8_t stage;
    uint16_t progress_count;
    uint32_t last_progress_ms;
    uint32_t reserved;
};

inline void boot_watchdog_record_clear(BootWatchdogRecord& record) {
    record = BootWatchdogRecord{
        BOOT_WATCHDOG_MAGIC,
        BOOT_WATCHDOG_VERSION,
        static_cast<uint8_t>(BootStage::None),
        0,
        0,
        0,
    };
}

inline bool boot_watchdog_record_valid(const BootWatchdogRecord& record) {
    return record.magic == BOOT_WATCHDOG_MAGIC &&
           record.version == BOOT_WATCHDOG_VERSION &&
           record.stage > static_cast<uint8_t>(BootStage::None) &&
           record.stage < static_cast<uint8_t>(BootStage::Count) &&
           record.progress_count > 0 &&
           record.reserved == 0;
}

inline void boot_watchdog_record_progress(BootWatchdogRecord& record,
                                          BootStage stage,
                                          uint32_t now_ms) {
    if (record.magic != BOOT_WATCHDOG_MAGIC ||
        record.version != BOOT_WATCHDOG_VERSION ||
        record.reserved != 0) {
        boot_watchdog_record_clear(record);
    }
    record.stage = static_cast<uint8_t>(stage);
    if (record.progress_count != UINT16_MAX) ++record.progress_count;
    record.last_progress_ms = now_ms;
}

inline bool boot_watchdog_previous_stall(const BootWatchdogRecord& retained,
                                         bool task_watchdog_reset,
                                         BootWatchdogRecord* out) {
    if (!task_watchdog_reset || !out ||
        !boot_watchdog_record_valid(retained)) {
        return false;
    }
    *out = retained;
    return true;
}

inline bool boot_watchdog_would_expire(uint32_t last_progress_ms,
                                       uint32_t now_ms,
                                       uint32_t timeout_sec) {
    return static_cast<uint32_t>(now_ms - last_progress_ms) >=
           timeout_sec * 1000u;
}

const char* boot_watchdog_stage_name(BootStage stage);

bool boot_watchdog_begin(int reset_reason);
void boot_watchdog_progress(BootStage stage);
bool boot_watchdog_enter_runtime();
void boot_watchdog_runtime_progress();
void boot_watchdog_stop();

}  // namespace hal
}  // namespace sigurdos
