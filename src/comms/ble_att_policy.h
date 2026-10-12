#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// ATT payload sizing policy for the BLE companion transport.
//
// Ported from the former MeshCore-core fix "retain MTU-safe BLE notifications"
// (SigurdOS issues #932 / #934): notifications reserve three bytes for the ATT
// protocol header, so a complete MAX_FRAME_SIZE protocol frame only fits when
// the negotiated MTU is at least MAX_FRAME_SIZE + 3. The device requests the
// documented maximum (517) and refuses to enqueue frames that cannot fit the
// peer's actual payload capacity.

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace comms {
namespace ble {

static constexpr uint16_t ATT_HEADER_SIZE = 3;
static constexpr uint16_t DEFAULT_ATT_MTU = 23;
static constexpr uint16_t PREFERRED_ATT_MTU = 517;

inline size_t payloadCapacity(uint16_t mtu) {
    return mtu > ATT_HEADER_SIZE ? static_cast<size_t>(mtu - ATT_HEADER_SIZE) : 0;
}

inline bool frameFitsMtu(size_t frame_len, uint16_t mtu) {
    return frame_len > 0 && frame_len <= payloadCapacity(mtu);
}

} // namespace ble
} // namespace comms
} // namespace sigurdos
