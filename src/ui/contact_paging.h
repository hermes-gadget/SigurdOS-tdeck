#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

namespace sigurdos::ui {

// Keep LVGL row/object count bounded independently of the 350-contact store.
// Twelve rows is two screenfuls, preserving useful local scrolling without
// constructing the previous 160+ row/label objects per page.
static constexpr int CONTACT_LIST_PAGE_SIZE = 12;

inline int contact_page_count(int total, int page_size = CONTACT_LIST_PAGE_SIZE) {
    if (total <= 0 || page_size <= 0) return 0;
    return (total + page_size - 1) / page_size;
}

inline int contact_clamp_page(int page, int total, int page_size = CONTACT_LIST_PAGE_SIZE) {
    int pages = contact_page_count(total, page_size);
    if (pages <= 0) return 0;
    if (page < 0) return 0;
    if (page >= pages) return pages - 1;
    return page;
}

inline int contact_page_start(int page, int total, int page_size = CONTACT_LIST_PAGE_SIZE) {
    page = contact_clamp_page(page, total, page_size);
    int start = page * page_size;
    return start < total ? start : total;
}

inline int contact_page_end(int page, int total, int page_size = CONTACT_LIST_PAGE_SIZE) {
    int start = contact_page_start(page, total, page_size);
    int end = start + page_size;
    return end < total ? end : total;
}

} // namespace sigurdos::ui
