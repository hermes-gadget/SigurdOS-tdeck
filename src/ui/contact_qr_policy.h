#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "../mesh/contact_store.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sigurdos::ui {

// A contact detail route may contain either a display name or a stable public
// key ID.  Keep both values in the QR callback so the human-readable name is
// exported while the key lookup remains unambiguous.
struct ContactQrPayload {
    char stable_id[sigurdos::mesh::SIGURDOS_CONTACT_ID_BUFFER_LEN]{};
    char display_name[sigurdos::mesh::SIGURDOS_CONTACT_NAME_LEN]{};
    uint8_t type = 0;
};

inline bool contact_qr_copy_field(char* out, size_t out_size,
                                  const char* value)
{
    if (!out || out_size == 0 || !value) return false;
    const size_t length = std::strlen(value);
    if (length >= out_size) return false;
    std::memcpy(out, value, length + 1);
    return true;
}

inline bool contact_qr_payload_init(ContactQrPayload& payload,
                                    const char* stable_id,
                                    const char* display_name,
                                    uint8_t type)
{
    ContactQrPayload candidate{};
    if (!stable_id || !stable_id[0]) return false;
    if (!contact_qr_copy_field(candidate.stable_id,
                               sizeof(candidate.stable_id), stable_id) ||
        !contact_qr_copy_field(candidate.display_name,
                               sizeof(candidate.display_name), display_name)) {
        return false;
    }
    candidate.type = type;
    payload = candidate;
    return true;
}

inline bool contact_qr_build_uri(const ContactQrPayload& payload,
                                 const char* public_key_hex,
                                 char* uri_out, size_t uri_size)
{
    if (!public_key_hex || !public_key_hex[0] || !uri_out || uri_size == 0) {
        return false;
    }

    char encoded_name[sizeof(payload.display_name) * 3]{};
    size_t encoded_length = 0;
    static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
    for (const unsigned char* p =
             reinterpret_cast<const unsigned char*>(payload.display_name);
         *p; ++p) {
        const bool safe = (*p >= 'a' && *p <= 'z') ||
                          (*p >= 'A' && *p <= 'Z') ||
                          (*p >= '0' && *p <= '9') ||
                          *p == '-' || *p == '_' || *p == '.' || *p == '~';
        const size_t needed = safe ? 1U : 3U;
        if (encoded_length + needed >= sizeof(encoded_name)) return false;
        if (safe) {
            encoded_name[encoded_length++] = static_cast<char>(*p);
        } else {
            encoded_name[encoded_length++] = '%';
            encoded_name[encoded_length++] = HEX_DIGITS[*p >> 4];
            encoded_name[encoded_length++] = HEX_DIGITS[*p & 0x0F];
        }
    }
    encoded_name[encoded_length] = '\0';

    const int written = std::snprintf(
        uri_out, uri_size,
        "meshcore://contact/add?name=%s&public_key=%s&type=%u",
        encoded_name, public_key_hex, static_cast<unsigned>(payload.type));
    return written >= 0 && static_cast<size_t>(written) < uri_size;
}

} // namespace sigurdos::ui
