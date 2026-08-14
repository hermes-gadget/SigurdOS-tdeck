// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <cstring>
#include "hal/storage.h"
#include "hal/tdeck_board.h"
#include "hal/tdeck_pins.h"
#include "hal/display.h"
#include "hal/battery.h"
#include "hal/gps.h"
#include "hal/sdcard.h"
#include "hal/wifi_ota.h"
#include "hal/github_ota.h"
#include "hal/wifi_coordinator.h"
#include "hal/prefs.h"
#include "hal/factory_reset_policy.h"
#include "hal/launcher_env.h"
#include "hal/buzzer.h"
#include "hal/boot_watchdog.h"
#include "hal/display_retry_state.h"
#include "hal/ota_boot_health.h"
#include "hal/keyboard.h"
#include "hal/touch.h"
#include "hal/trackball.h"
#include "app/map_renderer.h"
#include "app/gps_track_log.h"
#include "app/gps_clock_handoff.h"
#include "mesh/mesh_wrapper.h"
#include "comms/transport_iface.h"
#include "mesh/sd_message_store.h"
#include "ui/ui.h"
#include "ui/screens_common.h"
#include "ui/theme.h"
#include "power/screen_sleep.h"
#include "diagnostics/debug_cfg.h"
#include "diagnostics/diagnostic_io.h"
#if SIGURDOS_DEBUG_DIAG
#include "diagnostics/debug.h"
#endif
#if SIGURDOS_TELEMETRY
#include "diagnostics/telemetry.h"
#endif
#if defined(SIGURDOS_REMOTE_TEST) && SIGURDOS_REMOTE_TEST
#include "test/test_controller.h"
#endif

// The production source filter intentionally enumerates src subdirectories
// and cannot be changed for this phase. Keep the policy implementation in its
// own source file while compiling it once through the root main translation
// unit; native tests include the same implementation directly.
#include "power/screen_sleep.cpp"

static sigurdos::TDeckBoard board;

// Always emit compact boot milestones so release/empty-NVS boots are
// diagnosable over serial. Verbose subsystem detail stays behind DEBUG_UI.
static void boot_log(const char* msg)
{
    Serial.printf("[boot] +%lums %s\n", (unsigned long)millis(), msg);
}

static void boot_status(const char* status)
{
    sigurdos::ui::set_boot_status(status);
    sigurdos_display_render_now();
    boot_log(status);
}

#include "esp_log.h"
static const char* BOOT_TAG = "boot";

static bool power_screen_off()
{
    return !sigurdos_display_is_on();
}

static bool power_no_companion_clients()
{
    return !sigurdos::mesh::companionBleConnected();
}

static bool power_wifi_off()
{
    return sigurdos::wifi::currentOwner() == sigurdos::wifi::Owner::None &&
           !sigurdos::ota::isActive() &&
           !sigurdos::github_ota::isActive() &&
           !sigurdos::wifi_sta::isConnected();
}

static bool power_ble_off()
{
    // An enabled BLE companion keeps the 2.4 GHz radio advertising even when
    // no phone is connected, so it is intentionally a separate gate from
    // no_companion_clients.
    return !sigurdos::mesh::companionBleAvailable() ||
           !sigurdos::mesh::companionBleEnabled();
}

static bool power_on_battery()
{
    return !sigurdos_battery_charging();
}

static bool power_mesh_idle()
{
    return sigurdos::mesh::pendingMessageCount() == 0 &&
           !sigurdos::mesh::pingIsActive() &&
           !sigurdos::mesh::statusRequestPending();
}

static uint32_t power_next_wake_forcing_in_ms(uint32_t)
{
    return sigurdos::power::NO_WAKE_DEADLINE_MS;
}

static uint32_t power_epoch_now()
{
    return sigurdos::mesh::getCurrentTime();
}

static bool power_packet_entry_is_rx(const sigurdos::mesh::PacketLogEntry& entry)
{
    return entry.type[0] != '\0' &&
           std::strncmp(entry.type, "TX_", 3) != 0 &&
           std::strcmp(entry.type, "BOOT") != 0;
}

