// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <Arduino.h>
#include <SPIFFS.h>
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

void setup()
{
    delay(250);  // Let WebSerial port close before claiming USB CDC endpoint
    Serial.begin(115200);
    delay(500);
    Serial.println("[boot] HELLO FROM REMOTE_TEST BUILD -v2");
    Serial.flush();
#if SIGURDOS_DEBUG_UI
    Serial.println("SigurdOS T-Deck — booting...");
    Serial.println("[boot] step 1: serial OK");
#endif

    board.begin();
#if SIGURDOS_DEBUG_UI
    Serial.println("[boot] step 2: board init OK");
#endif
    sigurdos_battery_init();
    sigurdos::hal::buzzer_init();

    bool spiffs_ok = SPIFFS.begin(true);
    if (!spiffs_ok) {
        if (sigurdos_is_under_launcher()) {
            Serial.println("[boot] WARNING: SPIFFS mount failed — installed app-only under Launcher. Reinstall from the Launcher/merged image (SigurdOS-tdeck-launcher.bin) for persistence.");
        } else {
            Serial.println("[boot] WARNING: SPIFFS mount failed — identity/contacts won't persist across reboots");
        }
    }
#if SIGURDOS_DEBUG_UI
    else
        Serial.println("[boot] step 3: SPIFFS mounted");
#endif

    if (sigurdos::prefs_get().gps_enabled) {
        sigurdos_gps_init();
    }
#if SIGURDOS_DEBUG_UI
    Serial.println("[boot] step 4: GPS init done");
#endif

    if (!sigurdos_display_init()) {
        Serial.println("[boot] FATAL: Display init failed");
        while (1) delay(1000);
    }
#if SIGURDOS_DEBUG_UI
    Serial.println("[boot] step 6: display init OK");
#endif

    {
        const sigurdos::NodePrefs& p = sigurdos::prefs_get();
        sigurdos::mesh::setOwnName(p.node_name);
        sigurdos::theme::theme_apply(p.theme_id);
    }
#if defined(SIGURDOS_REMOTE_TEST) && SIGURDOS_REMOTE_TEST
#if defined(SIGURDOS_REMOTE_TEST_RADIO) && SIGURDOS_REMOTE_TEST_RADIO
    Serial.println("[boot] REMOTE TEST MODE — LoRa radio enabled (test controller + mesh)");
#else
    // Remote test mode — no LoRa radio initialised, but the shared SPI bus
    // (pins 40/38/41) must be initialised before SD card init or the card
    // fails with FR_NOT_READY. mesh::init() handles this via sigurdos_shared_spi_begin().
    Serial.println("[boot] REMOTE TEST MODE — LoRa radio disabled");
#endif
    sigurdos::mesh::init(spiffs_ok);
    sigurdos_test_controller_init();
#else
    if (!sigurdos::mesh::init(spiffs_ok))
        Serial.println("[boot] WARNING: Radio init failed");
#if SIGURDOS_DEBUG_UI
    else
        Serial.println("[boot] step 7: mesh radio initialized");
#endif
#endif

    sigurdos::ui::init();
#if SIGURDOS_DEBUG_UI
    Serial.println("[boot] step 8: UI splash screen shown");
#endif
#if SIGURDOS_DEBUG_DIAG
    sigurdos::debug::init();
    Serial.println("[boot] step 9: debug diagnostics enabled");
#endif

    // SD card init after radio init so SPI bus is already configured
    if (!sigurdos_sdcard_init()) {
#if SIGURDOS_DEBUG_UI
        Serial.println("[boot] step 10: no SD card detected");
#endif
    } else {
#if SIGURDOS_DEBUG_UI
        Serial.println("[boot] step 10: SD card mounted");
#endif
    }

    sigurdos_map_init();

#if SIGURDOS_DEBUG_UI
    Serial.println("[boot] === SigurdOS T-Deck ready ===");
#endif

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
    sigurdos::ota::loop();         // WiFi OTA web server
    sigurdos::github_ota::loop();  // GitHub OTA downloader
    sigurdos::wifi_sta::loop();    // WiFi STA maintenance
    sigurdos::ui::update_wifi_status();  // bottom bar WiFi icon
    {   // GPS enabled + interval gate
        static uint32_t last_gps_poll = 0;
        const sigurdos::NodePrefs& gp = sigurdos::prefs_get();
        if (gp.gps_enabled) {
            uint32_t now = millis();
            uint32_t interval_ms = (uint32_t)gp.gps_interval * 1000;
            if (interval_ms == 0 || (now - last_gps_poll >= interval_ms)) {
                last_gps_poll = now;
                sigurdos_gps_loop();
            }
        }
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
