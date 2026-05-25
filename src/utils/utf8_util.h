#pragma once

#include <cstddef>
#include <cstring>

namespace slopos {

// Safely truncate a UTF-8 string to at most max_bytes without splitting a
// multi-byte character. Returns the number of bytes to keep.
//
// After strnlen(str, max_bytes), if we hit the limit mid-sequence (the byte
// at position max_bytes is a UTF-8 continuation byte 0x80-0xBF), walk
// backward past continuation bytes to the start of the incomplete character
// and truncate before it.
static inline size_t utf8_truncate_bytes(const char* str, size_t max_bytes)
{
    size_t len = strnlen(str, max_bytes);
    if (len < max_bytes) return len;
    while (len > 0 && ((unsigned char)str[len] & 0xC0) == 0x80) {
        len--;
    }
    return len;
}

} // namespace slopos
