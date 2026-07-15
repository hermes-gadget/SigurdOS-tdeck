// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstdint>

#ifndef SIGURDOS_OTA_HEALTH_WINDOW_MS
#define SIGURDOS_OTA_HEALTH_WINDOW_MS 30000UL
#endif

namespace sigurdos {
namespace ota_boot_health {

static constexpr uint32_t HEALTH_WINDOW_MS = SIGURDOS_OTA_HEALTH_WINDOW_MS;

struct PolicyState {
    bool pending_verification;
    bool core_ready;
    bool loop_completed;
    uint32_t started_at;
};

inline bool healthWindowElapsed(uint32_t started_at, uint32_t now) {
    return static_cast<uint32_t>(now - started_at) >= HEALTH_WINDOW_MS;
}

inline bool shouldMarkValid(const PolicyState& state, uint32_t now) {
    return state.pending_verification && state.core_ready &&
           state.loop_completed && healthWindowElapsed(state.started_at, now);
}

// Inspect the running OTA partition. A newly installed image remains pending
// until markCoreReady(), a complete loop(), and the health window all succeed.
void begin();
void markCoreReady();
void loop();

// Roll back immediately when a critical boot prerequisite fails. This is a
// no-op when the running image is not pending; success reboots and does not
// return.
void rollbackIfPending(const char* reason);
bool isPendingVerification();

}  // namespace ota_boot_health
}  // namespace sigurdos
