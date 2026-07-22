#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.

#include <cstdint>
#include <Arduino.h>
#include <Wire.h>
#ifdef ESP32_PLATFORM
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#endif
#ifdef SIGURDOS_TDECK
#include <SD.h>
#include <SPI.h>
#include "tdeck_pins.h"
#include "battery.h"
#include "i2c_bus.h"
#include "spi_shared.h"
#include <helpers/ESP32Board.h>
#endif

namespace sigurdos {

static constexpr uint16_t SIGURDOS_TDECK_AUTO_SHUTDOWN_MV = 3200;

inline bool tdeck_battery_mv_is_critical(uint16_t millivolts) {
    return millivolts > 0 && millivolts < SIGURDOS_TDECK_AUTO_SHUTDOWN_MV;
}

inline bool tdeck_should_resleep_early(bool deep_sleep_reset,
                                      bool timer_wakeup,
                                      uint16_t battery_mv) {
    return deep_sleep_reset && timer_wakeup &&
           tdeck_battery_mv_is_critical(battery_mv);
}

inline uint32_t tdeck_sleep_wake_seconds(uint32_t requested_seconds) {
    return requested_seconds > 0 ? requested_seconds : 900U;
}

inline bool tdeck_wake_configuration_succeeded(int error) {
    return error == 0;
}

enum class TDeckSleepStatus : uint8_t {
    Ready,
    Inhibited,
    WakeConfigurationFailed,
    PeripheralRailHoldFailed,
};

inline TDeckSleepStatus tdeck_sleep_preflight(bool inhibited,
                                               int timer_error) {
    if (inhibited) return TDeckSleepStatus::Inhibited;
    if (!tdeck_wake_configuration_succeeded(timer_error)) {
        return TDeckSleepStatus::WakeConfigurationFailed;
    }
    return TDeckSleepStatus::Ready;
}

inline const char* tdeck_sleep_status_name(TDeckSleepStatus status) {
    switch (status) {
    case TDeckSleepStatus::Ready:
        return "ready";
    case TDeckSleepStatus::Inhibited:
        return "inhibited";
    case TDeckSleepStatus::WakeConfigurationFailed:
        return "wake configuration failed";
    case TDeckSleepStatus::PeripheralRailHoldFailed:
        return "peripheral rail hold failed";
    }
    return "unknown";
}

#ifdef SIGURDOS_TDECK
class TDeckBoard : public ESP32Board {
    uint8_t  _startup_reason;
    bool     _inhibit_sleep;

    // Low-battery auto-shutdown (matches MeshCore's AUTO_SHUTDOWN_MILLIVOLTS pattern)
    static constexpr uint16_t AUTO_SHUTDOWN_MV = SIGURDOS_TDECK_AUTO_SHUTDOWN_MV;
    bool _shutdown_pending = false;

    static void highImpedance(int pin) {
        if (sigurdos_gpio_is_valid(pin)) pinMode(pin, INPUT);
    }

    static void quiescePeripheralRail() {
        // Silence outputs and deassert every chip-select while the rail is
        // still present. This prevents a final peripheral transaction during
        // bus shutdown.
        pinMode(PIN_TFT_BL, OUTPUT);
        digitalWrite(PIN_TFT_BL, LOW);
        pinMode(PIN_BUZZER, OUTPUT);
        digitalWrite(PIN_BUZZER, LOW);

        const int chip_selects[] = {PIN_LORA_NSS, PIN_TFT_CS, PIN_SD_CS};
        for (int pin : chip_selects) {
            if (!sigurdos_gpio_is_valid(pin)) continue;
            pinMode(pin, OUTPUT);
            digitalWrite(pin, HIGH);
        }

        // Stop clients before releasing the shared signal pins. SD must
        // unmount before its underlying SPI bus is ended.
        SD.end();
        Wire.end();
        Serial1.flush();
        Serial1.end();
        sigurdos_shared_spi().end();

        // No MCU output may remain connected to an unpowered peripheral.
        // Backlight and buzzer stay actively low; all data/control pins become
        // high impedance before the peripheral rail is removed.
        const int signal_pins[] = {
            PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RESET, PIN_LORA_BUSY,
            PIN_LORA_SCLK, PIN_LORA_MISO, PIN_LORA_MOSI,
            PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST,
            PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_INT, PIN_TOUCH_RST,
            PIN_GPS_RX, PIN_GPS_TX, PIN_SD_CS,
        };
        for (int pin : signal_pins) highImpedance(pin);
    }

