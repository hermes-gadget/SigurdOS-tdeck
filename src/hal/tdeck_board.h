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
#include <driver/rtc_io.h>
#endif
#ifdef SIGURDOS_TDECK
#include "tdeck_pins.h"
#include "battery.h"
#include "i2c_bus.h"
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

#ifdef SIGURDOS_TDECK
class TDeckBoard : public ESP32Board {
    uint8_t  _startup_reason;
    bool     _inhibit_sleep;

    // Low-battery auto-shutdown (matches MeshCore's AUTO_SHUTDOWN_MILLIVOLTS pattern)
    static constexpr uint16_t AUTO_SHUTDOWN_MV = SIGURDOS_TDECK_AUTO_SHUTDOWN_MV;
    bool _shutdown_pending = false;

public:
    TDeckBoard() : _startup_reason(BD_STARTUP_NORMAL), _inhibit_sleep(false) {}

    void begin() {
        _startup_reason = BD_STARTUP_NORMAL;
        _shutdown_pending = false;

        // Enable peripheral power
        pinMode(PIN_PERIPH_PWR, OUTPUT);
        digitalWrite(PIN_PERIPH_PWR, HIGH);

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

    void sleep(uint32_t secs) override {
        if (_inhibit_sleep) return;
        // Power down peripherals to save battery during deep sleep
        digitalWrite(PIN_PERIPH_PWR, LOW);
        // Optionally turn off display backlight
        pinMode(PIN_TFT_BL, OUTPUT);
        digitalWrite(PIN_TFT_BL, LOW);

        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

        // GPIO45 (DIO1) is not RTC-capable on ESP32-S3 (RTC GPIOs are
        // 0-21). Guard all RTC-GPIO/ext1 calls so they don't silently
        // fail with ESP_ERR_INVALID_ARG. Wake-on-packet from deep sleep
        // is not supported on the T-Deck because DIO1 is on a non-RTC pin.
        if (rtc_gpio_is_valid_gpio((gpio_num_t)PIN_LORA_DIO1)) {
            rtc_gpio_set_direction((gpio_num_t)PIN_LORA_DIO1, RTC_GPIO_MODE_INPUT_ONLY);
            rtc_gpio_pulldown_en((gpio_num_t)PIN_LORA_DIO1);
            esp_sleep_enable_ext1_wakeup(SIGURDOS_LORA_DIO1_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);
        }

        // GPIO9 (NSS) IS RTC-capable — hold to prevent floating during sleep
        if (rtc_gpio_is_valid_gpio((gpio_num_t)PIN_LORA_NSS)) {
            rtc_gpio_hold_en((gpio_num_t)PIN_LORA_NSS);
        }

        // Always arm a timer wakeup so the device can recover even when
        // external wake (DIO1) is unavailable. For the critical-battery
        // path (secs == 0 → indefinite sleep), wake every 15 minutes to
        // re-check battery level and re-sleep if still below threshold.
        uint32_t wake_secs = (secs > 0) ? secs : 900;
        esp_sleep_enable_timer_wakeup(wake_secs * 1000000ULL);
        esp_deep_sleep_start();
    }

    void setInhibitSleep(bool inhibit) { _inhibit_sleep = inhibit; }
};
#endif // SIGURDOS_TDECK

} // namespace sigurdos
