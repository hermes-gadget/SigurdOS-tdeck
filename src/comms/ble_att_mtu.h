#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace comms {

// ATT notifications carry a three-byte opcode/handle header inside the MTU.
static constexpr size_t BLE_ATT_VALUE_OVERHEAD = 3;

constexpr size_t bleAttMtuForPayload(size_t payload_len)
{
    return payload_len + BLE_ATT_VALUE_OVERHEAD;
}

constexpr size_t bleAttPayloadCapacity(uint16_t mtu)
{
    return mtu > BLE_ATT_VALUE_OVERHEAD
        ? (size_t)mtu - BLE_ATT_VALUE_OVERHEAD
        : 0;
}

constexpr bool bleAttPayloadFits(size_t payload_len, uint16_t mtu)
{
    return payload_len <= bleAttPayloadCapacity(mtu);
}

} // namespace comms
} // namespace sigurdos
