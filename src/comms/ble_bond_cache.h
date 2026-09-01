#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>

#include "ble_auth_throttle.h"

namespace sigurdos {
namespace comms {

#if defined(CONFIG_BT_SMP_MAX_BONDS)
static constexpr size_t BLE_BOND_CACHE_CAPACITY = CONFIG_BT_SMP_MAX_BONDS;
#else
static constexpr size_t BLE_BOND_CACHE_CAPACITY = 15;
#endif
static_assert(BLE_BOND_CACHE_CAPACITY > 0,
              "BLE bond cache must have at least one slot");

// Fixed-capacity snapshot of Bluedroid's bond database. The snapshot is
// populated from the app task before advertising starts; BLE callbacks only
// perform bounded RAM lookups and never call the stack-heavy bond APIs on
// BTC_TASK.
class BleBondCache {
public:
    void clear() { _count = 0; }

    bool replace(const BlePeerAddress* peers, size_t count)
    {
        if (count > BLE_BOND_CACHE_CAPACITY || (count > 0 && !peers)) {
            return false;
        }
        for (size_t i = 0; i < count; ++i) _peers[i] = peers[i];
        _count = count;
        return true;
    }

    bool add(const BlePeerAddress& peer)
    {
        if (contains(peer)) return true;
        if (_count >= BLE_BOND_CACHE_CAPACITY) return false;
        _peers[_count++] = peer;
        return true;
    }

    bool contains(const BlePeerAddress& peer) const
    {
        for (size_t i = 0; i < _count; ++i) {
            if (_peers[i] == peer) return true;
        }
        return false;
    }

    size_t size() const { return _count; }

private:
    BlePeerAddress _peers[BLE_BOND_CACHE_CAPACITY]{};
    size_t _count = 0;
};

} // namespace comms
} // namespace sigurdos
