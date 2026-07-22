#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>

namespace sigurdos {
namespace comms {

static constexpr uint32_t BLE_PIN_RESPONSE_GRACE_MS = 1000;
static constexpr uint32_t BLE_BOND_PURGE_RETRY_MS = 250;

// Small, wrap-safe scheduler used to delay a live bond purge until the PIN
// command response has had time to leave the notification queue. A persisted
// boot-time request skips that grace period and purges before advertising.
class BleBondRotationState {
public:
    void request(uint32_t now_ms, bool response_grace)
    {
        _pending = true;
        _purge_started = false;
        _next_action_ms = now_ms +
            (response_grace ? BLE_PIN_RESPONSE_GRACE_MS : 0u);
    }

    bool actionDue(uint32_t now_ms) const
    {
        return _pending && (int32_t)(now_ms - _next_action_ms) >= 0;
    }

    void purgeSubmitted(uint32_t now_ms)
    {
        _purge_started = true;
        _next_action_ms = now_ms + BLE_BOND_PURGE_RETRY_MS;
    }

    void retryLater(uint32_t now_ms)
    {
        _next_action_ms = now_ms + BLE_BOND_PURGE_RETRY_MS;
    }

    void expedite(uint32_t now_ms)
    {
        if (_pending && !_purge_started) _next_action_ms = now_ms;
    }

    void clear()
    {
        _pending = false;
        _purge_started = false;
        _next_action_ms = 0;
    }

    bool pending() const { return _pending; }
    bool purgeStarted() const { return _purge_started; }

private:
    uint32_t _next_action_ms = 0;
    bool _pending = false;
    bool _purge_started = false;
};

} // namespace comms
} // namespace sigurdos
