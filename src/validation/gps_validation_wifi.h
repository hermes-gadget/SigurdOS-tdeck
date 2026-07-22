#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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

enum class WifiState : uint8_t { Disabled, Connecting, Disconnected, Connected };
enum class UploadPhase : uint8_t { Idle, Header, Body, Response };

inline ConnectPollResult pollConnect(bool connected, uint32_t elapsed_ms);

inline WifiState advanceWifiState(WifiState state, bool connected,
                                  uint32_t connect_elapsed_ms,
                                  bool reconnect_due) {
    if (state == WifiState::Connected && !connected) return WifiState::Disconnected;
    if (state == WifiState::Connecting) {
        const ConnectPollResult result = pollConnect(connected, connect_elapsed_ms);
        if (result == ConnectPollResult::Connected) return WifiState::Connected;
        if (result == ConnectPollResult::TimedOut) return WifiState::Disconnected;
    }
    if (state == WifiState::Disconnected && reconnect_due) return WifiState::Connecting;
    return state;
}

inline size_t boundedWriteSize(size_t total, size_t offset, size_t budget) {
    if (offset >= total || budget == 0) return 0;
    const size_t remaining = total - offset;
    return remaining < budget ? remaining : budget;
}

inline bool advanceWriteOffset(size_t total, size_t written, size_t* offset) {
    if (!offset || written == 0 || *offset > total || written > total - *offset) return false;
    *offset += written;
    return true;
}

inline int parseHttpStatusLine(const char* line) {
    if (!line) return 0;
    unsigned major = 0;
    unsigned minor = 0;
    int status = 0;
    int consumed = 0;
    if (sscanf(line, "HTTP/%u.%u %d%n", &major, &minor, &status, &consumed) != 3 ||
        status < 100 || status > 599) return 0;
    const char tail = line[consumed];
    return tail == '\0' || tail == ' ' || tail == '\t' ? status : 0;
}

inline bool responseExpired(bool connected, size_t available,
                            uint32_t now_ms, uint32_t started_ms) {
    return (!connected && available == 0) ||
           static_cast<uint32_t>(now_ms - started_ms) > 2000U;
}

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
