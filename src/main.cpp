// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <Arduino.h>
#include <SPIFFS.h>
#include "hal/tdeck_pins.h"
#include "hal/tdeck_board.h"
#include "hal/display.h"
#include "hal/battery.h"
#include "hal/gps.h"
#include "hal/sdcard.h"
#include "hal/prefs.h"
#include "hal/buzzer.h"
#include "app/map_renderer.h"
#include "mesh/mesh_wrapper.h"
#include "ui/ui.h"
#include "ui/theme.h"
#include "diagnostics/debug_cfg.h"
#if SIGURDOS_DEBUG_DIAG
#include "diagnostics/debug.h"
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
    if (!spiffs_ok)
        Serial.println("[boot] WARNING: SPIFFS mount failed — identity/contacts won't persist across reboots");
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
    // fails with FR_NOT_READY. mesh::init() handles this via lora_spi.begin().
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
}

void loop()
{
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

#if SIGURDOS_DEBUG_DIAG
    sigurdos::debug::loop();
#endif
}
