// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include "notification_model.h"

namespace sigurdos::ui {

void notifications_init();
void notifications_loop();
void notifications_post(NotificationEvent event, const char* text);
void notifications_message(const char* channel, const char* sender,
                           const char* text, bool is_self);
void notifications_login_result(const char* contact_name, bool success);
bool notifications_has_unread_mention();
void notifications_clear_unread_mentions();

} // namespace sigurdos::ui
