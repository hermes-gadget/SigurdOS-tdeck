#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Hex codec for the 16-byte private flood-scope key persisted in NodePrefs
// (default_scope_key_hex: 32 hex chars + NUL). Extracted verbatim from the
// companion adapter (ARCH-001, #820) so the codec is testable natively.
//
// Legacy semantics are preserved exactly: decode maps any non-hex character
// to a zero nibble (it never fails), and encode emits lowercase hex.

#include <stdint.h>

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

// Decode 32 hex chars from hex32 into out16[16]. Case-insensitive; a non-hex
// character contributes a zero nibble. hex32 must hold at least 32 bytes.
inline void scopeKeyHexDecode(const char* hex32, uint8_t* out16)
{
    if (!hex32 || !out16) return;
    for (int i = 0; i < SCOPE_KEY_LEN; i++) {
        char hi = hex32[i * 2];
        char lo = hex32[i * 2 + 1];
        uint8_t b = 0;
        if (hi >= '0' && hi <= '9') b = (hi - '0') << 4;
        else if (hi >= 'a' && hi <= 'f') b = (hi - 'a' + 10) << 4;
        else if (hi >= 'A' && hi <= 'F') b = (hi - 'A' + 10) << 4;
        if (lo >= '0' && lo <= '9') b |= (lo - '0');
        else if (lo >= 'a' && lo <= 'f') b |= (lo - 'a' + 10);
        else if (lo >= 'A' && lo <= 'F') b |= (lo - 'A' + 10);
        out16[i] = b;
    }
}

} // namespace mesh
} // namespace sigurdos
