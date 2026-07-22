#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos {
namespace comms {

static constexpr uint32_t BLE_AUTH_BACKOFF_BASE_MS = 1000;
static constexpr uint32_t BLE_AUTH_BACKOFF_MAX_MS = 5 * 60 * 1000;
static constexpr uint32_t BLE_PAIRING_WINDOW_MS = 2 * 60 * 1000;

struct BlePeerAddress {
    uint8_t bytes[6]{};

    bool operator==(const BlePeerAddress& other) const
    {
        return std::memcmp(bytes, other.bytes, sizeof(bytes)) == 0;
    }
};

// Authentication abuse policy. Known bonds can reconnect outside a pairing
// window, while a new bond must be initiated locally. Both are subject to a
// bounded global/peer cooldown after failed or abandoned authentication.
class BleAuthThrottle {
public:
    void openPairingWindow(uint32_t now_ms)
    {
        reset();
        _pairing_deadline_ms = now_ms + BLE_PAIRING_WINDOW_MS;
        _pairing_window_open = true;
    }

    bool pairingWindowOpen(uint32_t now_ms) const
    {
        return _pairing_window_open && before(now_ms, _pairing_deadline_ms);
    }

    bool attemptAllowed(const BlePeerAddress& peer, bool known_bond,
                        uint32_t now_ms) const
    {
        if (blocked(now_ms)) return false;
        const PeerState* state = find(peer);
        if (state && state->failures > 0 && before(now_ms, state->blocked_until_ms)) {
            return false;
        }
        return known_bond || pairingWindowOpen(now_ms);
    }

    uint32_t recordFailure(const BlePeerAddress& peer, uint32_t now_ms)
    {
        if (_global_failures < 31) ++_global_failures;
        PeerState& state = findOrReplace(peer);
        if (state.failures < 31) ++state.failures;
        const uint8_t exponent = maxCount(_global_failures, state.failures) - 1;
        uint32_t delay = BLE_AUTH_BACKOFF_BASE_MS;
        for (uint8_t i = 0; i < exponent && delay < BLE_AUTH_BACKOFF_MAX_MS; ++i) {
            delay = delay > BLE_AUTH_BACKOFF_MAX_MS / 2
                        ? BLE_AUTH_BACKOFF_MAX_MS : delay * 2;
        }
        if (delay > BLE_AUTH_BACKOFF_MAX_MS) delay = BLE_AUTH_BACKOFF_MAX_MS;
        _blocked_until_ms = now_ms + delay;
        state.blocked_until_ms = _blocked_until_ms;
        return delay;
    }

    void recordSuccess() { reset(); }

    void reset()
    {
        _global_failures = 0;
        _blocked_until_ms = 0;
        for (PeerState& state : _peers) state = PeerState{};
        _pairing_deadline_ms = 0;
        _pairing_window_open = false;
    }

    bool blocked(uint32_t now_ms) const
    {
        return _global_failures > 0 && before(now_ms, _blocked_until_ms);
    }

    uint8_t globalFailureCount() const { return _global_failures; }

    uint8_t peerFailureCount(const BlePeerAddress& peer) const
    {
        const PeerState* state = find(peer);
        return state ? state->failures : 0;
    }

private:
    struct PeerState {
        BlePeerAddress address{};
        uint32_t blocked_until_ms = 0;
        uint8_t failures = 0;
        bool used = false;
    };

    static bool before(uint32_t now_ms, uint32_t deadline_ms)
    {
        return static_cast<int32_t>(now_ms - deadline_ms) < 0;
    }

    static uint8_t maxCount(uint8_t a, uint8_t b) { return a > b ? a : b; }

    const PeerState* find(const BlePeerAddress& peer) const
    {
        for (const PeerState& state : _peers) {
            if (state.used && state.address == peer) return &state;
        }
        return nullptr;
    }

    PeerState& findOrReplace(const BlePeerAddress& peer)
    {
        for (PeerState& state : _peers) {
            if (state.used && state.address == peer) return state;
        }
        for (PeerState& state : _peers) {
            if (!state.used) {
                state.used = true;
                state.address = peer;
                return state;
            }
        }
        // A bounded table prevents remote peers from consuming memory. Replace
        // the least-failed entry; the global counter still preserves cooldown.
        size_t victim = 0;
        for (size_t i = 1; i < PEER_SLOTS; ++i) {
            if (_peers[i].failures < _peers[victim].failures) victim = i;
        }
        _peers[victim] = PeerState{};
        _peers[victim].used = true;
        _peers[victim].address = peer;
        return _peers[victim];
    }

    static constexpr size_t PEER_SLOTS = 4;
    PeerState _peers[PEER_SLOTS]{};
    uint32_t _blocked_until_ms = 0;
    uint32_t _pairing_deadline_ms = 0;
    uint8_t _global_failures = 0;
    bool _pairing_window_open = false;
};

} // namespace comms
} // namespace sigurdos
