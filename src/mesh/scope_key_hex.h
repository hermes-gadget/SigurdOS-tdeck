#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Hex codec for the 16-byte private flood-scope key persisted in NodePrefs
// (default_scope_key_hex: 32 hex chars + NUL). Extracted verbatim from the
// companion adapter (ARCH-001, #820) so the codec is testable natively.
//
// Decode is strict: corrupt text and the prohibited all-zero private key are
// rejected without modifying the caller's output buffer.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace sigurdos {
namespace mesh {

static constexpr int SCOPE_KEY_LEN = 16;
static constexpr int SCOPE_KEY_HEX_LEN = SCOPE_KEY_LEN * 2;  // excl. NUL

// Encode key16[16] into out33 as 32 lowercase hex chars + NUL terminator.
inline void scopeKeyHexEncode(const uint8_t* key16, char* out33)
{
    if (!key16 || !out33) return;
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < SCOPE_KEY_LEN; i++) {
        out33[i * 2] = hex[key16[i] >> 4];
        out33[i * 2 + 1] = hex[key16[i] & 0x0F];
    }
    out33[SCOPE_KEY_HEX_LEN] = '\0';
}

inline int scopeKeyHexNibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

inline bool scopeKeyNonZero(const uint8_t* key16)
{
    if (!key16) return false;
    uint8_t combined = 0;
    for (int i = 0; i < SCOPE_KEY_LEN; ++i) combined |= key16[i];
    return combined != 0;
}

// Decode exactly 32 hex characters. Case-insensitive and all-or-nothing.
inline bool scopeKeyHexDecode(const char* hex32, uint8_t* out16)
{
    if (!hex32 || !out16 || strlen(hex32) != SCOPE_KEY_HEX_LEN) return false;
    uint8_t decoded[SCOPE_KEY_LEN]{};
    for (int i = 0; i < SCOPE_KEY_LEN; i++) {
        const int hi = scopeKeyHexNibble(hex32[i * 2]);
        const int lo = scopeKeyHexNibble(hex32[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        decoded[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    if (!scopeKeyNonZero(decoded)) return false;
    memcpy(out16, decoded, sizeof(decoded));
    return true;
}

inline int scopeKeyBase64Value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Decode the canonical 24-character Base64 representation of a 16-byte
// private flood-scope key. Strict validation prevents malformed input from
// silently decoding as zeroes.
inline bool scopeKeyBase64Decode(const char* encoded, uint8_t* out16)
{
    if (!encoded || !out16 || strlen(encoded) != 24 ||
        encoded[22] != '=' || encoded[23] != '=') return false;

    uint8_t decoded[SCOPE_KEY_LEN]{};
    size_t out = 0;
    for (size_t i = 0; i < 20; i += 4) {
        int a = scopeKeyBase64Value(encoded[i]);
        int b = scopeKeyBase64Value(encoded[i + 1]);
        int c = scopeKeyBase64Value(encoded[i + 2]);
        int d = scopeKeyBase64Value(encoded[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;
        decoded[out++] = (uint8_t)((a << 2) | (b >> 4));
        decoded[out++] = (uint8_t)((b << 4) | (c >> 2));
        decoded[out++] = (uint8_t)((c << 6) | d);
    }

    int a = scopeKeyBase64Value(encoded[20]);
    int b = scopeKeyBase64Value(encoded[21]);
    if (a < 0 || b < 0 || (b & 0x0F) != 0 || out != 15) return false;
    decoded[out] = (uint8_t)((a << 2) | (b >> 4));
    if (!scopeKeyNonZero(decoded)) return false;
    memcpy(out16, decoded, sizeof(decoded));
    return true;
}

} // namespace mesh
} // namespace sigurdos
