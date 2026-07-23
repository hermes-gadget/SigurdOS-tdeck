// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace sigurdos::ui {

enum class IdentityOperation : uint8_t { None, Export, Import };
enum class IdentityGuardResult : uint8_t { Allowed, ConfirmationRequired, ConfirmationRejected };

struct IdentityCommandGuard {
    IdentityOperation armed = IdentityOperation::None;
    uint32_t armed_at_ms = 0;
};

inline IdentityGuardResult authorizeIdentityCommand(
    IdentityCommandGuard& guard, uint32_t device_pin, IdentityOperation operation,
    bool explicit_confirmation, uint32_t now, uint32_t window_ms = 15000) {
    if (operation == IdentityOperation::None) {
        guard = {};
        return IdentityGuardResult::ConfirmationRejected;
    }
    if (device_pin != 0) {
        guard = {};
        return IdentityGuardResult::Allowed;
    }
    if (!explicit_confirmation) {
        guard.armed = operation;
        guard.armed_at_ms = now;
        return IdentityGuardResult::ConfirmationRequired;
    }
    const bool allowed = guard.armed == operation &&
                         now - guard.armed_at_ms < window_ms;
    guard = {};
    return allowed ? IdentityGuardResult::Allowed
                   : IdentityGuardResult::ConfirmationRejected;
}

}  // namespace sigurdos::ui
