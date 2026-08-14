// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(ESP32_PLATFORM)
#include <freertos/FreeRTOS.h>
#else
#include <mutex>
#endif

namespace sigurdos::diagnostics {

// Arduino-ESP32 HWCDC otherwise retries a full TX ring for roughly two
// seconds per write. Diagnostics are best-effort and must never stall the
// watchdog-owned loop task.
constexpr uint32_t DIAGNOSTIC_CDC_TX_TIMEOUT_MS = 0;

void configure_diagnostic_output();

class NonBlockingWriter {
public:
    static constexpr std::size_t RECORD_CAPACITY = 384;
    static constexpr std::size_t QUEUE_CAPACITY = 2048;
    void print(const char* value);
    void print(char value);
    void print(int value);
    void print(unsigned int value);
    void print(long value);
    void print(unsigned long value);
    void println();
    void println(const char* value);
    std::size_t write(uint8_t value);
    void printf(const char* format, ...);
    void flush();
    void drain(std::size_t byte_budget = 256);
    uint32_t dropped_records() const;
    void reset();

#if !defined(ESP32_PLATFORM)
    using DrainUnlockedHookForTest = void (*)(void*);
    void setDrainUnlockedHookForTest(DrainUnlockedHookForTest hook,
                                     void* context);
    std::size_t queuedBytesForTest() const;
#endif

private:
    void lock() const;
    void unlock() const;

    struct LockGuard {
        explicit LockGuard(const NonBlockingWriter& writer) : writer_(writer) {
            writer_.lock();
        }
        ~LockGuard() { writer_.unlock(); }
        const NonBlockingWriter& writer_;
    };

    char record_[RECORD_CAPACITY]{};
    uint8_t queue_[QUEUE_CAPACITY]{};
    std::size_t record_size_ = 0;
    std::size_t queue_head_ = 0;
    std::size_t queue_tail_ = 0;
    std::size_t queue_size_ = 0;
    uint32_t dropped_records_ = 0;
    uint32_t reported_drops_ = 0;
    bool record_overflow_ = false;
    bool drain_active_ = false;

#if !defined(ESP32_PLATFORM)
    DrainUnlockedHookForTest drain_unlocked_hook_ = nullptr;
    void* drain_unlocked_hook_context_ = nullptr;
#endif

    void append(const char* data, std::size_t length);
    void commit();
    void drain_locked(std::size_t byte_budget);

#if defined(ESP32_PLATFORM)
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
#else
    mutable std::mutex mutex_;
#endif
};

NonBlockingWriter& writer();
void drain_diagnostic_output(std::size_t byte_budget = 256);
void diagnostic_logf(const char* prefix, const char* format, ...);

}  // namespace sigurdos::diagnostics
