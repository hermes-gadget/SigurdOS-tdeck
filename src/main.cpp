// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <Arduino.h>
#include "hal/storage.h"
#include "hal/tdeck_board.h"
#include "hal/tdeck_pins.h"
#include "hal/display.h"
#include "hal/battery.h"
#include "hal/gps.h"
#include "hal/sdcard.h"
#include "hal/wifi_ota.h"
#include "hal/github_ota.h"
#include "hal/prefs.h"
#include "hal/launcher_env.h"
#include "hal/buzzer.h"
#include "app/map_renderer.h"
#include "mesh/mesh_wrapper.h"
#include "ui/ui.h"
#include "ui/theme.h"
#include "diagnostics/debug_cfg.h"
#if SIGURDOS_DEBUG_DIAG
#include "diagnostics/debug.h"
#endif
#if SIGURDOS_TELEMETRY
#include "diagnostics/telemetry.h"
#endif
#if defined(SIGURDOS_REMOTE_TEST) && SIGURDOS_REMOTE_TEST
#include "test/test_controller.h"
#endif

static sigurdos::TDeckBoard board;

#if SIGURDOS_DEBUG_UI
static void boot_log(const char* msg)
{
    Serial.printf("[boot] +%lums %s\n", (unsigned long)millis(), msg);
}
#else
static void boot_log(const char*) {}
#endif

static void boot_status(const char* status)
{
    sigurdos::ui::set_boot_status(status);
    sigurdos_display_render_now();
    boot_log(status);
}

void setup()
{
    Serial.begin(115200);
#if defined(SIGURDOS_REMOTE_TEST) && SIGURDOS_REMOTE_TEST
    Serial.println("[boot] HELLO FROM REMOTE_TEST BUILD -v2");
#endif
    boot_log("serial OK");

    board.begin();
    boot_log("board init OK");
    sigurdos_battery_init();

    // Critical-battery sleep wakes periodically to permit recovery after
    // charging. Re-sleep before display/radio/storage initialization when a
    // timer wake finds that the battery remains below the safe threshold.
    const bool deep_sleep_reset = esp_reset_reason() == ESP_RST_DEEPSLEEP;
    const bool timer_wakeup = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
    const uint16_t early_battery_mv = sigurdos_battery_mv();
    if (sigurdos::tdeck_should_resleep_early(
            deep_sleep_reset, timer_wakeup, early_battery_mv)) {
        Serial.printf("[boot] battery still critical (%u mV); returning to deep sleep\n",
                      early_battery_mv);
        board.sleep(0);
        return;
    }
    sigurdos::hal::buzzer_init();

    // Track display init failures across reboots to detect boot loops.
    // RTC_NOINIT_ATTR persists through software resets (ESP.restart()).
    static RTC_NOINIT_ATTR uint8_t display_failures = 0;
    static constexpr uint8_t MAX_DISPLAY_FAILURES = 3;

    if (!sigurdos_display_init()) {
        display_failures++;
        Serial.printf("[boot] FATAL: Display init failed (%u/%u attempts)\n",
                      display_failures, MAX_DISPLAY_FAILURES);
        if (display_failures >= MAX_DISPLAY_FAILURES) {
            Serial.println("[boot] HALTED: Too many display failures.");
            Serial.println("[boot] USB reflashing remains available; reset to retry.");
            // Mesh, BLE, OTA, and interactive command services have not started.
            // Halt to break the reboot loop while retaining ROM/USB reflashing.
            while (true) { delay(1000); }
        }
        Serial.printf("[boot] Restarting in 5s...\n");
        delay(5000);
        ESP.restart();
    }
    display_failures = 0;  // success - reset counter
    boot_log("display core init OK");

    sigurdos::ui::init();
    boot_status("Starting SigurdOS...");
    boot_log("first splash frame flushed");

    boot_status("Mounting storage...");
    // Safe SPIFFS init — auto-formats an erased partition (clean flash)
    // but leaves corrupt data alone (user must factory-reset to recover).
    bool spiffs_ok = sigurdos::storage_init();
    if (!spiffs_ok) {
        if (sigurdos_is_under_launcher()) {
            Serial.println("[boot] WARNING: SPIFFS mount failed — installed app-only under Launcher. Reinstall from the Launcher/merged image (SigurdOS-tdeck-launcher.bin) for persistence.");
        } else {
            Serial.println("[boot] WARNING: SPIFFS mount failed — identity/contacts won't persist across reboots. Use factory reset to reformat and recover.");
        }
    }
    boot_status(spiffs_ok ? "Storage ready" : "Storage unavailable");

    boot_status("Loading settings...");
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    sigurdos::mesh::setOwnName(p.node_name);
    sigurdos::theme::theme_apply(p.theme_id);
    sigurdos_display_set_brightness(p.display_brightness);
    sigurdos_display_reset_auto_off();
    boot_log("settings loaded");

    boot_status("Starting input...");
    sigurdos_display_init_inputs();
    boot_status("Input ready");

    if (p.gps_enabled) {
        boot_status("Starting GPS...");
        sigurdos_gps_init();
        boot_log("GPS init done");
    } else {
        boot_log("GPS disabled");
    }

    // Probe/mount SD before the SX1262 begins listening. SD card handshake may
    // reset SPI2; doing so after radio init can invalidate RadioLib state.
    boot_status("Checking SD card...");
    if (!sigurdos_sdcard_init()) {
        boot_status("No SD card");
    } else {
        boot_status("SD card ready");
    }

    boot_status("Starting radio...");
    const char* radio_status = "Radio ready";
#if defined(SIGURDOS_REMOTE_TEST) && SIGURDOS_REMOTE_TEST
#if defined(SIGURDOS_REMOTE_TEST_RADIO) && SIGURDOS_REMOTE_TEST_RADIO
    Serial.println("[boot] REMOTE TEST MODE — LoRa radio enabled (test controller + mesh)");
    if (!sigurdos::mesh::init(spiffs_ok)) {
        radio_status = "Radio unavailable";
    }
#else
    // Remote test mode — no LoRa radio initialised, but the shared SPI bus
    // (pins 40/38/41) must be initialised before SD card init or the card
    // fails with FR_NOT_READY. mesh::init() handles this via sigurdos_shared_spi_begin().
    Serial.println("[boot] REMOTE TEST MODE — LoRa radio disabled");
    radio_status = "Radio disabled";
    sigurdos::mesh::init(spiffs_ok);
#endif
    sigurdos_test_controller_init();
#else
    if (!sigurdos::mesh::init(spiffs_ok)) {
        Serial.println("[boot] WARNING: Radio init failed");
        radio_status = "Radio unavailable";
    }
#endif
    boot_status(radio_status);
    sigurdos_sdcard_lock_bus_reset();

    boot_status("Loading chats...");
    sigurdos::ui::load_persisted_state();
    boot_status("Chats ready");

#if SIGURDOS_DEBUG_DIAG
    sigurdos::debug::init();
    boot_log("debug diagnostics enabled");
#endif

    boot_status("Preparing map...");
    sigurdos_map_init();

    boot_status("Ready");
    boot_log("SigurdOS T-Deck ready");

    // Auto-connect WiFi if credentials are saved
    {
        const sigurdos::NodePrefs& p = sigurdos::prefs_get();
        if (p.wifi_ssid[0]) {
            sigurdos::wifi_sta::beginConnect(p.wifi_ssid, p.wifi_password);
        }
    }

#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::init();
#endif
}

