// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace mesh {
namespace path {

static constexpr size_t MAX_ENCODED_PATH_BYTES = 64;
static constexpr uint8_t UNKNOWN = 0xFF;

constexpr uint8_t hashCount(uint8_t encoded_len)
{
    return encoded_len & 0x3F;
}

constexpr uint8_t hashSize(uint8_t encoded_len)
{
    return (encoded_len >> 6) + 1;
}

constexpr size_t byteCount(uint8_t encoded_len)
{
    return (size_t)hashCount(encoded_len) * hashSize(encoded_len);
}

constexpr bool encodedLengthValid(uint8_t encoded_len)
{
    return hashSize(encoded_len) != 4 &&
        byteCount(encoded_len) <= MAX_ENCODED_PATH_BYTES;
}

constexpr bool storedLengthValid(uint8_t encoded_len)
{
    return encoded_len == UNKNOWN || encodedLengthValid(encoded_len);
}

} // namespace path
} // namespace mesh
} // namespace sigurdos