static sigurdos::power::WakeReason power_poll_wake_reason()
{
    static bool initialized = false;
    static uint32_t last_touch_press_count = 0;
    static uint32_t last_keyboard_event_count = 0;
    static uint32_t last_trackball_event_count = 0;
    static uint32_t last_packet_generation = 0;

    SigurdOSTouchDiag touch{};
    SigurdOSKeyboardDiag keyboard{};
    SigurdOSTrackballDiag trackball{};
    const bool have_touch = sigurdos_touch_get_diag(&touch);
    const bool have_keyboard = sigurdos_keyboard_get_diag(&keyboard);
    const bool have_trackball = sigurdos_trackball_get_diag(&trackball);
    const uint32_t packet_generation =
        sigurdos::mesh::getPacketLogGeneration();

    if (!initialized) {
        initialized = true;
        if (have_touch) last_touch_press_count = touch.press_count;
        if (have_keyboard) last_keyboard_event_count = keyboard.event_count;
        if (have_trackball) last_trackball_event_count = trackball.event_count;
        last_packet_generation = packet_generation;
        return sigurdos::power::WakeReason::None;
    }

    sigurdos::power::WakeReason reason = sigurdos::power::WakeReason::None;

    if (have_touch) {
        if (touch.press_count != last_touch_press_count) {
            reason = sigurdos::power::WakeReason::Touch;
        }
        last_touch_press_count = touch.press_count;
    }

    if (have_keyboard) {
        if (keyboard.event_count != last_keyboard_event_count &&
            reason == sigurdos::power::WakeReason::None) {
            reason = sigurdos::power::WakeReason::Button;
        }
        last_keyboard_event_count = keyboard.event_count;
    }

    if (have_trackball) {
        if (trackball.event_count != last_trackball_event_count &&
            reason == sigurdos::power::WakeReason::None) {
            reason = sigurdos::power::WakeReason::Button;
        }
        last_trackball_event_count = trackball.event_count;
    }

    if (packet_generation != last_packet_generation) {
        const int count = sigurdos::mesh::getPacketLogCount();
        sigurdos::mesh::PacketLogEntry entry{};
        if (count > 0 &&
            sigurdos::mesh::getPacketLogEntry(count - 1, &entry) &&
            power_packet_entry_is_rx(entry)) {
            reason = sigurdos::power::WakeReason::Packet;
        }
        last_packet_generation = packet_generation;
    }

    return reason;
}

static void power_wake_display()
{
    sigurdos_display_wake();
}

static void power_transition(uint32_t, bool entering)
{
    if (entering) sigurdos::ui::lock_screen_enter();
}

static void power_wake_notification(
    const sigurdos::power::WakeEvent& event)
{
    sigurdos::ui::lock_screen_note_wake(event.reason);
}

[[noreturn]] static void enter_orderly_sleep()
{
    sigurdos::ui::pin_clear_grace();
    // The mesh shutdown coordinator stops new work, persists every available
    // store, quiesces buses, and retries checked deep-sleep preparation. It is
    // valid before mesh init and skips persistence when no store is usable.
    sigurdos::mesh::shutdown();

    // TDeckBoard::sleep() is retrying and successful deep sleep is
    // non-returning. Preserve the application invariant if that contract is
    // ever violated by a platform implementation.
    while (true) {
        Serial.println("[power] shutdown coordinator returned unexpectedly");
        delay(1000);
    }
}

