// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

namespace sigurdos::ui {

static constexpr int CHAT_LIST_RENDER_CAPACITY = 24;
static constexpr int PACKET_LIST_RENDER_CAPACITY = 7;

struct ListWindow {
    int start;
    int end;

    int count() const { return end - start; }
};

inline int list_page_count(int total, int capacity)
{
    if (total <= 0 || capacity <= 0) return 0;
    return (total + capacity - 1) / capacity;
}

inline int list_clamp_page(int page, int total, int capacity)
{
    const int pages = list_page_count(total, capacity);
    if (pages == 0 || page < 0) return 0;
    return page >= pages ? pages - 1 : page;
}

inline int list_clamp_newest_offset(int offset, int total)
{
    if (offset < 0 || total <= 0) return 0;
    return offset >= total ? total - 1 : offset;
}

inline ListWindow list_window_from_newest_offset(int total, int capacity,
                                                  int newest_offset)
{
    if (total <= 0 || capacity <= 0) return {0, 0};
    newest_offset = list_clamp_newest_offset(newest_offset, total);
    const int end = total - newest_offset;
    const int start = end > capacity ? end - capacity : 0;
    return {start, end};
}

// Page zero is the newest page. Entries within the window remain in their
// original oldest-to-newest order so callers can bind rows directly.
inline ListWindow list_window_from_newest(int total, int capacity, int page)
{
    if (total <= 0 || capacity <= 0) return {0, 0};
    page = list_clamp_page(page, total, capacity);
    return list_window_from_newest_offset(total, capacity, page * capacity);
}

} // namespace sigurdos::ui
