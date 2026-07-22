// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace sigurdos::mesh {

inline int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Decode standard padded Base64 without whitespace, ignored characters,
// trailing data, or non-canonical padding bits.
inline bool decodeBase64Strict(const char* input, size_t input_len,
                               uint8_t* output, size_t output_cap,
                               size_t& output_len) {
    output_len = 0;
    if (!input || !output || input_len == 0 || input_len % 4 != 0) return false;

    for (size_t offset = 0; offset < input_len; offset += 4) {
        const bool final = offset + 4 == input_len;
        const bool pad2 = input[offset + 2] == '=';
        const bool pad3 = input[offset + 3] == '=';
        if (!final && (pad2 || pad3)) return false;
        if (pad2 && !pad3) return false;

        const int a = base64Value(input[offset]);
        const int b = base64Value(input[offset + 1]);
        const int c = pad2 ? 0 : base64Value(input[offset + 2]);
        const int d = pad3 ? 0 : base64Value(input[offset + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;
        if (pad2 && (b & 0x0F) != 0) return false;
        if (!pad2 && pad3 && (c & 0x03) != 0) return false;

        const size_t produced = pad2 ? 1 : (pad3 ? 2 : 3);
        if (output_len + produced > output_cap) return false;
        const uint32_t word = (uint32_t(a) << 18) | (uint32_t(b) << 12) |
                              (uint32_t(c) << 6) | uint32_t(d);
        output[output_len++] = uint8_t(word >> 16);
        if (produced > 1) output[output_len++] = uint8_t(word >> 8);
        if (produced > 2) output[output_len++] = uint8_t(word);
    }
    return true;
}

}  // namespace sigurdos::mesh
