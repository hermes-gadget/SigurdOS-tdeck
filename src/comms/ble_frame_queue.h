#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "secure_wipe.h"

#if defined(ESP32_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <mutex>
#endif

namespace sigurdos {
namespace comms {

enum class BleFrameQueuePushResult : uint8_t {
    Accepted,
    InvalidFrame,
    Full,
};

enum class BleRxAdmissionAction : uint8_t {
    Accepted,
    DisconnectPeer,
};

// Arduino-ESP32 acknowledges writes before invoking onWrite(), so the callback
// cannot return ATT backpressure. A rejected frame must terminate the session
// to make failure visible and force the client to resynchronize.
inline BleRxAdmissionAction rxAdmissionAction(BleFrameQueuePushResult result)
{
    return result == BleFrameQueuePushResult::Accepted
        ? BleRxAdmissionAction::Accepted
        : BleRxAdmissionAction::DisconnectPeer;
}

// Bounded FIFO carrying BLE frames across the Bluedroid host-task to
// app-loop-task boundary (audit NET-002, issue #813). Both sides run in task
// context, so the target locks with a FreeRTOS critical section — the hold
// time is one bounded memcpy. The native build locks with std::mutex so tests
// can stress producer/consumer with real threads.
template <size_t MAX_LEN, size_t CAPACITY>
class BleFrameQueue {
public:
    // Producer side (BLE host task). Reports queue saturation separately so
    // callers can apply backpressure instead of silently dropping a frame.
    BleFrameQueuePushResult tryPush(const uint8_t* data, size_t len)
    {
        if (!data || len == 0 || len > MAX_LEN) {
            return BleFrameQueuePushResult::InvalidFrame;
        }
        LockGuard guard(*this);
        if (_count >= CAPACITY) return BleFrameQueuePushResult::Full;
        Frame& slot = _frames[(_head + _count) % CAPACITY];
        slot.len = (uint16_t)len;
        memcpy(slot.buf, data, len);
        _count++;
        return BleFrameQueuePushResult::Accepted;
    }

    bool push(const uint8_t* data, size_t len)
    {
        return tryPush(data, len) == BleFrameQueuePushResult::Accepted;
    }

    // Consumer side (app loop task). Copies the oldest frame into dest,
    // which must hold at least MAX_LEN bytes. Returns 0 when empty.
    size_t pop(uint8_t* dest)
    {
        if (!dest) return 0;
        LockGuard guard(*this);
        if (_count == 0) return 0;
        Frame& slot = _frames[_head];
        const size_t len = slot.len;
        memcpy(dest, slot.buf, len);
        secureWipe(&slot, sizeof(slot));
        _head = (_head + 1) % CAPACITY;
        _count--;
        return len;
    }

    void clear()
    {
        LockGuard guard(*this);
        _head = 0;
        _count = 0;
        secureWipe(_frames, sizeof(_frames));
    }

    size_t size() const
    {
        LockGuard guard(*this);
        return _count;
    }

    bool storageClearedForTest() const
    {
        LockGuard guard(*this);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(_frames);
        for (size_t i = 0; i < sizeof(_frames); ++i) {
            if (bytes[i] != 0) return false;
        }
        return true;
    }

private:
    struct Frame {
        uint16_t len;
        uint8_t buf[MAX_LEN];
    };

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
        const BleFrameQueue& queue;
        explicit LockGuard(const BleFrameQueue& q) : queue(q) { queue.lock(); }
        ~LockGuard() { queue.unlock(); }
    };

    Frame _frames[CAPACITY]{};
    size_t _head = 0;
    size_t _count = 0;
#if defined(ESP32_PLATFORM)
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
#else
    mutable std::mutex _mutex;
#endif
};

} // namespace comms
} // namespace sigurdos
