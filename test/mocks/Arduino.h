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

// Mock Arduino.h for native PlatformIO testing
// Provides minimal stubs for all Arduino functions used by SigurdOS

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdarg>
#include <string>

// ── Types ────────────────────────────────────────────────
using byte = uint8_t;
using word = uint16_t;

typedef bool boolean;

// ── Pin modes ────────────────────────────────────────────
#define INPUT          0x01
#define OUTPUT         0x02
#define INPUT_PULLUP   0x05
#define INPUT_PULLDOWN 0x06

#define HIGH  1
#define LOW   0

#define LED_BUILTIN 2

#define SERIAL_8N1 0x800001c

// ── Mock state (manipulated by tests) ────────────────────
namespace arduino_mock {
    extern unsigned long current_millis;
    extern int pin_states[64];       // digital pin states
    extern int analog_values[16];    // analog pin readings
    extern bool serial_output[1024]; // serial TX buffer
    extern int serial_output_len;

    void reset();  // reset all mock state between tests
}

// ── Core functions ───────────────────────────────────────
inline unsigned long millis() { return arduino_mock::current_millis; }
inline unsigned long micros() { return arduino_mock::current_millis * 1000UL; }
inline void delay(unsigned long ms) { arduino_mock::current_millis += ms; }
inline void delayMicroseconds(unsigned int us) { arduino_mock::current_millis += us / 1000 + 1; }

// ── Digital I/O ──────────────────────────────────────────
inline void pinMode(uint8_t pin, uint8_t mode) {
    if (pin < 64) arduino_mock::pin_states[pin] = mode;
}
inline void digitalWrite(uint8_t pin, uint8_t val) {
    if (pin < 64) arduino_mock::pin_states[pin] = val;
}
inline int digitalRead(uint8_t pin) {
    return (pin < 64) ? arduino_mock::pin_states[pin] : 0;
}

// ── Analog I/O ───────────────────────────────────────────
inline void analogReadResolution(int bits) { (void)bits; }
inline int analogRead(uint8_t pin) {
    return (pin < 16) ? arduino_mock::analog_values[pin] : 0;
}
inline void adcAttachPin(uint8_t pin) { (void)pin; }

// ── Serial ───────────────────────────────────────────────
class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }
    virtual void flush() {}
    virtual size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t* buf, size_t len) { (void)buf; return len; }
};

class HardwareSerial : public Stream {
public:
    void begin(unsigned long baud) {
        _begun = true;
        _begin_count++;
        _last_baud = baud;
        _last_config = 0;
        _last_rx_pin = -1;
        _last_tx_pin = -1;
    }
    void begin(unsigned long baud, uint32_t config, int8_t rx_pin, int8_t tx_pin) {
        _begun = true;
        _begin_count++;
        _last_baud = baud;
        _last_config = config;
        _last_rx_pin = rx_pin;
        _last_tx_pin = tx_pin;
    }
    int available() override { return (int)(_rx_len - _rx_pos); }
    int read() override {
        if (_rx_pos < _rx_len) return _rx_buf[_rx_pos++];
        return -1;
    }
    size_t write(uint8_t b) override {
        _tx_buf.push_back((char)b);
        return 1;
    }
    size_t write(const uint8_t* buf, size_t len) {
        if (!buf) return 0;
        _tx_buf.append((const char*)buf, len);
        return len;
    }
    void print(const char* s) {
        if (s) _tx_buf += s;
    }
    void print(char c) { _tx_buf.push_back(c); }
    void print(int v) { append_format("%d", v); }
    void print(unsigned int v) { append_format("%u", v); }
    void print(long v) { append_format("%ld", v); }
    void print(unsigned long v) { append_format("%lu", v); }
    void println() { _tx_buf.push_back('\n'); }
    void println(const char* s) {
        print(s);
        println();
    }
    void printf(const char* fmt, ...) {
        if (!fmt) return;
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n <= 0) return;
        size_t len = (n < (int)sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;
        _tx_buf.append(buf, len);
    }
    operator bool() const { return true; }

    void mock_clear_rx() {
        _rx_pos = 0;
        _rx_len = 0;
        memset(_rx_buf, 0, sizeof(_rx_buf));
    }
    void mock_queue_rx(const char* data) {
        if (!data) return;
        while (*data && _rx_len < sizeof(_rx_buf)) {
            _rx_buf[_rx_len++] = *data++;
        }
    }
    void mock_clear_tx() { _tx_buf.clear(); }
    const std::string& mock_tx_output() const { return _tx_buf; }
    void mock_reset() {
        mock_clear_rx();
        mock_clear_tx();
        _begun = false;
        _begin_count = 0;
        _last_baud = 0;
        _last_config = 0;
        _last_rx_pin = -1;
        _last_tx_pin = -1;
    }
    bool mock_was_begun() const { return _begun; }
    int mock_begin_count() const { return _begin_count; }
    unsigned long mock_last_baud() const { return _last_baud; }
    uint32_t mock_last_config() const { return _last_config; }
    int8_t mock_last_rx_pin() const { return _last_rx_pin; }
    int8_t mock_last_tx_pin() const { return _last_tx_pin; }

private:
    template <typename T>
    void append_format(const char* fmt, T value) {
        char buf[32];
        snprintf(buf, sizeof(buf), fmt, value);
        _tx_buf += buf;
    }

