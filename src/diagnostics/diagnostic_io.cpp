// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagnostic_io.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace sigurdos::diagnostics {

namespace {
NonBlockingWriter s_writer;
}

void configure_diagnostic_output() {
#if defined(ESP32_PLATFORM) && defined(ARDUINO_USB_CDC_ON_BOOT) && \
    ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(DIAGNOSTIC_CDC_TX_TIMEOUT_MS);
#endif
}

NonBlockingWriter& writer() { return s_writer; }

void NonBlockingWriter::lock() const {
#if defined(ESP32_PLATFORM)
    portENTER_CRITICAL(&mux_);
#else
    mutex_.lock();
#endif
}

void NonBlockingWriter::unlock() const {
#if defined(ESP32_PLATFORM)
    portEXIT_CRITICAL(&mux_);
#else
    mutex_.unlock();
#endif
}

void NonBlockingWriter::append(const char* data, std::size_t length) {
    if (!data || length == 0 || record_overflow_) return;
    if (length > RECORD_CAPACITY - record_size_) {
        record_overflow_ = true;
        return;
    }
    memcpy(record_ + record_size_, data, length);
    record_size_ += length;
}

void NonBlockingWriter::commit() {
    if (record_overflow_ || record_size_ > QUEUE_CAPACITY - queue_size_) {
        if (dropped_records_ < UINT32_MAX) ++dropped_records_;
    } else {
        for (std::size_t i = 0; i < record_size_; ++i) {
            queue_[queue_head_] = static_cast<uint8_t>(record_[i]);
            queue_head_ = (queue_head_ + 1U) % QUEUE_CAPACITY;
        }
        queue_size_ += record_size_;
    }
    record_size_ = 0;
    record_overflow_ = false;
}

void NonBlockingWriter::print(const char* value) {
    LockGuard guard(*this);
    if (value) append(value, strlen(value));
}
void NonBlockingWriter::print(char value) {
    LockGuard guard(*this);
    append(&value, 1);
}
void NonBlockingWriter::print(int value) { printf("%d", value); }
void NonBlockingWriter::print(unsigned int value) { printf("%u", value); }
void NonBlockingWriter::print(long value) { printf("%ld", value); }
void NonBlockingWriter::print(unsigned long value) { printf("%lu", value); }

void NonBlockingWriter::println() {
    LockGuard guard(*this);
    append("\n", 1);
    commit();
    drain_locked(RECORD_CAPACITY);
}

void NonBlockingWriter::println(const char* value) {
    LockGuard guard(*this);
    if (value) append(value, strlen(value));
    append("\n", 1);
    commit();
    drain_locked(RECORD_CAPACITY);
}

std::size_t NonBlockingWriter::write(uint8_t value) {
    LockGuard guard(*this);
    const char byte = static_cast<char>(value);
    append(&byte, 1);
    return record_overflow_ ? 0 : 1;
}

void NonBlockingWriter::printf(const char* format, ...) {
    LockGuard guard(*this);
    if (!format || record_overflow_) return;
    char formatted[RECORD_CAPACITY];
    va_list args;
    va_start(args, format);
    const int result = vsnprintf(formatted, sizeof(formatted), format, args);
    va_end(args);
    if (result < 0 || static_cast<std::size_t>(result) >= sizeof(formatted)) {
        record_overflow_ = true;
        return;
    }
    append(formatted, static_cast<std::size_t>(result));
    if (result > 0 && formatted[result - 1] == '\n') {
        commit();
        drain_locked(RECORD_CAPACITY);
    }
}

