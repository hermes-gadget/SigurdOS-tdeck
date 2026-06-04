#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.


#include "../hal/trackball.h"
#include <lvgl.h>
#include <cstdint>

namespace sigurdos::ui {

// Create and show the chat screen
void chat_screen_show();

// Open a direct message conversation with a contact (creates if needed)
void chat_screen_open_dm(const char* contact_name);

// Set which channels to show: 0=all, 1=#channels only, 2=DMs only
void chat_screen_set_filter(int mode);

// Add a message to the chat display
void chat_screen_add_msg(const char* channel, const char* sender, const char* text, bool is_self);

// Handle trackball events for the chat screen. Returns true if consumed.
bool chat_screen_handle_trackball(SigurdOSTrackballEvent event);

// Return the chat input textarea object if the messaging view is active, else nullptr.
lv_obj_t* chat_screen_get_input_field();

// Return the name of the currently active channel (e.g. "#eng-nw"), or "" if none.
const char* chat_screen_get_active_channel_name();

// Chat message history cap (per-channel): get/set and persistence-backed config.
uint16_t chat_screen_get_message_cap();
void     chat_screen_set_message_cap(uint16_t cap);

// Persist/restore per-channel message history to/from SPIFFS
void chat_save_messages();
void chat_load_messages();

// Periodically check for newly arrived ACKs and re-render if needed.
// Call from the main UI loop (~1s interval).
void chat_screen_refresh_acks();

} // namespace sigurdos::ui
