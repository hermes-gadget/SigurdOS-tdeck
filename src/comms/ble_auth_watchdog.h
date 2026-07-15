#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>

#if defined(ESP32_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <mutex>
#endif

namespace sigurdos {
namespace comms {

static constexpr uint32_t BLE_AUTHENTICATION_TIMEOUT_MS = 30000;

// Tracks the unauthenticated interval after a physical BLE connection. State
// is synchronized because callbacks arm/cancel it while the app loop expires
// it. Expiry is consumed once so disconnect is never spammed.
class BleAuthWatchdog {
public:
    explicit BleAuthWatchdog(
        uint32_t timeout_ms = BLE_AUTHENTICATION_TIMEOUT_MS)
        : _timeout_ms(timeout_ms) {}

    void arm(uint16_t conn_id, uint32_t now_ms)
    {
        LockGuard guard(*this);
        _conn_id = conn_id;
        _deadline_ms = now_ms + _timeout_ms;
        _armed = true;
    }

    void cancel()
    {
        LockGuard guard(*this);
        _armed = false;
    }

    bool takeExpired(uint32_t now_ms, uint16_t& conn_id)
    {
        LockGuard guard(*this);
        if (!_armed || (int32_t)(now_ms - _deadline_ms) < 0) return false;
        conn_id = _conn_id;
        _armed = false;
        return true;
    }

    bool armed() const
    {
        LockGuard guard(*this);
        return _armed;
    }

private:
    void lock() const
    {
#if defined(ESP32_PLATFORM)
        portENTER_CRITICAL(&_mux);
#else
        _mutex.lock();
#endif
    }

    void unlock() const
    {
#if defined(ESP32_PLATFORM)
        portEXIT_CRITICAL(&_mux);
#else
        _mutex.unlock();
#endif
    }

    struct LockGuard {
        explicit LockGuard(const BleAuthWatchdog& watchdog)
            : _watchdog(watchdog) { _watchdog.lock(); }
        ~LockGuard() { _watchdog.unlock(); }
        const BleAuthWatchdog& _watchdog;
    };

    const uint32_t _timeout_ms;
    uint32_t _deadline_ms = 0;
    uint16_t _conn_id = 0;
    bool _armed = false;
#if defined(ESP32_PLATFORM)
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
#else
    mutable std::mutex _mutex;
#endif
};

} // namespace comms
} // namespace sigurdos
