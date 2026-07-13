#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace comms {

struct CompanionFrameDelivery {
    uint32_t token = 0;
    bool succeeded = false;
};

// Optional transport capability for frames whose delivery must be confirmed
// after they have merely been accepted into a transport queue.
class CompanionFrameDeliveryTracker {
public:
    virtual ~CompanionFrameDeliveryTracker() = default;
    virtual size_t writeFrameTracked(const uint8_t src[], size_t len,
                                     uint32_t token) = 0;
    virtual bool pollFrameDelivery(CompanionFrameDelivery& delivery) = 0;
};

} // namespace comms
} // namespace sigurdos
