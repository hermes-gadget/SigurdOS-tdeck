// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>

namespace sigurdos::mesh::detail {

// Clear key-scoped fixed-table entries without changing the slot numbering.
// This is used for pending tables where terminal entries may remain visible to
// callers until their normal expiry/reuse policy runs.
template <typename Entry, typename Match>
std::size_t clearMatchingEntries(Entry* entries, std::size_t count,
                                 Match match)
{
    if (!entries) return 0;
    std::size_t cleared = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!match(entries[i])) continue;
        entries[i] = Entry{};
        ++cleared;
    }
    return cleared;
}

// Compact key-scoped entries whose count is tracked separately (for example a
// bounded fetch or signal table), preserving the relative order of others.
template <typename Entry, typename Match>
std::size_t compactRemoveMatchingEntries(Entry* entries, std::size_t count,
                                          Match match)
{
    if (!entries) return count;
    std::size_t write = 0;
    for (std::size_t read = 0; read < count; ++read) {
        if (match(entries[read])) continue;
        if (write != read) entries[write] = entries[read];
        ++write;
    }
    for (std::size_t i = write; i < count; ++i) entries[i] = Entry{};
    return write;
}

} // namespace sigurdos::mesh::detail
