#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// MeshCore library shim for upstream TUs that expect the per-variant
// <target.h> (e.g. helpers/ESP32Board.cpp, helpers/CommonRadioPrefs.cpp).
//
// SigurdOS owns its board stack (TDeckBoard, its own radio driver and power
// paths) and never invokes upstream's ESP32Board helpers at runtime — but the
// library build compiles every core TU, so the handful of variant symbols
// those TUs reference must exist. Pin macros map to SigurdOS's definitions;
// the board globals are inert stand-ins.

#include "hal/tdeck_pins.h"   // P_LORA_NSS and the T-Deck pin macros

namespace meshcore_lib_shim {

struct InertRadio {
    void powerOff() {}
    // SigurdOS never routes remote-radio CLI commands into ESP32Board; report
    // unsupported for the boosted-gain toggle like a board without an FEM.
    bool setRxBoostedGainMode(bool) { return false; }
    void setTxPower(uint8_t) {}
};

struct InertLocationProvider {
    void stop() {}
};

struct InertSensors {
    InertLocationProvider* getLocationProvider() const {
        static InertLocationProvider provider;
        return &provider;
    }
};

} // namespace meshcore_lib_shim

[[maybe_unused]] static meshcore_lib_shim::InertRadio radio_driver;
[[maybe_unused]] static meshcore_lib_shim::InertSensors sensors;
