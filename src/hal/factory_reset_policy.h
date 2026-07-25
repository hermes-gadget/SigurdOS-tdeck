// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos::hal::factory_reset {

static constexpr char PREFS_NAMESPACE[] = "sigurdos";
static constexpr char PASSWORDS_NAMESPACE[] = "sigurdos_pw";
static constexpr char CHAT_SCOPES_NAMESPACE[] = "chat_scopes";

enum class NvsAction : uint8_t {
    Clear,
    ReplaceWithSafePrefs,
};

struct NvsTarget {
    const char* name;
    NvsAction action;
};

// The primary prefs namespace is deliberately last. The earlier namespaces
// may be cleared independently, while the final action atomically replaces
// prefs with a BLE-off, bond-purge-pending reset record.
static constexpr NvsTarget NVS_TARGETS[] = {
    {PASSWORDS_NAMESPACE, NvsAction::Clear},
    {CHAT_SCOPES_NAMESPACE, NvsAction::Clear},
    {PREFS_NAMESPACE, NvsAction::ReplaceWithSafePrefs},
};

using ApplyNvsTargetFn = bool (*)(const NvsTarget&, void*);

inline bool applyNvsTargets(ApplyNvsTargetFn apply, void* context,
                            const NvsTarget** failed_target = nullptr)
{
    if (failed_target) *failed_target = nullptr;
    if (!apply) return false;
    for (const NvsTarget& target : NVS_TARGETS) {
        if (apply(target, context)) continue;
        if (failed_target) *failed_target = &target;
        return false;
    }
    return true;
}

static constexpr size_t nvsTargetCount()
{
    return sizeof(NVS_TARGETS) / sizeof(NVS_TARGETS[0]);
}

} // namespace sigurdos::hal::factory_reset
