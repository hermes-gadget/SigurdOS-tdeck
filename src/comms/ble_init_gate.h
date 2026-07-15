#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

namespace sigurdos {
namespace comms {

// Keeps BLE configuration separate from controller initialization. The caller
// supplies the target-specific initializer only when BLE is actually enabled.
class BleInitGate {
public:
    void configure() { _configured = true; }

    template <typename Initializer>
    bool ensureInitialized(Initializer initializer)
    {
        if (_initialized) return true;
        if (!_configured || !initializer()) return false;
        _initialized = true;
        return true;
    }

    bool configured() const { return _configured; }
    bool initialized() const { return _initialized; }

private:
    bool _configured = false;
    bool _initialized = false;
};

} // namespace comms
} // namespace sigurdos