void setup()
{
    esp_log_write(ESP_LOG_ERROR, BOOT_TAG, "SETUP ENTRY - BEFORE Serial.begin");
    Serial.begin(115200);
    sigurdos::diagnostics::configure_diagnostic_output();
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    sigurdos::ota_boot_health::begin();
    sigurdos::hal::boot_watchdog_begin(reset_reason);
#if defined(SIGURDOS_REMOTE_TEST) && SIGURDOS_REMOTE_TEST
    esp_log_write(ESP_LOG_ERROR, BOOT_TAG, "HELLO FROM REMOTE_TEST BUILD -v2");
#endif
    boot_log("serial OK");

    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Board);
    board.begin();
    boot_log("board init OK");
    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Battery);
    sigurdos_battery_init();

    // Critical-battery sleep wakes periodically to permit recovery after
    // charging. Re-sleep before display/radio/storage initialization when a
    // timer wake finds that the battery remains below the safe threshold.
    const bool deep_sleep_reset = reset_reason == ESP_RST_DEEPSLEEP;
    const bool timer_wakeup = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
    const uint16_t early_battery_mv = sigurdos_battery_mv();
    if (sigurdos::tdeck_should_resleep_early(
            deep_sleep_reset, timer_wakeup, early_battery_mv)) {
        Serial.printf("[boot] battery still critical (%u mV); returning to deep sleep\n",
                      early_battery_mv);
        enter_orderly_sleep();
    }
    sigurdos::hal::buzzer_init();

    // Track display init failures across reboots to detect boot loops.
    // RTC_NOINIT_ATTR persists through software resets (ESP.restart()).
    static RTC_NOINIT_ATTR sigurdos::hal::DisplayRetryState
        display_retry_state;
    sigurdos::hal::display_retry_begin(
        display_retry_state, reset_reason == ESP_RST_SW);

    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Display);
    if (!sigurdos_display_init()) {
        const uint8_t display_failures =
            sigurdos::hal::display_retry_note_failure(display_retry_state);
        Serial.printf("[boot] FATAL: Display init failed (%u/%u attempts)\n",
                      display_failures,
                      sigurdos::hal::MAX_DISPLAY_FAILURES);
        sigurdos::ota_boot_health::rollbackIfPending(
            "display initialization failed");
        if (sigurdos::hal::display_retry_should_halt(display_retry_state)) {
            Serial.println("[boot] HALTED: Too many display failures.");
            Serial.println("[boot] USB reflashing remains available; reset to retry.");
            // Mesh, BLE, OTA, and interactive command services have not started.
            // Halt to break the reboot loop while retaining ROM/USB reflashing.
            sigurdos::hal::boot_watchdog_stop();
            while (true) { delay(1000); }
        }
        Serial.printf("[boot] Restarting in 5s...\n");
        delay(5000);
        ESP.restart();
    }
    sigurdos::hal::display_retry_clear(display_retry_state);
    boot_log("display core init OK");

    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Ui);
    sigurdos::ui::init();
    boot_status("Starting SigurdOS...");
    boot_log("first splash frame flushed");

    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Storage);
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
    if (spiffs_ok) {
        // Absorb the first-write SPIFFS GC in a background task so the
        // first runtime commit (channel store etc.) is not 10-90s slow.
        sigurdos::storage_warm_after_mount();
    }

    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Settings);
    boot_status("Loading settings...");
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    sigurdos::mesh::setOwnName(p.node_name);
    sigurdos::theme::theme_apply(p.theme_id);
    sigurdos_display_set_brightness(p.display_brightness);
    sigurdos_display_reset_auto_off();

    // ── Phase 3 screen sleep / lock integration ───────────────
    // Existing display auto-off remains responsible for turning off the
    // backlights. This policy only gates the main loop after that transition.
    {
        sigurdos::power::Hooks power_hooks;
        power_hooks.screenOff = power_screen_off;
        power_hooks.noClient = power_no_companion_clients;
        power_hooks.wifiOff = power_wifi_off;
        power_hooks.bleOff = power_ble_off;
        power_hooks.onBattery = power_on_battery;
        power_hooks.meshIdle = power_mesh_idle;
        power_hooks.nextWakeForcingInMs = power_next_wake_forcing_in_ms;
        power_hooks.epochNow = power_epoch_now;
        power_hooks.pollWakeReason = power_poll_wake_reason;
        power_hooks.wakeDisplay = power_wake_display;
        sigurdos::power::begin(power_hooks);
        sigurdos::power::onTransition(power_transition);
        sigurdos::power::onWake(power_wake_notification);
        sigurdos::power::setEnabled(true);
    }
    boot_log("settings loaded");

    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Input);
    boot_status("Starting input...");
    const SigurdOSInputInitStatus input = sigurdos_display_init_inputs();
    if (input.all_ready()) {
        boot_status("Input ready");
    } else {
        Serial.printf("[boot] WARNING: Input degraded (lvgl=%d touch=%d keyboard=%d trackball=%d)\n",
                      input.lvgl_ready ? 1 : 0,
                      input.touch_ready ? 1 : 0,
                      input.keyboard_ready ? 1 : 0,
                      input.trackball_ready ? 1 : 0);
        boot_status("Input degraded");
    }

    if (p.gps_enabled || p.gps_track_enabled) {
        sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Gps);
        boot_status("Starting GPS...");
        sigurdos_gps_init();
        boot_log("GPS init done");
    } else {
        boot_log("GPS disabled");
    }

    // Probe/mount SD before the SX1262 begins listening. SD card handshake may
    // reset SPI2; doing so after radio init can invalidate RadioLib state.
    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::SdCard);
    boot_status("Checking SD card...");
    if (!sigurdos_sdcard_init()) {
        boot_status("No SD card");
    } else {
        boot_status("SD card ready");
    }

    // A reset marker means a destructive wipe was interrupted or is being
    // resumed. Do not initialize radio/mesh or enter ordinary runtime until
    // the transaction has completed; factoryReset() preserves the marker on
    // terminal failure so the next boot retries the correct stage.
    sigurdos::hal::factory_reset::ResetMarkerStage reset_stage =
        sigurdos::hal::factory_reset::ResetMarkerStage::Armed;
    const sigurdos::hal::factory_reset::ResetMarkerStatus reset_status =
        sigurdos::hal::factory_reset::reset_marker_read(&reset_stage);

    // ── Phase 2: select the deep chat-history backend ──
    // Mesh/UI callers keep using message_store; this boot-time selector
    // chooses SD when mounted and leaves the bounded SPIFFS fallback active
    // otherwise. Once NVS or SPIFFS reset work has completed, skip selection
    // so boot cannot migrate history back onto an SD card being wiped.
    if (reset_status ==
            sigurdos::hal::factory_reset::ResetMarkerStatus::Absent ||
        (reset_status ==
             sigurdos::hal::factory_reset::ResetMarkerStatus::Pending &&
         reset_stage ==
             sigurdos::hal::factory_reset::ResetMarkerStage::Armed)) {
        sigurdos::mesh::sdMessageStoreSelect(spiffs_ok);
    }

    if (reset_status !=
        sigurdos::hal::factory_reset::ResetMarkerStatus::Absent) {
        boot_status("Resuming factory reset...");
        if (!sigurdos::mesh::factoryReset()) {
            Serial.println("[boot] HALTED: factory reset could not complete");
        } else {
            Serial.println("[boot] HALTED: factory reset restart returned unexpectedly");
        }
        sigurdos::hal::boot_watchdog_stop();
        while (true) { delay(1000); }
    }

    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Radio);
    boot_status("Starting radio...");
    const char* radio_status = "Radio ready";
