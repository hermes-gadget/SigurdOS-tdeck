#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#if defined(ESP32_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <mutex>
#endif

namespace sigurdos {
namespace comms {

// A task-level recursive mutex. BLE operations can call into the stack and
// must not run while a FreeRTOS spinlock/critical section is held.
class BleTaskMutex {
public:
    class Guard {
    public:
        explicit Guard(const BleTaskMutex& mutex) : _mutex(mutex)
        {
            _mutex.lock();
        }

        ~Guard() { _mutex.unlock(); }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        const BleTaskMutex& _mutex;
    };

    BleTaskMutex()
    {
#if defined(ESP32_PLATFORM)
        _handle = xSemaphoreCreateRecursiveMutexStatic(&_storage);
        configASSERT(_handle != nullptr);
#endif
    }

    BleTaskMutex(const BleTaskMutex&) = delete;
    BleTaskMutex& operator=(const BleTaskMutex&) = delete;

private:
    void lock() const
    {
#if defined(ESP32_PLATFORM)
        xSemaphoreTakeRecursive(_handle, portMAX_DELAY);
#else
        _mutex.lock();
#endif
    }

    void unlock() const
    {
#if defined(ESP32_PLATFORM)
        xSemaphoreGiveRecursive(_handle);
#else
        _mutex.unlock();
#endif
    }

#if defined(ESP32_PLATFORM)
    mutable StaticSemaphore_t _storage{};
    mutable SemaphoreHandle_t _handle = nullptr;
#else
    mutable std::recursive_mutex _mutex;
#endif
};

} // namespace comms
} // namespace sigurdos
