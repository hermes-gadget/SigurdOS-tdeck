// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <climits>

namespace sigurdos::mesh {

inline bool parse_bounded_int(const uint8_t* data, size_t len, int* out)
{
    if (!data || !out || len == 0) return false;

    size_t pos = 0;
    bool negative = false;
    if (data[pos] == '+' || data[pos] == '-') {
        negative = data[pos] == '-';
        if (++pos == len) return false;
    }

    const uint32_t limit = negative
        ? static_cast<uint32_t>(INT_MAX) + 1U
        : static_cast<uint32_t>(INT_MAX);
    uint32_t value = 0;
    for (; pos < len; ++pos) {
        if (data[pos] < '0' || data[pos] > '9') return false;
        const uint32_t digit = data[pos] - '0';
        if (value > (limit - digit) / 10U) return false;
        value = value * 10U + digit;
    }

    *out = negative
        ? (value == static_cast<uint32_t>(INT_MAX) + 1U
               ? INT_MIN
               : -static_cast<int>(value))
        : static_cast<int>(value);
    return true;
}

}  // namespace sigurdos::mesh
