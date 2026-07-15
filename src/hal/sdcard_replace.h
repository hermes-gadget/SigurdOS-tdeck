#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdint>

namespace sigurdos::sdcard::detail {

enum class RenameResult : uint8_t {
    Success,
    DestinationExists,
    Failure,
};

enum class ReplaceStage : uint8_t {
    TempOpened,
    TempWritten,
    TempSynced,
    TempClosed,
    TempValidated,
    LiveRemoved,
    TempRenamed,
};

struct ReplaceOps {
    void* context;
    bool (*exists)(void*, const char*);
    bool (*remove)(void*, const char*);
    void* (*open)(void*, const char*);
    size_t (*write)(void*, void*, const uint8_t*, size_t);
    bool (*sync)(void*, void*);
    bool (*size)(void*, void*, size_t*);
    bool (*close)(void*, void*);
    RenameResult (*rename)(void*, const char*, const char*);
    // Production leaves this null. Native tests use it to model a reset or
    // card removal at precise persistence boundaries.
    bool (*checkpoint)(void*, ReplaceStage);
};

inline bool checkpoint(const ReplaceOps& ops, ReplaceStage stage)
{
    return !ops.checkpoint || ops.checkpoint(ops.context, stage);
}

inline bool promoteValidated(const char* path, const char* ready_path,
                             const ReplaceOps& ops)
{
    RenameResult renamed = ops.rename(ops.context, ready_path, path);
    if (renamed == RenameResult::Success) {
        return checkpoint(ops, ReplaceStage::TempRenamed);
    }
    if (renamed != RenameResult::DestinationExists ||
        !ops.remove(ops.context, path)) {
        return false;
    }
    if (!checkpoint(ops, ReplaceStage::LiveRemoved)) return false;

    renamed = ops.rename(ops.context, ready_path, path);
    if (renamed != RenameResult::Success) return false;
    return checkpoint(ops, ReplaceStage::TempRenamed);
}

// Raw .tmp files may be partial and are never promoted. Only a temp that was
// synced, closed, size-checked, and renamed to .ready can enter promotion.
inline bool recoverPending(const char* path, const char* temp_path,
                           const char* ready_path, const ReplaceOps& ops)
{
    if (!path || !temp_path || !ready_path || !ops.exists ||
        !ops.remove || !ops.rename) {
        return false;
    }
    if (ops.exists(ops.context, ready_path) &&
        !promoteValidated(path, ready_path, ops)) {
        return false;
    }
    return !ops.exists(ops.context, temp_path) ||
        ops.remove(ops.context, temp_path);
}

inline bool replaceFile(const char* path, const char* temp_path,
                        const char* ready_path,
                        const uint8_t* data, size_t length,
                        const ReplaceOps& ops)
{
    if (!path || !temp_path || !ready_path || (length > 0 && !data) ||
        !ops.exists || !ops.remove || !ops.open || !ops.write ||
        !ops.sync || !ops.size || !ops.close || !ops.rename) {
        return false;
    }
    if (!recoverPending(path, temp_path, ready_path, ops)) return false;

    void* file = ops.open(ops.context, temp_path);
    if (!file) return false;
    if (!checkpoint(ops, ReplaceStage::TempOpened)) return false;

    const bool wrote = length == 0 ||
        ops.write(ops.context, file, data, length) == length;
    if (!checkpoint(ops, ReplaceStage::TempWritten)) return false;
    if (!wrote) {
        (void)ops.close(ops.context, file);
        (void)ops.remove(ops.context, temp_path);
        return false;
    }

    const bool synced = ops.sync(ops.context, file);
    if (!checkpoint(ops, ReplaceStage::TempSynced)) return false;

    size_t actual_size = 0;
    const bool sized = synced && ops.size(ops.context, file, &actual_size) &&
        actual_size == length;
    const bool closed = ops.close(ops.context, file);
    if (!checkpoint(ops, ReplaceStage::TempClosed)) return false;
    if (!sized || !closed) {
        (void)ops.remove(ops.context, temp_path);
        return false;
    }

    if (ops.rename(ops.context, temp_path, ready_path) != RenameResult::Success) {
        return false;
    }
    if (!checkpoint(ops, ReplaceStage::TempValidated)) return false;

    // POSIX/FAT VFS may support an atomic overwrite. Only fall back to the
    // remove-then-rename sequence when the backend explicitly reports a
    // destination conflict; other failures preserve the validated ready file.
    return promoteValidated(path, ready_path, ops);
}

} // namespace sigurdos::sdcard::detail
