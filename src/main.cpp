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
#include "app/map_renderer.h"
#include "mesh/mesh_wrapper.h"
#include "ui/ui.h"
#include "diagnostics/debug_cfg.h"
#if SLOPOS_DEBUG_DIAG
#include "diagnostics/debug.h"
#endif
#if defined(SLOPOS_REMOTE_TEST) && SLOPOS_REMOTE_TEST
#include "test/test_controller.h"
#endif

static slopos::TDeckBoard board;

void setup()
{
    delay(250);  // Let WebSerial port close before claiming USB CDC endpoint
    Serial.begin(115200);
    delay(500);
#if SLOPOS_DEBUG_UI
    Serial.println("SlopOS T-Deck — booting...");
    Serial.println("[boot] step 1: serial OK");
#endif

    board.begin();
#if SLOPOS_DEBUG_UI
    Serial.println("[boot] step 2: board init OK");
#endif
    slopos_battery_init();

    bool spiffs_ok = SPIFFS.begin(true);
    if (!spiffs_ok)
        Serial.println("[boot] WARNING: SPIFFS mount failed — identity/contacts won't persist across reboots");
#if SLOPOS_DEBUG_UI
    else
        Serial.println("[boot] step 3: SPIFFS mounted");
#endif

    slopos_gps_init();
#if SLOPOS_DEBUG_UI
    Serial.println("[boot] step 4: GPS init done");
#endif

    if (!slopos_display_init()) {
        Serial.println("[boot] FATAL: Display init failed");
        while (1) delay(1000);
    }
#if SLOPOS_DEBUG_UI
    Serial.println("[boot] step 6: display init OK");
#endif

    {
        const slopos::NodePrefs& p = slopos::prefs_get();
        slopos::mesh::setOwnName(p.node_name);
    }
#if defined(SLOPOS_REMOTE_TEST) && SLOPOS_REMOTE_TEST
    // Remote test mode — no LoRa radio initialised, but the shared SPI bus
    // (pins 40/38/41) must be initialised before SD card init or the card
    // fails with FR_NOT_READY. mesh::init() handles this via lora_spi.begin().
    Serial.println("[boot] REMOTE TEST MODE — LoRa radio disabled");
    slopos::mesh::init(spiffs_ok);
    slopos_test_controller_init();
#else
    if (!slopos::mesh::init(spiffs_ok))
        Serial.println("[boot] WARNING: Radio init failed");
#if SLOPOS_DEBUG_UI
    else
        Serial.println("[boot] step 7: mesh radio initialized");
#endif
#endif

    slopos::ui::init();
#if SLOPOS_DEBUG_UI
    Serial.println("[boot] step 8: UI splash screen shown");
#endif
#if SLOPOS_DEBUG_DIAG
    slopos::debug::init();
    Serial.println("[boot] step 9: debug diagnostics enabled");
#endif

    // SD card init after radio init so SPI bus is already configured
    if (!slopos_sdcard_init()) {
#if SLOPOS_DEBUG_UI
        Serial.println("[boot] step 10: no SD card detected");
#endif
    } else {
#if SLOPOS_DEBUG_UI
        Serial.println("[boot] step 10: SD card mounted");
#endif
    }

    slopos_map_init();

#if SLOPOS_DEBUG_UI
    Serial.println("[boot] === SlopOS T-Deck ready ===");
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

#if defined(SLOPOS_REMOTE_TEST) && SLOPOS_REMOTE_TEST
    slopos_test_controller_loop();
#else
    slopos::mesh::loop();
#endif
    slopos_gps_loop();
    slopos_display_loop();
    slopos::ui::loop();

#if SLOPOS_DEBUG_DIAG
    slopos::debug::loop();
#endif
}