    char _rx_buf[512] = {};
    size_t _rx_pos = 0;
    size_t _rx_len = 0;
    std::string _tx_buf;
    bool _begun = false;
    int _begin_count = 0;
    unsigned long _last_baud = 0;
    uint32_t _last_config = 0;
    int8_t _last_rx_pin = -1;
    int8_t _last_tx_pin = -1;
};
extern HardwareSerial Serial;
extern HardwareSerial Serial1;

// ── I2C ──────────────────────────────────────────────────
class TwoWire {
public:
    void begin() {}
    void begin(int, int) {}
    void setClock(uint32_t) {}

    // Master write
    void beginTransmission(uint8_t addr) {
        _tx_addr = addr;
        _tx_len = 0;
    }
    size_t write(uint8_t val) {
        if (_tx_len < 32) _tx_buf[_tx_len++] = val;
        return 1;
    }
    uint8_t endTransmission(bool stopBit = true) {
        (void)stopBit;
        return _end_error;
    }

    // Master read
    uint8_t requestFrom(uint8_t addr, uint8_t len) {
        _rx_addr = addr;
        _rx_pos = 0;
        // Copy queued bytes to read buffer
        uint8_t actual = (_q_len < len) ? _q_len : len;
        _rx_len = actual;
        for (uint8_t i = 0; i < actual; i++) {
            _rx_buf[i] = _q_buf[i];
        }
        _q_len = 0;  // consume queue
        return _rx_len;
    }
    int available() { return (int)(_rx_len - _rx_pos); }
    int read() {
        if (_rx_pos < _rx_len) return _rx_buf[_rx_pos++];
        return -1;
    }

    // ── Test control ──────────────────────────────────
    void mock_set_error(uint8_t err) { _end_error = err; }
    void mock_queue_rx_byte(uint8_t val) {
        if (_q_len < 32) _q_buf[_q_len++] = val;
    }
    uint8_t mock_last_tx_addr() const { return _tx_addr; }
    uint8_t mock_last_tx_data(int i) const {
        return (i >= 0 && i < (int)_tx_len) ? _tx_buf[i] : 0;
    }
    int mock_tx_len() const { return (int)_tx_len; }

private:
    uint8_t _tx_addr = 0;
    uint8_t _tx_buf[32] = {};
    size_t  _tx_len = 0;
    uint8_t _end_error = 0;

    uint8_t _rx_addr = 0;
    uint8_t _rx_buf[32] = {};
    size_t  _rx_pos = 0;
    size_t  _rx_len = 0;

    uint8_t _q_buf[32] = {};
    size_t  _q_len = 0;
};
extern TwoWire Wire;

// ── SPI ──────────────────────────────────────────────────
#define VSPI_HOST 2
#define SPI_DMA_CH_AUTO 0

class SPIClass {
public:
    void begin() {}
};
extern SPIClass SPI;

// ── ESP32 specific ───────────────────────────────────────
inline float temperatureRead() { return 45.0f; }
inline void esp_restart() {}
inline void esp_deep_sleep_start() {}

// Reset reasons
#define ESP_RST_POWERON   1
#define ESP_RST_DEEPSLEEP 5

typedef int esp_reset_reason_t;
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }

// RTC GPIO (stubs for deep sleep)
#define RTC_GPIO_MODE_INPUT_ONLY 0
typedef int gpio_num_t;
typedef int rtc_gpio_mode_t;
inline void rtc_gpio_set_direction(gpio_num_t, rtc_gpio_mode_t) {}
inline void rtc_gpio_pulldown_en(gpio_num_t) {}
inline void rtc_gpio_hold_en(gpio_num_t) {}
inline void rtc_gpio_hold_dis(gpio_num_t) {}
inline void rtc_gpio_deinit(gpio_num_t) {}

#define ESP_PD_DOMAIN_RTC_PERIPH 0
#define ESP_PD_OPTION_ON 0
typedef int esp_sleep_pd_domain_t;
inline void esp_sleep_pd_config(esp_sleep_pd_domain_t, int) {}

#define ESP_EXT1_WAKEUP_ANY_HIGH 0
inline void esp_sleep_enable_ext1_wakeup(uint64_t, int) {}
inline void esp_sleep_enable_timer_wakeup(uint64_t) {}

// ── Utilities ────────────────────────────────────────────
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define map(value, fromLow, fromHigh, toLow, toHigh) \
    ((value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow)
