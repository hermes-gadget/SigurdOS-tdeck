// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstdint>

namespace sigurdos::mesh {

static constexpr uint8_t AUTOADD_ADV_TYPE_CHAT = 1;
static constexpr uint8_t AUTOADD_ADV_TYPE_REPEATER = 2;
static constexpr uint8_t AUTOADD_ADV_TYPE_ROOM = 3;
static constexpr uint8_t AUTOADD_ADV_TYPE_SENSOR = 4;

static constexpr uint8_t AUTO_ADD_OVERWRITE_OLDEST = 1U << 0;
static constexpr uint8_t AUTO_ADD_CHAT = 1U << 1;
static constexpr uint8_t AUTO_ADD_REPEATER = 1U << 2;
static constexpr uint8_t AUTO_ADD_ROOM_SERVER = 1U << 3;
static constexpr uint8_t AUTO_ADD_SENSOR = 1U << 4;

inline bool autoAddEnabled(uint8_t manual_add_contacts)
{
    return (manual_add_contacts & 1U) == 0;
}

inline bool shouldAutoAddType(uint8_t manual_add_contacts,
                              uint8_t autoadd_config,
                              uint8_t contact_type)
{
    if (autoAddEnabled(manual_add_contacts)) return true;

    uint8_t type_bit = 0;
    switch (contact_type) {
        case AUTOADD_ADV_TYPE_CHAT:     type_bit = AUTO_ADD_CHAT; break;
        case AUTOADD_ADV_TYPE_REPEATER: type_bit = AUTO_ADD_REPEATER; break;
        case AUTOADD_ADV_TYPE_ROOM:     type_bit = AUTO_ADD_ROOM_SERVER; break;
        case AUTOADD_ADV_TYPE_SENSOR:   type_bit = AUTO_ADD_SENSOR; break;
        default: return false;
    }
    return (autoadd_config & type_bit) != 0;
}

inline bool shouldOverwriteAutoAddContact(uint8_t autoadd_config)
{
    return (autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) != 0;
}

}  // namespace sigurdos::mesh
