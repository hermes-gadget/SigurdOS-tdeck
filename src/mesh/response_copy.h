// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos::mesh::detail {

inline bool copyResponseBuffers(
    uint32_t tag, const uint8_t* data, uint8_t data_len,
    const char* contact_name, size_t contact_name_max,
    uint32_t* out_tag,
    uint8_t* out_data, size_t out_data_cap, uint8_t* out_len,
    char* out_contact_name, size_t out_contact_name_cap,
    size_t* out_contact_name_len)
{
    if ((data_len > 0 && !data) || !contact_name || contact_name_max == 0) {
        return false;
    }
    const size_t name_len = strnlen(contact_name, contact_name_max);
    if (name_len == contact_name_max) return false;
    const size_t name_required = name_len + 1;

    if (out_len) *out_len = data_len;
    if (out_contact_name_len) *out_contact_name_len = name_required;
    if ((out_data && out_data_cap < data_len) ||
        (out_contact_name && out_contact_name_cap < name_required)) {
        return false;
    }

    if (out_tag) *out_tag = tag;
    if (out_data && data_len > 0) std::memcpy(out_data, data, data_len);
    if (out_contact_name) {
        std::memcpy(out_contact_name, contact_name, name_required);
    }
    return true;
}

} // namespace sigurdos::mesh::detail