void loop()
{
    // Loop timing — measure from start of loop
#if SIGURDOS_TELEMETRY
    uint32_t loop_start_us = micros();
#endif
    // Low-battery auto-shutdown (matches MeshCore pattern)
    static uint32_t last_batt_check = 0;
    if (millis() - last_batt_check > 30000) {  // every 30s
        last_batt_check = millis();
        if (board.isBatteryCritical()) {
            Serial.println("CRITICAL: Battery low — entering deep sleep");
            board.sleep(0);  // sleep indefinitely until charged
            return;
        }
    }

    // Process display/LVGL first so UI stays responsive during mesh ops
    sigurdos_display_loop();
    sigurdos::hal::buzzer_loop();  // non-blocking beep pattern playback
    sigurdos::ota::loop();         // WiFi OTA web server
    sigurdos::github_ota::loop();  // GitHub OTA downloader
    sigurdos::wifi_sta::loop();    // WiFi STA maintenance
    {   // WiFi icon refresh — 1 Hz is plenty for an RSSI readout
        static uint32_t last_wifi_ui = 0;
        if (millis() - last_wifi_ui >= 1000) {
            last_wifi_ui = millis();
            sigurdos::ui::update_wifi_status();  // bottom bar WiFi icon
        }
    }
    {   // Persisted background cadence plus explicit map/time-sync demand.
        const sigurdos::NodePrefs& gp = sigurdos::prefs_get();
        sigurdos_gps_service(gp.gps_enabled, gp.gps_interval);
    }
#if defined(SIGURDOS_REMOTE_TEST) && SIGURDOS_REMOTE_TEST
    sigurdos::mesh::loop();
    sigurdos_test_controller_loop();
#else
    sigurdos::mesh::loop();
#endif
    sigurdos::ui::loop();

#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::loop();
    // Report loop timing after telemetry processing
    uint32_t loop_elapsed_us = micros() - loop_start_us;
    sigurdos::telemetry::report_loop_timing(loop_elapsed_us);
#endif
#if SIGURDOS_DEBUG_DIAG
    sigurdos::debug::loop();
#endif
}
