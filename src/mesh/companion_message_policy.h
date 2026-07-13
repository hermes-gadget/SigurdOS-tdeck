#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>

namespace sigurdos::mesh {

static constexpr uint8_t COMPANION_TEXT_PLAIN = 0;
static constexpr uint8_t COMPANION_TEXT_CLI_DATA = 1;
static constexpr uint8_t COMPANION_TEXT_SIGNED_PLAIN = 2;

// Match stock companion_radio: CLI response data belongs in the admin
// transcript and companion offline queue, not the normal chat timeline.
inline bool companion_message_should_present_in_chat(uint8_t txt_type)
{
    return txt_type == COMPANION_TEXT_PLAIN ||
           txt_type == COMPANION_TEXT_SIGNED_PLAIN;
}

} // namespace sigurdos::mesh
