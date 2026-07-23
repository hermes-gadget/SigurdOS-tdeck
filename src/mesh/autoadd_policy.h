// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include "auto_add_policy.h"

namespace sigurdos::mesh {

// Compatibility names retained for callers that used the original header.
// All behavior delegates to auto_add_policy, the production implementation.
static constexpr uint8_t AUTOADD_ADV_TYPE_CHAT = 1;
static constexpr uint8_t AUTOADD_ADV_TYPE_REPEATER = 2;
static constexpr uint8_t AUTOADD_ADV_TYPE_ROOM = 3;
static constexpr uint8_t AUTOADD_ADV_TYPE_SENSOR = 4;

static constexpr uint8_t AUTO_ADD_OVERWRITE_OLDEST =
    auto_add_policy::OVERWRITE_OLDEST;
static constexpr uint8_t AUTO_ADD_CHAT = auto_add_policy::ADD_CHAT;
static constexpr uint8_t AUTO_ADD_REPEATER = auto_add_policy::ADD_REPEATER;
static constexpr uint8_t AUTO_ADD_ROOM_SERVER = auto_add_policy::ADD_ROOM;
static constexpr uint8_t AUTO_ADD_SENSOR = auto_add_policy::ADD_SENSOR;

inline bool autoAddEnabled(uint8_t manual_add_contacts)
{
    return auto_add_policy::enabled(manual_add_contacts);
}

inline bool shouldAutoAddType(uint8_t manual_add_contacts,
                              uint8_t autoadd_config,
                              uint8_t contact_type)
{
    return auto_add_policy::typeAllowed(
        manual_add_contacts, autoadd_config, contact_type);
}

inline bool shouldOverwriteAutoAddContact(uint8_t autoadd_config)
{
    return auto_add_policy::overwriteWhenFull(autoadd_config);
}

}  // namespace sigurdos::mesh