    [[noreturn]] static void enterDeepSleep() {
        esp_deep_sleep_start();

        // ESP-IDF documents this call as non-returning. If a platform fault
        // violates that contract, stay out of the application loop because
        // the peripheral rail and buses have already been shut down.
        while (true) delay(1000);
    }

public:
    TDeckBoard() : _startup_reason(BD_STARTUP_NORMAL), _inhibit_sleep(false) {}

    void begin() {
        _startup_reason = BD_STARTUP_NORMAL;
        _shutdown_pending = false;

        // Preload the active level before releasing a deep-sleep pad hold.
        // ESP-IDF requires this order to prevent a low glitch on wake.
        pinMode(PIN_PERIPH_PWR, OUTPUT);
        digitalWrite(PIN_PERIPH_PWR, HIGH);
#ifdef ESP32_PLATFORM
        const esp_err_t rail_release_error = gpio_hold_dis(
            static_cast<gpio_num_t>(PIN_PERIPH_PWR));
        gpio_deep_sleep_hold_dis();
        if (!tdeck_wake_configuration_succeeded(rail_release_error)) {
            Serial.printf("[power] peripheral rail hold release failed: %d\n",
                          rail_release_error);
        }
#endif

        // Trackball button as input with internal pull-up (GPIO 0 shared with BOOT)
        pinMode(PIN_TRACKBALL, INPUT_PULLUP);

        // LoRa DIO1 pullup
        pinMode(PIN_LORA_DIO1, INPUT_PULLUP);

        // Battery ADC
        analogReadResolution(12);
        adcAttachPin(PIN_BAT_ADC);

        // Recover before Wire owns the pins, then start the shared bus with a
        // bounded transaction timeout for touch and keyboard traffic.
        i2c::begin();

        // Detect wake from deep sleep (matches MeshCore TDeckBoard pattern).
        // GPIO45 (DIO1) is not RTC-capable on ESP32-S3 (RTC GPIOs are 0-21),
        // so the ext1 wake path is guarded and BD_STARTUP_RX_PACKET is never
        // reached on this hardware. Timer-wake detection is handled by the
        // caller (e.g. critical-battery re-check after the 15-min safety wake).
        esp_reset_reason_t reason = esp_reset_reason();
        if (reason == ESP_RST_DEEPSLEEP) {
            if (rtc_gpio_is_valid_gpio((gpio_num_t)PIN_LORA_DIO1)) {
                uint64_t wakeup_source = esp_sleep_get_ext1_wakeup_status();
                if (wakeup_source & SIGURDOS_LORA_DIO1_WAKE_MASK) {
                    _startup_reason = BD_STARTUP_RX_PACKET;
                }
                rtc_gpio_deinit((gpio_num_t)PIN_LORA_DIO1);
            }
            if (rtc_gpio_is_valid_gpio((gpio_num_t)PIN_LORA_NSS)) {
                rtc_gpio_hold_dis((gpio_num_t)PIN_LORA_NSS);
            }
        }
    }

    // Returns true if battery is critically low and device should shut down
    bool isBatteryCritical() {
        if (_shutdown_pending) return true;
        uint16_t mv = getBattMilliVolts();
        if (tdeck_battery_mv_is_critical(mv)) {
            _shutdown_pending = true;
            return true;
        }
        return false;
    }

    uint16_t getBattMilliVolts() override {
        // Delegate to sigurdos_battery_mv() for consistent efuse-calibrated
        // ADC reading. Previously had duplicated analogReadMilliVolts logic.
        return sigurdos_battery_mv();
    }

    float getMCUTemperature() override {
        uint32_t raw = 0;
        for (int i = 0; i < 4; i++) raw += temperatureRead();
        return (float)raw / 4.0f;
    }

