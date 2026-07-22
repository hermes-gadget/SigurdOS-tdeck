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

NonBlockingWriter& writer() { return s_writer; }

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
    if (value) append(value, strlen(value));
}
void NonBlockingWriter::print(char value) { append(&value, 1); }
void NonBlockingWriter::print(int value) { printf("%d", value); }
void NonBlockingWriter::print(unsigned int value) { printf("%u", value); }
void NonBlockingWriter::print(long value) { printf("%ld", value); }
void NonBlockingWriter::print(unsigned long value) { printf("%lu", value); }

void NonBlockingWriter::println() {
    append("\n", 1);
    commit();
    drain(RECORD_CAPACITY);
}

void NonBlockingWriter::println(const char* value) {
    print(value);
    println();
}

std::size_t NonBlockingWriter::write(uint8_t value) {
    const char byte = static_cast<char>(value);
    append(&byte, 1);
    return record_overflow_ ? 0 : 1;
}

void NonBlockingWriter::printf(const char* format, ...) {
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
        drain(RECORD_CAPACITY);
    }
}

void NonBlockingWriter::drain(std::size_t byte_budget) {
    while (queue_size_ > 0 && byte_budget > 0) {
        const int available = Serial.availableForWrite();
        if (available <= 0) return;
        std::size_t contiguous = QUEUE_CAPACITY - queue_tail_;
        if (contiguous > queue_size_) contiguous = queue_size_;
        if (contiguous > byte_budget) contiguous = byte_budget;
        if (contiguous > static_cast<std::size_t>(available)) {
            contiguous = static_cast<std::size_t>(available);
        }
        if (contiguous == 0) return;
        const std::size_t written = Serial.write(queue_ + queue_tail_, contiguous);
        if (written == 0) return;
        queue_tail_ = (queue_tail_ + written) % QUEUE_CAPACITY;
        queue_size_ -= written;
        byte_budget -= written;
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

void NonBlockingWriter::flush() { drain(QUEUE_CAPACITY); }

void NonBlockingWriter::reset() {
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
    writer().print(message);
    writer().println();
}

}  // namespace sigurdos::diagnostics
