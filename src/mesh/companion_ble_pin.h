#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "comms/companion_bridge.h"
#include "hal/prefs.h"

namespace sigurdos {
namespace mesh {

inline bool applyCompanionBlePin(NodePrefs& prefs, uint32_t pin)
{
    if (!sigurdos::comms::companionBlePinValid(pin)) return false;
    prefs.ble_pin = pin;
    prefs.ble_bond_reset_pending = true;
    return true;
}

} // namespace mesh
} // namespace sigurdos
