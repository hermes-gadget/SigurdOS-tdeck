// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos::ui {

enum class ContactSortMode : uint8_t { Alpha, Recent, Type };
enum class ContactFilterMode : uint8_t { All, Favourites, Infrastructure, HasPath };

inline bool contact_filter_accepts(ContactFilterMode mode, uint8_t type,
                                   bool favourite, bool has_path)
{
    switch (mode) {
        case ContactFilterMode::Favourites: return favourite;
        case ContactFilterMode::Infrastructure: return type == 2 || type == 3;
        case ContactFilterMode::HasPath: return has_path;
        default: return true;
    }
}

inline int contact_sort_compare(ContactSortMode mode,
                                const char* a_name, uint8_t a_type, uint32_t a_seen,
                                const char* b_name, uint8_t b_type, uint32_t b_seen)
{
    if (mode == ContactSortMode::Recent && a_seen != b_seen) {
        return a_seen > b_seen ? -1 : 1;
    }
    if (mode == ContactSortMode::Type && a_type != b_type) {
        return a_type < b_type ? -1 : 1;
    }
    return std::strcmp(a_name ? a_name : "", b_name ? b_name : "");
}

inline bool contact_manual_fields_valid(const char* name, const char* pubkey_hex)
{
    if (!name || !name[0] || std::strlen(name) > 31 || !pubkey_hex ||
        std::strlen(pubkey_hex) != 64) return false;
    bool nonzero = false;
    for (size_t i = 0; i < 64; ++i) {
        const char c = pubkey_hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return false;
        nonzero = nonzero || c != '0';
    }
    return nonzero;
}

inline const char* contact_quality_name(int rssi, float snr)
{
    if (rssi == 0) return "?";
    if (rssi >= -90 && snr >= 0.0f) return "GOOD";
    if (rssi >= -110 && snr >= -8.0f) return "FAIR";
    return "WEAK";
}

} // namespace sigurdos::ui
