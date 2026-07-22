// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "wifi_coordinator.h"

#ifdef ESP32_PLATFORM
#include <WiFi.h>
#endif

namespace sigurdos::wifi {
namespace {

Coordinator coordinator;

#ifdef ESP32_PLATFORM
RadioMode readRadioMode() {
    switch (WiFi.getMode()) {
    case WIFI_STA: return RadioMode::Sta;
    case WIFI_AP: return RadioMode::Ap;
    case WIFI_AP_STA: return RadioMode::ApSta;
    case WIFI_OFF:
    default: return RadioMode::Off;
    }
}

bool applyRadioMode(RadioMode mode) {
    wifi_mode_t target = WIFI_OFF;
    switch (mode) {
    case RadioMode::Sta: target = WIFI_STA; break;
    case RadioMode::Ap: target = WIFI_AP; break;
    case RadioMode::ApSta: target = WIFI_AP_STA; break;
    case RadioMode::Off: target = WIFI_OFF; break;
    }
    return WiFi.getMode() == target || WiFi.mode(target);
}
#else
RadioMode readRadioMode() {
    return coordinator.currentMode();
}

bool applyRadioMode(RadioMode) {
    return true;
}
#endif

}  // namespace

bool acquire(Owner owner, RadioMode requested_mode) {
    if (coordinator.currentOwner() == owner) {
        return coordinator.currentMode() == requested_mode;
    }

    if (!coordinator.acquire(owner, requested_mode, readRadioMode())) return false;
    if (applyRadioMode(requested_mode)) return true;

    const ReleasePlan rollback = coordinator.release(owner);
    if (rollback.released) applyRadioMode(rollback.restored_mode);
    return false;
}

bool release(Owner owner) {
    const ReleasePlan plan = coordinator.release(owner);
    if (!plan.released) return false;
    return applyRadioMode(plan.restored_mode);
}

bool canAcquire(Owner owner) {
    return coordinator.canAcquire(owner);
}

Owner currentOwner() {
    return coordinator.currentOwner();
}

const char* ownerName(Owner owner) {
    switch (owner) {
    case Owner::Sta: return "WiFi connection";
    case Owner::Scan: return "WiFi scan";
    case Owner::ApOta: return "local OTA";
    case Owner::GitHubOta: return "GitHub OTA";
    case Owner::None:
    default: return "idle";
    }
}

}  // namespace sigurdos::wifi