void NonBlockingWriter::drain_locked(std::size_t byte_budget) {
    if (drain_active_) return;
    drain_active_ = true;
    struct DrainOwnership {
        explicit DrainOwnership(bool& active) : active_(active) {}
        ~DrainOwnership() { active_ = false; }
        bool& active_;
    } ownership(drain_active_);

    while (queue_size_ > 0 && byte_budget > 0) {
        // Driver queries and writes must stay outside the ESP32 critical
        // section. drain_active_ remains protected by the queue lock and keeps
        // the selected tail range exclusively owned while the lock is open.
        unlock();
        const int available = Serial.availableForWrite();
        lock();
        if (available <= 0) return;

        std::size_t to_write = queue_size_;
        if (to_write > byte_budget) to_write = byte_budget;
        if (to_write > static_cast<std::size_t>(available)) {
            to_write = static_cast<std::size_t>(available);
        }
        if (to_write == 0) return;

        // Copy drainable bytes from the ring buffer into a stack buffer
        // while the lock is still held (ring index math is the shared state).
        char drain_buf[256];
        std::size_t count = to_write;
        if (count > sizeof(drain_buf)) count = sizeof(drain_buf);
        std::size_t idx = queue_tail_;
        for (std::size_t i = 0; i < count; ++i) {
            drain_buf[i] = static_cast<char>(queue_[idx]);
            idx = (idx + 1) % QUEUE_CAPACITY;
        }

#if !defined(ESP32_PLATFORM)
        const DrainUnlockedHookForTest hook = drain_unlocked_hook_;
        void* const hook_context = drain_unlocked_hook_context_;
#endif
        unlock();
#if !defined(ESP32_PLATFORM)
        if (hook) hook(hook_context);
#endif
        const std::size_t written =
            Serial.write(reinterpret_cast<const uint8_t*>(drain_buf), count);
        lock();

        std::size_t consumed = written < count ? written : count;
        if (consumed > queue_size_) consumed = queue_size_;
        if (consumed == 0) return;
        queue_tail_ = (queue_tail_ + consumed) % QUEUE_CAPACITY;
        queue_size_ -= consumed;
        byte_budget -= consumed;
    }

    // Report losses only after the queued diagnostics have drained. The report is
    // itself queued atomically and will be written during a later bounded drain.
    if (queue_size_ == 0 && record_size_ == 0 && dropped_records_ != reported_drops_) {
        char report[96];
        const int length = snprintf(report, sizeof(report),
                                    "@alert|desc=diagnostic_output_dropped|n=%lu\n",
                                    static_cast<unsigned long>(dropped_records_));
        if (length > 0 && static_cast<std::size_t>(length) < sizeof(report)) {
            append(report, static_cast<std::size_t>(length));
            commit();
            reported_drops_ = dropped_records_;
        }
    }
}

void NonBlockingWriter::drain(std::size_t byte_budget) {
    LockGuard guard(*this);
    drain_locked(byte_budget);
}

void NonBlockingWriter::flush() {
    LockGuard guard(*this);
    drain_locked(QUEUE_CAPACITY);
}

uint32_t NonBlockingWriter::dropped_records() const {
    LockGuard guard(*this);
    return dropped_records_;
}

#if !defined(ESP32_PLATFORM)
void NonBlockingWriter::setDrainUnlockedHookForTest(
    DrainUnlockedHookForTest hook, void* context) {
    LockGuard guard(*this);
    drain_unlocked_hook_ = hook;
    drain_unlocked_hook_context_ = context;
}

std::size_t NonBlockingWriter::queuedBytesForTest() const {
    LockGuard guard(*this);
    return queue_size_;
}
#endif

void NonBlockingWriter::reset() {
    LockGuard guard(*this);
    record_size_ = 0;
    queue_head_ = queue_tail_ = queue_size_ = 0;
    dropped_records_ = 0;
    reported_drops_ = 0;
    record_overflow_ = false;
}

void drain_diagnostic_output(std::size_t byte_budget) {
    s_writer.drain(byte_budget);
}

void diagnostic_logf(const char* prefix, const char* format, ...) {
    char message[NonBlockingWriter::RECORD_CAPACITY];
    const int prefix_len = snprintf(message, sizeof(message), "%s", prefix ? prefix : "");
    if (prefix_len < 0 || static_cast<std::size_t>(prefix_len) >= sizeof(message)) return;
    va_list args;
    va_start(args, format);
    const int body_len = vsnprintf(message + prefix_len,
                                   sizeof(message) - static_cast<std::size_t>(prefix_len),
                                   format ? format : "", args);
    va_end(args);
    if (body_len < 0 || static_cast<std::size_t>(body_len) >=
                            sizeof(message) - static_cast<std::size_t>(prefix_len)) {
        writer().println("diagnostic log record exceeded bound");
        return;
    }
    writer().println(message);
}

}  // namespace sigurdos::diagnostics
