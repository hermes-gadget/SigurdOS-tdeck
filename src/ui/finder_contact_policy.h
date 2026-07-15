#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>
#include <limits>

namespace sigurdos::ui {

static constexpr int FINDER_RECENT_CONTACT_LIMIT = 32;
static constexpr uint32_t FINDER_RECENT_WINDOW_SECONDS = 120;

inline uint32_t finder_contact_age(uint32_t now, uint32_t last_seen)
{
    if (now == 0 || last_seen == 0) return std::numeric_limits<uint32_t>::max();
    return now >= last_seen ? now - last_seen : 0;
}

template <typename Contact>
int finder_select_recent_contacts(
    const Contact* contacts, int total,
    Contact* selected, int selected_capacity,
    uint32_t now,
    uint32_t window_seconds = FINDER_RECENT_WINDOW_SECONDS)
{
    if (!contacts || total <= 0 || !selected || selected_capacity <= 0 || now == 0) {
        return 0;
    }

    int selected_count = 0;
    for (int i = 0; i < total; i++) {
        if (finder_contact_age(now, contacts[i].last_seen) > window_seconds) continue;

        int insert_at = 0;
        while (insert_at < selected_count &&
               selected[insert_at].last_seen >= contacts[i].last_seen) {
            insert_at++;
        }
        if (insert_at >= selected_capacity) continue;

        if (selected_count < selected_capacity) selected_count++;
        for (int j = selected_count - 1; j > insert_at; j--) {
            selected[j] = selected[j - 1];
        }
        selected[insert_at] = contacts[i];
    }
    return selected_count;
}

} // namespace sigurdos::ui
