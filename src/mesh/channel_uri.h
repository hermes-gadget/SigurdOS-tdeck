// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include "channel_validation.h"
#include "contact_uri.h"

#include <cstddef>
#include <cstring>

namespace sigurdos::mesh {

struct ChannelUriFields {
    char name[32]{};
    char secret_hex[33]{};
};

inline bool parseChannelAddUri(const char* uri, ChannelUriFields& out)
{
    static constexpr char PREFIX[] = "meshcore://channel/add?";
    static constexpr size_t PREFIX_LEN = sizeof(PREFIX) - 1;
    if (!uri || std::strncmp(uri, PREFIX, PREFIX_LEN) != 0) return false;
    ChannelUriFields parsed{};
    bool have_name = false;
    bool have_secret = false;
    // Index rather than pointer+sizeof — CodeQL cpp/suspicious-add-with-sizeof.
    const char* cursor = &uri[PREFIX_LEN];
    if (!*cursor) return false;
    while (*cursor) {
        const char* key = cursor;
        while (*cursor && *cursor != '=' && *cursor != '&') ++cursor;
        if (*cursor != '=') return false;
        const size_t key_len = static_cast<size_t>(cursor - key);
        const char* value = ++cursor;
        while (*cursor && *cursor != '&') ++cursor;
        const size_t value_len = static_cast<size_t>(cursor - value);
        if (value_len == 0) return false;

        if (key_len == 4 && std::memcmp(key, "name", 4) == 0) {
            if (have_name || !detail::decodeUriComponent(
                    value, value_len, parsed.name, sizeof(parsed.name)) ||
                !channel_name_valid(parsed.name)) return false;
            have_name = true;
        } else if (key_len == 6 && std::memcmp(key, "secret", 6) == 0) {
            if (have_secret || value_len != 32) return false;
            for (size_t i = 0; i < value_len; ++i) {
                if (!detail::uriHex(value[i])) return false;
            }
            std::memcpy(parsed.secret_hex, value, value_len);
            parsed.secret_hex[value_len] = '\0';
            have_secret = true;
        } else {
            return false;
        }

        if (*cursor == '&') {
            if (!cursor[1]) return false;
            ++cursor;
        }
    }
    if (!have_name || !have_secret) return false;
    out = parsed;
    return true;
}

inline bool parseRawContactUriHex(const char* uri, char* hex, size_t hex_cap,
                                  size_t& hex_len)
{
    static constexpr char PREFIX[] = "meshcore://";
    static constexpr size_t PREFIX_LEN = sizeof(PREFIX) - 1;
    hex_len = 0;
    if (!uri || !hex || hex_cap == 0 ||
        std::strncmp(uri, PREFIX, PREFIX_LEN) != 0) return false;
    // Index rather than pointer+sizeof — CodeQL cpp/suspicious-add-with-sizeof.
    const char* value = &uri[PREFIX_LEN];
    while (value[hex_len]) {
        if (!detail::uriHex(value[hex_len]) || hex_len + 1 >= hex_cap) return false;
        hex[hex_len] = value[hex_len];
        ++hex_len;
    }
    if (hex_len < SIGURDOS_CONTACT_PUBKEY_LEN * 2 || (hex_len & 1U) != 0) {
        return false;
    }
    hex[hex_len] = '\0';
    return true;
}

} // namespace sigurdos::mesh
