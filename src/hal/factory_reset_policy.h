// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos::hal::factory_reset {

static constexpr char PREFS_NAMESPACE[] = "sigurdos";
static constexpr char PASSWORDS_NAMESPACE[] = "sigurdos_pw";
static constexpr char CHAT_SCOPES_NAMESPACE[] = "chat_scopes";

// This namespace is deliberately outside the owned namespaces below. It
// survives their deletion so boot can resume an interrupted reset instead of
// entering normal runtime with a mixed identity/history state.
static constexpr char RESET_MARKER_NAMESPACE[] = "sigurdos_reset";
static constexpr char RESET_MARKER_KEY[] = "stage";

enum class ResetMarkerStage : uint8_t {
    Armed = 1,
    Nvs = 2,
    Spiffs = 3,
};

enum class ResetMarkerStatus : uint8_t {
    Absent,
    Pending,
    Invalid,
};

// Implemented with the low-level NVS API in prefs.cpp so the marker is
// committed independently from the preference namespace it protects.
bool reset_marker_write(ResetMarkerStage stage);
ResetMarkerStatus reset_marker_read(ResetMarkerStage* stage = nullptr);
bool reset_marker_clear();

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

enum class FailureStage : uint8_t {
    None,
    Nvs,
    Spiffs,
};

using FormatStorageFn = bool (*)(void*);

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

// Keep the destructive sequence injectable so callers can refuse to reboot
// when either the namespace reset or filesystem format did not complete.
inline bool eraseOwnedStorage(ApplyNvsTargetFn apply_nvs, void* nvs_context,
                              FormatStorageFn format_storage, void* format_context,
                              const NvsTarget** failed_target = nullptr,
                              FailureStage* failed_stage = nullptr)
{
    if (failed_target) *failed_target = nullptr;
    if (failed_stage) *failed_stage = FailureStage::None;

    if (!applyNvsTargets(apply_nvs, nvs_context, failed_target)) {
        if (failed_stage) *failed_stage = FailureStage::Nvs;
        return false;
    }
    if (!format_storage || !format_storage(format_context)) {
        if (failed_stage) *failed_stage = FailureStage::Spiffs;
        return false;
    }
    return true;
}

static constexpr size_t nvsTargetCount()
{
    return sizeof(NVS_TARGETS) / sizeof(NVS_TARGETS[0]);
}

} // namespace sigurdos::hal::factory_reset
