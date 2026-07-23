// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace hal {

// OTA transport and flash writes run on a managed task on core 0. The Arduino
// loopTask remains free to service LVGL, mesh, input, and the runtime watchdog.
constexpr uint32_t OTA_WORKER_STACK_BYTES = 12U * 1024U;
constexpr int OTA_WORKER_CORE = 0;

// Bound each GitHub download pass even when the TLS socket has a large backlog.
// Update.write() therefore releases the worker regularly and never drains a
// complete image in one scheduling slice.
constexpr size_t OTA_TRANSFER_SLICE_BYTES = 16U * 1024U;
constexpr uint32_t OTA_TRANSFER_SLICE_MS = 8U;
constexpr size_t OTA_MAX_IMAGE_BYTES = 6U * 1024U * 1024U;

inline bool otaTransferSliceExhausted(size_t bytes_processed,
                                      uint32_t slice_started_at,
                                      uint32_t now) {
    return bytes_processed >= OTA_TRANSFER_SLICE_BYTES ||
           static_cast<uint32_t>(now - slice_started_at) >=
               OTA_TRANSFER_SLICE_MS;
}

inline size_t otaTransferReadLimit(size_t available, size_t image_remaining,
                                   size_t slice_remaining,
                                   size_t buffer_capacity) {
    size_t result = available;
    if (result > image_remaining) result = image_remaining;
    if (result > slice_remaining) result = slice_remaining;
    if (result > buffer_capacity) result = buffer_capacity;
    return result;
}

}  // namespace hal
}  // namespace sigurdos
