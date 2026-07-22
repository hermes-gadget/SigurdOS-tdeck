// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos::diagnostics {

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
    uint32_t dropped_records() const { return dropped_records_; }
    void reset();

private:
    char record_[RECORD_CAPACITY]{};
    uint8_t queue_[QUEUE_CAPACITY]{};
    std::size_t record_size_ = 0;
    std::size_t queue_head_ = 0;
    std::size_t queue_tail_ = 0;
    std::size_t queue_size_ = 0;
    uint32_t dropped_records_ = 0;
    uint32_t reported_drops_ = 0;
    bool record_overflow_ = false;

    void append(const char* data, std::size_t length);
    void commit();
};

NonBlockingWriter& writer();
void drain_diagnostic_output(std::size_t byte_budget = 256);
void diagnostic_logf(const char* prefix, const char* format, ...);

}  // namespace sigurdos::diagnostics
