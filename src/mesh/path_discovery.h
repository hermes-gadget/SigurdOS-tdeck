// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos::mesh {

static constexpr size_t PATH_DISCOVERY_REQUEST_LEN = 9;
static constexpr uint8_t PATH_DISCOVERY_TELEMETRY_REQUEST = 0x03;
static constexpr uint8_t PATH_DISCOVERY_BASE_PERMISSION = 0x01;

inline void buildPathDiscoveryRequest(
    uint8_t (&request)[PATH_DISCOVERY_REQUEST_LEN],
    const uint8_t (&random_bytes)[4])
{
    request[0] = PATH_DISCOVERY_TELEMETRY_REQUEST;
    request[1] = static_cast<uint8_t>(~PATH_DISCOVERY_BASE_PERMISSION);
    std::memset(&request[2], 0, 3);
    std::memcpy(&request[5], random_bytes, sizeof(random_bytes));
}

}  // namespace sigurdos::mesh