#if defined(SIGURDOS_REMOTE_TEST) && SIGURDOS_REMOTE_TEST
#if defined(SIGURDOS_REMOTE_TEST_RADIO) && SIGURDOS_REMOTE_TEST_RADIO
    Serial.println("[boot] REMOTE TEST MODE — LoRa radio enabled (test controller + mesh)");
    if (!sigurdos::mesh::init(spiffs_ok)) {
        radio_status = "Radio unavailable";
    }
#else
    // Remote test mode — no LoRa radio initialised. SD card init (above)
    // already initialised the shared SPI singleton (pins 40/38/41); the later
    // mesh::init() call only reuses it via sigurdos_shared_spi_begin().
    Serial.println("[boot] REMOTE TEST MODE — LoRa radio disabled");
    radio_status = "Radio disabled";
    sigurdos::mesh::init(spiffs_ok);
#endif
    sigurdos_test_controller_init();
    // OTA partition view — dual-slot verification aid (remote-test builds only)
    {
        const esp_partition_t* boot = esp_ota_get_boot_partition();
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
        Serial.printf("[ota-diag] boot=%s running=%s next=%s\n",
                      boot ? boot->label : "?",
                      running ? running->label : "?",
                      next ? next->label : "?");
    }
#else
    if (!sigurdos::mesh::init(spiffs_ok)) {
        Serial.println("[boot] WARNING: Radio init failed");
        radio_status = "Radio unavailable";
    }
#endif
    boot_status(radio_status);
    sigurdos_sdcard_lock_bus_reset();

    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Chats);
    boot_status("Loading chats...");
    sigurdos::ui::load_persisted_state();
    boot_status("Chats ready");

#if SIGURDOS_DEBUG_DIAG
    sigurdos::hal::boot_watchdog_progress(
        sigurdos::hal::BootStage::Diagnostics);
    sigurdos::debug::init();
    boot_log("debug diagnostics enabled");
#endif

    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Map);
    boot_status("Preparing map...");
    sigurdos_map_init();

    boot_status("Ready");
    boot_log("SigurdOS T-Deck ready");

    // Auto-connect WiFi if credentials are saved
    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Wifi);
    {
        const sigurdos::NodePrefs& p = sigurdos::prefs_get();
        if (p.wifi_ssid[0]) {
            sigurdos::wifi_sta::beginConnect(p.wifi_ssid, p.wifi_password);
        }
    }

    // ── Phase 1 companion transports (TCP + WebSocket registry) ───────
    sigurdos::comms::transports_init();

