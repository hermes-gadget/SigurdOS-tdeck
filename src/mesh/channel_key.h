// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <MeshCore.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos::mesh {

// Channel PSKs are MeshCore cipher keys, not public keys. Keep the wire and
// buffer sizes tied to the protocol constant so QR export cannot accidentally
// grow to the 32-byte identity-key width.
static constexpr size_t CHANNEL_SECRET_BYTES =
    static_cast<size_t>(CIPHER_KEY_SIZE);
static constexpr size_t CHANNEL_SECRET_HEX_LEN = CHANNEL_SECRET_BYTES * 2;
static constexpr size_t CHANNEL_SECRET_HEX_CAPACITY =
    CHANNEL_SECRET_HEX_LEN + 1;

inline bool channelSecretHexDigit(char value, uint8_t& out)
{
    if (value >= '0' && value <= '9') {
        out = static_cast<uint8_t>(value - '0');
        return true;
    }
    if (value >= 'a' && value <= 'f') {
        out = static_cast<uint8_t>(value - 'a' + 10);
        return true;
    }
    if (value >= 'A' && value <= 'F') {
        out = static_cast<uint8_t>(value - 'A' + 10);
        return true;
    }
    return false;
}

inline bool channelSecretHexEncode(const uint8_t* secret, char* out,
                                   size_t out_capacity)
{
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    if (!secret || !out || out_capacity < CHANNEL_SECRET_HEX_CAPACITY) {
        return false;
    }
    for (size_t i = 0; i < CHANNEL_SECRET_BYTES; ++i) {
        out[i * 2] = HEX_DIGITS[secret[i] >> 4];
        out[i * 2 + 1] = HEX_DIGITS[secret[i] & 0x0F];
    }
    out[CHANNEL_SECRET_HEX_LEN] = '\0';
    return true;
}

inline bool channelSecretHexDecode(const char* hex, uint8_t* out,
                                   size_t out_capacity)
{
    if (!hex || !out || out_capacity < CHANNEL_SECRET_BYTES ||
        std::strlen(hex) != CHANNEL_SECRET_HEX_LEN) {
        return false;
    }
    for (size_t i = 0; i < CHANNEL_SECRET_BYTES; ++i) {
        uint8_t high = 0;
        uint8_t low = 0;
        if (!channelSecretHexDigit(hex[i * 2], high) ||
            !channelSecretHexDigit(hex[i * 2 + 1], low)) {
            return false;
        }
        out[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

} // namespace sigurdos::mesh
