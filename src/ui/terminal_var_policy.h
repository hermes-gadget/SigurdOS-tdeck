#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstring>

namespace sigurdos::ui {

inline bool terminal_var_rewrite(const char* input, size_t input_len,
                                 const char* key, const char* value,
                                 char* output, size_t output_capacity,
                                 size_t* output_len, bool* found)
{
    if (!key || !key[0] || !output || output_capacity == 0 || !output_len || !found) return false;
    *output_len = 0;
    *found = false;
    const size_t key_len = std::strlen(key);
    size_t pos = 0;
    while (pos < input_len) {
        const size_t start = pos;
        while (pos < input_len && input[pos] != '\n') ++pos;
        size_t end = pos;
        if (end > start && input[end - 1] == '\r') --end;
        if (pos < input_len) ++pos;
        const bool matches = end > start + key_len && input[start + key_len] == '=' &&
                             std::memcmp(input + start, key, key_len) == 0;
        if (matches) { *found = true; continue; }
        if (end == start) continue;
        const size_t line_len = end - start;
        if (*output_len + line_len + 1 >= output_capacity) return false;
        std::memcpy(output + *output_len, input + start, line_len);
        *output_len += line_len;
        output[(*output_len)++] = '\n';
    }
    if (value) {
        const size_t value_len = std::strlen(value);
        if (*output_len + key_len + value_len + 2 >= output_capacity) return false;
        std::memcpy(output + *output_len, key, key_len);
        *output_len += key_len;
        output[(*output_len)++] = '=';
        std::memcpy(output + *output_len, value, value_len);
        *output_len += value_len;
        output[(*output_len)++] = '\n';
    }
    output[*output_len] = '\0';
    return true;
}

} // namespace sigurdos::ui
