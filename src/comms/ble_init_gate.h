#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>

namespace sigurdos {
namespace comms {

enum class BleInitState : uint8_t {
    Unconfigured,
    Ready,
    Initializing,
    Initialized,
    Failed,
};

// Keeps BLE configuration separate from controller initialization. The caller
// supplies the target-specific initializer only when BLE is actually enabled.
class BleInitGate {
public:
    explicit BleInitGate(uint32_t retry_delay_ms = 1000)
        : _retry_delay_ms(retry_delay_ms) {}

    void configure()
    {
        _state = BleInitState::Ready;
        _retry_at_ms = 0;
    }

    template <typename Initializer>
    bool ensureInitialized(uint32_t now_ms, Initializer initializer)
    {
        if (_state == BleInitState::Initialized) return true;
        if (_state == BleInitState::Unconfigured ||
            _state == BleInitState::Initializing) {
            return false;
        }
        if (_state == BleInitState::Failed &&
            static_cast<int32_t>(now_ms - _retry_at_ms) < 0) {
            return false;
        }

        _state = BleInitState::Initializing;
        if (!initializer()) {
            _state = BleInitState::Failed;
            _retry_at_ms = now_ms + _retry_delay_ms;
            return false;
        }
        _state = BleInitState::Initialized;
        return true;
    }

    bool configured() const { return _state != BleInitState::Unconfigured; }
    bool initialized() const { return _state == BleInitState::Initialized; }
    BleInitState state() const { return _state; }
    uint32_t retryAtMs() const { return _retry_at_ms; }

private:
    BleInitState _state = BleInitState::Unconfigured;
    uint32_t _retry_delay_ms;
    uint32_t _retry_at_ms = 0;
};

} // namespace comms
} // namespace sigurdos
