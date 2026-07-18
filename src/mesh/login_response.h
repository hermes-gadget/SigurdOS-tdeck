// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstdint>

namespace sigurdos {
namespace mesh {
namespace login_response {

enum class Format : uint8_t {
    Unrecognized,
    LegacySuccess,
    CurrentSuccess,
    CurrentFailure,
};

struct Parsed {
    Format format = Format::Unrecognized;
    uint16_t keep_alive_secs = 0;
    uint8_t permission = 0;
    uint8_t acl_permissions = 0;
    uint8_t firmware_level = 0;
    uint8_t failure_code = 0;
};

inline Parsed parse(const uint8_t* data, uint8_t len)
{
    Parsed result{};
    if (!data) return result;

    // Legacy repeaters return exactly [tag:4]["OK"]. Check this before the
    // current numeric response code because ASCII 'O' is also non-zero.
    if (len >= 6 && data[4] == 'O' && data[5] == 'K') {
        result.format = Format::LegacySuccess;
        return result;
    }

    // Current responses require code, keep-alive, and permission. ACL was
    // added later and all fields after it are optional for this parser.
    if (len < 7) return result;
    if (data[4] == 0) {
        result.format = Format::CurrentSuccess;
        result.keep_alive_secs = (uint16_t)data[5] * 16U;
        result.permission = data[6];
        result.acl_permissions = len >= 8 ? data[7] : 0;
        result.firmware_level = len >= 13 ? data[12] : 0;
    } else {
        result.format = Format::CurrentFailure;
        result.failure_code = data[4];
    }
    return result;
}

} // namespace login_response
} // namespace mesh
} // namespace sigurdos