    const char* getManufacturerName() const override {
        return "LilyGo T-Deck";
    }

    uint8_t getStartupReason() const override {
        return _startup_reason;
    }

    void reboot() override {
        esp_restart();
    }

    // Returns only when sleep preparation fails. A successful transition is
    // explicitly non-returning via enterDeepSleep().
    TDeckSleepStatus trySleep(uint32_t secs) {
        if (_inhibit_sleep) {
            Serial.println("[power] deep sleep inhibited");
            return TDeckSleepStatus::Inhibited;
        }

        // Validate the mandatory recovery wake source before touching powered
        // peripherals. If configuration fails, leave the device fully powered
        // so the caller can log/retry instead of entering an unwakeable state.
        const uint32_t wake_secs = tdeck_sleep_wake_seconds(secs);
        const esp_err_t timer_error =
            esp_sleep_enable_timer_wakeup(wake_secs * 1000000ULL);
        const TDeckSleepStatus preflight =
            tdeck_sleep_preflight(_inhibit_sleep, timer_error);
        if (preflight != TDeckSleepStatus::Ready) {
            Serial.printf("[power] timer wake setup failed: %d\n", timer_error);
            return preflight;
        }

        quiescePeripheralRail();

        // GPIO45 (DIO1) is not RTC-capable on ESP32-S3 (RTC GPIOs are
        // 0-21). Guard all RTC-GPIO/ext1 calls so they don't silently
        // fail with ESP_ERR_INVALID_ARG. Wake-on-packet from deep sleep
        // is not supported on the T-Deck because DIO1 is on a non-RTC pin.
        if (rtc_gpio_is_valid_gpio((gpio_num_t)PIN_LORA_DIO1)) {
            const esp_err_t direction_error = rtc_gpio_set_direction(
                (gpio_num_t)PIN_LORA_DIO1, RTC_GPIO_MODE_INPUT_ONLY);
            const esp_err_t pull_error =
                rtc_gpio_pulldown_en((gpio_num_t)PIN_LORA_DIO1);
            const esp_err_t ext1_error = esp_sleep_enable_ext1_wakeup(
                SIGURDOS_LORA_DIO1_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);
            if (!tdeck_wake_configuration_succeeded(direction_error) ||
                !tdeck_wake_configuration_succeeded(pull_error) ||
                !tdeck_wake_configuration_succeeded(ext1_error)) {
                Serial.printf(
                    "[power] optional radio wake setup failed: dir=%d pull=%d ext1=%d\n",
                    direction_error, pull_error, ext1_error);
            }
        }

        // Only now remove peripheral power. All connected outputs are already
        // low or high impedance, preventing GPIO back-power into the dead rail.
        pinMode(PIN_PERIPH_PWR, OUTPUT);
        digitalWrite(PIN_PERIPH_PWR, LOW);
        delay(10);
#ifdef ESP32_PLATFORM
        const esp_err_t hold_error = gpio_hold_en(
            static_cast<gpio_num_t>(PIN_PERIPH_PWR));
        if (!tdeck_wake_configuration_succeeded(hold_error)) {
            Serial.printf("[power] peripheral rail hold failed: %d\n",
                          hold_error);
            return TDeckSleepStatus::PeripheralRailHoldFailed;
        }
        // The per-pad hold latches GPIO 10's active LOW configuration; this
        // global gate makes that hold effective while digital GPIO powers down.
        gpio_deep_sleep_hold_en();
#endif
        enterDeepSleep();
    }

    void sleep(uint32_t secs) override {
        // MeshCore's virtual API cannot report failure. Keep callers out of a
        // partially quiesced application and retry the checked transition.
        while (true) {
            const TDeckSleepStatus status = trySleep(secs);
            Serial.printf("[power] deep-sleep attempt failed (%s); retrying\n",
                          tdeck_sleep_status_name(status));
            delay(5000);
        }
    }

    void setInhibitSleep(bool inhibit) { _inhibit_sleep = inhibit; }
};
#endif // SIGURDOS_TDECK

} // namespace sigurdos
