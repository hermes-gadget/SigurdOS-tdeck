// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "boot_watchdog.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "diagnostics/log.h"

namespace sigurdos {
namespace hal {
namespace {

RTC_NOINIT_ATTR BootWatchdogRecord s_record;
bool s_enabled = false;
uint32_t s_last_runtime_record_ms = 0;

bool feedWatchdog() {
    if (!s_enabled) return false;
    const esp_err_t err = esp_task_wdt_reset();
    if (err == ESP_OK) return true;
    SIG_LOGW("[boot] loopTask watchdog feed failed (%d)", err);
    return false;
}

}  // namespace

const char* boot_watchdog_stage_name(BootStage stage) {
    switch (stage) {
    case BootStage::Starting: return "starting";
    case BootStage::Board: return "board";
    case BootStage::Battery: return "battery";
    case BootStage::Display: return "display";
    case BootStage::Ui: return "ui";
    case BootStage::Storage: return "storage";
    case BootStage::Settings: return "settings";
    case BootStage::Input: return "input";
    case BootStage::Gps: return "gps";
    case BootStage::SdCard: return "sd-card";
    case BootStage::Radio: return "radio";
    case BootStage::Chats: return "chats";
    case BootStage::Diagnostics: return "diagnostics";
    case BootStage::Map: return "map";
    case BootStage::Wifi: return "wifi";
    case BootStage::Telemetry: return "telemetry";
    case BootStage::Runtime: return "runtime";
    case BootStage::MapDiscovery: return "map-discovery";
    default: return "unknown";
    }
}

bool boot_watchdog_begin(int reset_reason) {
    BootWatchdogRecord previous{};
    const bool previous_stall = boot_watchdog_previous_stall(
        s_record, reset_reason == ESP_RST_TASK_WDT, &previous);

    boot_watchdog_record_clear(s_record);
    boot_watchdog_record_progress(s_record, BootStage::Starting, millis());

    if (previous_stall) {
        SIG_LOGW(
            "[boot] Previous loopTask watchdog reset: stage=%s progress=%u last=+%lums",
            boot_watchdog_stage_name(static_cast<BootStage>(previous.stage)),
            previous.progress_count,
            static_cast<unsigned long>(previous.last_progress_ms));
    }

    const esp_err_t init_err = esp_task_wdt_init(
        SETUP_WATCHDOG_TIMEOUT_SEC, true);
    if (init_err != ESP_OK) {
        SIG_LOGW("[boot] setup watchdog init failed (%d)", init_err);
        return false;
    }

    enableLoopWDT();
    if (esp_task_wdt_status(nullptr) != ESP_OK) {
        SIG_LOGW("[boot] loopTask watchdog registration failed");
        return false;
    }

    s_enabled = true;
    feedWatchdog();
    SIG_LOGD("[boot] loopTask watchdog enabled (%lus setup)",
             static_cast<unsigned long>(SETUP_WATCHDOG_TIMEOUT_SEC));
    return true;
}

void boot_watchdog_progress(BootStage stage) {
    boot_watchdog_record_progress(s_record, stage, millis());
    feedWatchdog();
}

bool boot_watchdog_enter_runtime() {
    boot_watchdog_progress(BootStage::Runtime);
    const esp_err_t init_err = esp_task_wdt_init(
        RUNTIME_WATCHDOG_TIMEOUT_SEC, true);
    if (init_err != ESP_OK) {
        SIG_LOGW("[boot] runtime watchdog config failed (%d)", init_err);
        return false;
    }
    s_last_runtime_record_ms = millis();
    feedWatchdog();
    SIG_LOGD("[boot] loopTask watchdog tightened to %lus",
             static_cast<unsigned long>(RUNTIME_WATCHDOG_TIMEOUT_SEC));
    return true;
}

void boot_watchdog_runtime_progress() {
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - s_last_runtime_record_ms) < 1000u) return;
    s_last_runtime_record_ms = now;
    // Arduino's loopTask wrapper feeds the registered task before each loop().
    // This records completed-loop progress without adding a second hidden feed.
    boot_watchdog_record_progress(s_record, BootStage::Runtime, now);
}

void boot_watchdog_stop() {
    if (!s_enabled) return;
    disableLoopWDT();
    s_enabled = false;
}

}  // namespace hal
}  // namespace sigurdos
