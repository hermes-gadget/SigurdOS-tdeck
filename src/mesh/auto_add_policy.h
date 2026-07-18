#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

namespace sigurdos::mesh::auto_add_policy {

constexpr uint8_t OVERWRITE_OLDEST = 0x01;
constexpr uint8_t ADD_CHAT = 0x02;
constexpr uint8_t ADD_REPEATER = 0x04;
constexpr uint8_t ADD_ROOM = 0x08;
constexpr uint8_t ADD_SENSOR = 0x10;

inline bool enabled(uint8_t manual_add_contacts) {
    return (manual_add_contacts & 0x01) == 0;
}

inline bool typeAllowed(uint8_t manual_add_contacts, uint8_t config,
                        uint8_t contact_type) {
    if (enabled(manual_add_contacts)) return true;
    uint8_t type_bit = 0;
    switch (contact_type) {
        case 1: type_bit = ADD_CHAT; break;
        case 2: type_bit = ADD_REPEATER; break;
        case 3: type_bit = ADD_ROOM; break;
        case 4: type_bit = ADD_SENSOR; break;
        default: return false;
    }
    return (config & type_bit) != 0;
}

inline bool overwriteWhenFull(uint8_t config) {
    return (config & OVERWRITE_OLDEST) != 0;
}

}  // namespace sigurdos::mesh::auto_add_policy
