#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace sigurdos::gps_validation {

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 15000;
constexpr size_t UPLOAD_QUEUE_CAPACITY = 4;
constexpr size_t UPLOAD_LINE_CAPACITY = 256;

enum class ConnectPollResult : uint8_t {
    Waiting,
    Connected,
    TimedOut,
};

inline ConnectPollResult pollConnect(bool connected, uint32_t elapsed_ms)
{
    if (connected) return ConnectPollResult::Connected;
    return elapsed_ms > WIFI_CONNECT_TIMEOUT_MS
        ? ConnectPollResult::TimedOut
        : ConnectPollResult::Waiting;
}

inline bool reconnectDue(uint32_t now_ms, uint32_t last_attempt_ms)
{
    return static_cast<uint32_t>(now_ms - last_attempt_ms)
        >= WIFI_RECONNECT_INTERVAL_MS;
}

// Validation status is generated faster than a failed HTTP endpoint can be
// retried. Keep a tiny fixed queue so reconnects cannot consume the heap or
// stall the GPS producer. A full queue drops the new record, preserving a
// record that may already be in flight.
class UploadQueue {
public:
    bool push(const char* line)
    {
        if (line == nullptr || line[0] == '\0') return false;

        size_t len = 0;
        while (len < UPLOAD_LINE_CAPACITY && line[len] != '\0') len++;
        if (len >= UPLOAD_LINE_CAPACITY || count_ >= UPLOAD_QUEUE_CAPACITY) {
            dropped_++;
            return false;
        }

        const size_t tail = (head_ + count_) % UPLOAD_QUEUE_CAPACITY;
        memcpy(lines_[tail], line, len + 1);
        count_++;
        return true;
    }

    const char* front() const
    {
        return count_ > 0 ? lines_[head_] : nullptr;
    }

    void pop()
    {
        if (count_ == 0) return;
        head_ = (head_ + 1) % UPLOAD_QUEUE_CAPACITY;
        count_--;
    }

    size_t size() const { return count_; }
    uint32_t dropped() const { return dropped_; }

private:
    char lines_[UPLOAD_QUEUE_CAPACITY][UPLOAD_LINE_CAPACITY] = {};
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t dropped_ = 0;
};

} // namespace sigurdos::gps_validation

void sigurdos_gps_validation_wifi_init();
void sigurdos_gps_validation_wifi_service();
void sigurdos_gps_validation_wifi_post_status(const char* line);
