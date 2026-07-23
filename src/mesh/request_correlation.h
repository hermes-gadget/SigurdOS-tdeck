// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos {
namespace mesh {

inline bool requestResponseMatches(uint32_t tag, const uint8_t* public_key,
                                   uint32_t expected_tag,
                                   const uint8_t* expected_public_key,
                                   size_t key_len) {
    return tag != 0 && tag == expected_tag && public_key &&
        expected_public_key && key_len > 0 &&
        std::memcmp(public_key, expected_public_key, key_len) == 0;
}

inline bool typedResponseMatches(uint32_t tag, const uint8_t* public_key,
                                 uint8_t request_type,
                                 uint32_t expected_tag,
                                 const uint8_t* expected_public_key,
                                 uint8_t expected_type, size_t key_len) {
    return request_type == expected_type && requestResponseMatches(
        tag, public_key, expected_tag, expected_public_key, key_len);
}

} // namespace mesh
} // namespace sigurdos
