// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include "contact_store.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos::mesh {

struct ContactUriFields {
    char name[SIGURDOS_CONTACT_NAME_LEN]{};
    char pubkey_hex[SIGURDOS_CONTACT_PUBKEY_LEN * 2 + 1]{};
    uint8_t type = 1;
};

namespace detail {

inline bool uriHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

inline uint8_t uriHexValue(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return (uint8_t)(c - 'A' + 10);
}

inline bool decodeUriComponent(const char* src, size_t src_len,
                               char* out, size_t out_size)
{
    if (!src || !out || out_size == 0) return false;
    size_t read = 0;
    size_t written = 0;
    while (read < src_len) {
        unsigned char decoded = (unsigned char)src[read++];
        if (decoded == '+') {
            decoded = ' ';
        } else if (decoded == '%') {
            if (read + 1 >= src_len || !uriHex(src[read]) ||
                !uriHex(src[read + 1])) return false;
            decoded = (uint8_t)((uriHexValue(src[read]) << 4) |
                                uriHexValue(src[read + 1]));
            read += 2;
        }
        if (decoded == '\0' || written + 1 >= out_size) return false;
        out[written++] = (char)decoded;
    }
    out[written] = '\0';
    return true;
}

} // namespace detail

inline bool parseContactAddUri(const char* uri, ContactUriFields& out)
{
    static constexpr char PREFIX[] = "meshcore://contact/add?";
    if (!uri || std::strncmp(uri, PREFIX, sizeof(PREFIX) - 1) != 0) return false;

    ContactUriFields parsed{};
    bool have_name = false;
    bool have_key = false;
    bool have_type = false;
    const char* cursor = uri + sizeof(PREFIX) - 1;
    while (*cursor) {
        const char* key = cursor;
        while (*cursor && *cursor != '=' && *cursor != '&') cursor++;
        if (*cursor != '=') return false;
        const size_t key_len = (size_t)(cursor - key);
        const char* value = ++cursor;
        while (*cursor && *cursor != '&') cursor++;
        const size_t value_len = (size_t)(cursor - value);

        if (key_len == 4 && std::memcmp(key, "name", 4) == 0) {
            if (have_name || !detail::decodeUriComponent(
                    value, value_len, parsed.name, sizeof(parsed.name))) return false;
            have_name = true;
        } else if (key_len == 10 && std::memcmp(key, "public_key", 10) == 0) {
            if (have_key || value_len != SIGURDOS_CONTACT_PUBKEY_LEN * 2) return false;
            for (size_t i = 0; i < value_len; ++i) {
                if (!detail::uriHex(value[i])) return false;
            }
            std::memcpy(parsed.pubkey_hex, value, value_len);
            parsed.pubkey_hex[value_len] = '\0';
            have_key = true;
        } else if (key_len == 4 && std::memcmp(key, "type", 4) == 0) {
            if (have_type || value_len != 1 || value[0] < '1' || value[0] > '3') {
                return false;
            }
            parsed.type = (uint8_t)(value[0] - '0');
            have_type = true;
        }

        if (*cursor == '&') cursor++;
    }
    if (!have_name || !have_key || parsed.name[0] == '\0') return false;
    out = parsed;
    return true;
}

} // namespace sigurdos::mesh
