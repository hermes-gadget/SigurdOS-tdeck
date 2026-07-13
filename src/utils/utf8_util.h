#pragma once

#include <cstddef>
#include <cstring>

namespace sigurdos {

static inline bool utf8_is_continuation(unsigned char byte)
{
    return (byte & 0xC0) == 0x80;
}

static inline size_t utf8_sequence_length(unsigned char lead)
{
    if ((lead & 0x80) == 0) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 0;
}

static inline bool utf8_sequence_is_valid(const unsigned char* bytes,
                                          size_t length)
{
    if (!bytes || length == 0) return false;
    if (length == 1) return bytes[0] < 0x80;
    for (size_t i = 1; i < length; ++i) {
        if (!utf8_is_continuation(bytes[i])) return false;
    }
    // Reject overlong encodings, UTF-16 surrogates, and values above U+10FFFF.
    if (length == 3 && bytes[0] == 0xE0 && bytes[1] < 0xA0) return false;
    if (length == 3 && bytes[0] == 0xED && bytes[1] > 0x9F) return false;
    if (length == 4 && bytes[0] == 0xF0 && bytes[1] < 0x90) return false;
    if (length == 4 && bytes[0] == 0xF4 && bytes[1] > 0x8F) return false;
    return true;
}

// Safely truncate a UTF-8 string to at most max_bytes without splitting a
// multi-byte character. Returns the number of bytes to keep.
//
// Only bytes inside [0, max_bytes) are inspected, so bounded buffers do not
// need to be null-terminated one byte past max_bytes.
static inline size_t utf8_truncate_bytes(const char* str, size_t max_bytes)
{
    if (!str || max_bytes == 0) return 0;

    size_t pos = 0;
    while (pos < max_bytes && str[pos] != '\0') {
        const unsigned char* bytes =
            reinterpret_cast<const unsigned char*>(str + pos);
        const size_t sequence_len = utf8_sequence_length(bytes[0]);
        if (sequence_len == 0 || sequence_len > max_bytes - pos) break;
        if (!utf8_sequence_is_valid(bytes, sequence_len)) break;
        pos += sequence_len;
    }
    return pos;
}

// Copy a null-terminated UTF-8 string into dest without splitting or retaining
// a malformed code point. Returns the number of bytes copied, excluding NUL.
static inline size_t utf8_copy_truncate(char* dest, size_t dest_size,
                                        const char* src)
{
    if (!dest || dest_size == 0) return 0;
    const size_t keep = utf8_truncate_bytes(src, dest_size - 1);
    if (keep > 0) std::memmove(dest, src, keep);
    dest[keep] = '\0';
    return keep;
}

// Bounded form for packet/file fields which may not contain a NUL terminator.
static inline size_t utf8_copy_truncate_n(char* dest, size_t dest_size,
                                          const char* src, size_t src_size)
{
    if (!dest || dest_size == 0) return 0;
    const size_t max_copy = src_size < dest_size - 1 ? src_size : dest_size - 1;
    const size_t keep = utf8_truncate_bytes(src, max_copy);
    if (keep > 0) std::memmove(dest, src, keep);
    dest[keep] = '\0';
    return keep;
}

// Append while preserving the same UTF-8 boundary contract as copy.
static inline size_t utf8_append_truncate(char* dest, size_t dest_size,
                                          const char* src)
{
    if (!dest || dest_size == 0) return 0;
    const size_t used = strnlen(dest, dest_size);
    if (used == dest_size) {
        dest[dest_size - 1] = '\0';
        return dest_size - 1;
    }
    return used + utf8_copy_truncate(dest + used, dest_size - used, src);
}

} // namespace sigurdos
