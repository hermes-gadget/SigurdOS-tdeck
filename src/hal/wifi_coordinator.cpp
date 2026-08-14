// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "wifi_coordinator.h"

#ifdef ESP32_PLATFORM
#include <WiFi.h>
#endif

namespace sigurdos::wifi {
namespace {

Coordinator coordinator;
ReleaseRequestQueue release_requests;

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

    if (!coordinator.releaseWith(owner, applyRadioMode) &&
        coordinator.hasOwner(owner)) {
        release_requests.request(owner);
    }
    return false;
}

bool reserveForWorker(Owner owner, RadioMode requested_mode) {
    if (coordinator.currentOwner() == owner) {
        return coordinator.currentMode() == requested_mode;
    }
    return coordinator.acquire(owner, requested_mode, readRadioMode());
}

bool release(Owner owner) {
    return coordinator.releaseWith(owner, applyRadioMode);
}

void requestRelease(Owner owner) {
    release_requests.request(owner);
}

void servicePendingReleases() {
    const uint8_t pending = release_requests.take();
    constexpr Owner owners[] = {
        Owner::Scan, Owner::ApOta, Owner::GitHubOta, Owner::Sta,
    };
    for (const Owner owner : owners) {
        if ((pending & releaseRequestMask(owner)) != 0) {
            if (!release(owner) && coordinator.hasOwner(owner)) {
                // Keep the logical lease and retry the hardware restoration
                // on the next loop iteration. A worker request must never be
                // silently discarded after a transient driver failure.
                release_requests.request(owner);
            }
        }
    }
}

bool canAcquire(Owner owner) {
    return coordinator.canAcquire(owner);
}

Owner currentOwner() {
    return coordinator.currentOwner();
}

bool hasOwner(Owner owner) {
    return coordinator.hasOwner(owner);
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