#if SIGURDOS_TELEMETRY
    sigurdos::hal::boot_watchdog_progress(sigurdos::hal::BootStage::Telemetry);
    sigurdos::telemetry::init();
#endif
    sigurdos::ota_boot_health::markCoreReady();
    sigurdos::hal::boot_watchdog_enter_runtime();
}

void loop()
{
    // Loop timing — measure from start of loop
#if SIGURDOS_TELEMETRY
    uint32_t loop_start_us = micros();
#endif
    sigurdos::wifi::servicePendingReleases();
    // Low-battery auto-shutdown (matches MeshCore pattern)
    static uint32_t last_batt_check = 0;
    if (millis() - last_batt_check > 30000) {  // every 30s
        last_batt_check = millis();
        if (board.isBatteryCritical()) {
            Serial.println("CRITICAL: Battery low — entering deep sleep");
            enter_orderly_sleep();
        }
    }

    // Process display/LVGL first so UI stays responsive during mesh ops
    sigurdos_display_loop();
    sigurdos::ui::loop();
    sigurdos::hal::buzzer_loop();  // non-blocking beep pattern playback
    sigurdos::ota::loop();         // WiFi OTA web server
    sigurdos::github_ota::loop();  // GitHub OTA downloader
    sigurdos::wifi_sta::loop();    // WiFi STA maintenance
    // ── Phase 1 companion transports (TCP + WebSocket registry) ───────
    sigurdos::comms::transports_loop();
    {   // WiFi icon refresh — 1 Hz is plenty for an RSSI readout
        static uint32_t last_wifi_ui = 0;
        if (millis() - last_wifi_ui >= 1000) {
            last_wifi_ui = millis();
            sigurdos::ui::update_wifi_status();  // bottom bar WiFi icon
        }
    }
    {   // Persisted background cadence plus explicit map/time-sync demand.
        const sigurdos::NodePrefs& gp = sigurdos::prefs_get();
        const bool track_recording = gp.gps_track_enabled;
        const bool gps_background_enabled = gp.gps_enabled || track_recording;
        const uint32_t gps_interval =
            sigurdos::app::gpsTrackGpsDemandInterval(
                gp.gps_enabled, gp.gps_interval, track_recording,
                gp.gps_track_interval);
        sigurdos_gps_service(gps_background_enabled, gps_interval);
        sigurdos::app::gpsTrackService(
            track_recording, gp.gps_track_interval, sigurdos_gps_has_fix(),
            sigurdos_gps_latitude(), sigurdos_gps_longitude(),
            sigurdos::mesh::getCurrentTime(), millis());
        const bool gps_time_requested = gps_background_enabled ||
            sigurdos_gps_time_sync_status() == SigurdOSGpsSyncStatus::Waiting;
        if (gps_time_requested) {
            sigurdos::app::serviceGpsClock(
                true,
                [](SigurdOSGpsUtcTime* out) { return sigurdos_gps_get_pending_time(out); },
                [](int year, int month, int day, int hour, int minute) {
                    return sigurdos::mesh::makeEpoch(year, month, day, hour, minute);
                },
                [](uint32_t epoch) {
                    return sigurdos::mesh::setSystemTime(
                        epoch, sigurdos::mesh::TimeSource::GPS);
                },
                [] { sigurdos_gps_mark_time_synced(); });
        }
    }
    sigurdos::mesh::loop();
#if defined(SIGURDOS_REMOTE_TEST) && SIGURDOS_REMOTE_TEST
    sigurdos_test_controller_loop();
#endif

#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::loop();
    // Report loop timing after telemetry processing.
    uint32_t loop_elapsed_us = micros() - loop_start_us;
    sigurdos::telemetry::report_loop_timing(loop_elapsed_us);
#endif
#if SIGURDOS_DEBUG_DIAG
    sigurdos::debug::loop();
#endif
    sigurdos::ota_boot_health::loop();
    sigurdos::diagnostics::drain_diagnostic_output();
    sigurdos::hal::boot_watchdog_runtime_progress();

    // ── Phase 3 screen sleep / lock integration ───────────────
    // This is deliberately the last loop hook: radio RX and all existing
    // service work run before the bounded vTaskDelay.
    sigurdos::power::loopEnd(millis());
}
