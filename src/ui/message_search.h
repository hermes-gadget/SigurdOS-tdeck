#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
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

// Pure, hardware-independent message-search predicates shared by the message
// search screen and its native unit tests. Keeping them header-only means the
// tests exercise the exact matching logic the UI runs, not a re-implementation.

#include <cstddef>

namespace sigurdos::ui {

// ASCII-only case fold. Multi-byte UTF-8 continuation bytes (>= 0x80) are left
// untouched so a byte-wise comparison of two identically encoded strings still
// matches — we only fold the ASCII range where 'A'..'Z' vs 'a'..'z' differs.
inline char message_search_fold(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

// Return the byte offset of the first case-insensitive occurrence of `needle`
// in `haystack`, or -1 if there is no match (or either input is null/empty).
inline int message_search_find_ci(const char* haystack, const char* needle)
{
    if (!haystack || !needle || !needle[0]) return -1;
    for (int i = 0; haystack[i] != '\0'; i++) {
        int j = 0;
        while (needle[j] != '\0') {
            const char h = haystack[i + j];
            if (h == '\0') break;  // haystack exhausted before needle completed
            if (message_search_fold(h) != message_search_fold(needle[j])) break;
            j++;
        }
        if (needle[j] == '\0') return i;  // ran the whole needle => full match
    }
    return -1;
}

// True when the query appears case-insensitively in the message body. Sender
// and conversation are displayed as result context, but the feature filters by
// message text. An empty query never matches so the results list starts blank.
inline bool message_search_matches(const char* text, const char* query)
{
    if (!query || !query[0]) return false;
    return message_search_find_ci(text, query) >= 0;
}

// Compute a display window into `text` centred on the first query match so a
// match late in a long message is still visible in a one-line snippet. Writes
// the byte offset where the window starts to `win_start` and the match offset
// (relative to the window start) to `match_off`; `match_len` receives the query
// byte length. Returns false when there is no match in `text`.
//
// `lead` is the number of bytes of context to keep before the match. The window
// start is snapped back so it never lands on a UTF-8 continuation byte, which
// would split a multi-byte codepoint and render as garbage.
inline bool message_search_snippet_window(const char* text, const char* query,
                                          int lead, int* win_start,
                                          int* match_off, int* match_len)
{
    const int pos = message_search_find_ci(text, query);
    if (pos < 0) return false;

    size_t qlen = 0;
    while (query[qlen] != '\0') qlen++;

    int start = pos - lead;
    if (start < 0) start = 0;
    // Snap left off any UTF-8 continuation byte (0b10xxxxxx) at the window edge.
    while (start > 0 &&
           (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
        start--;
    }

    if (win_start) *win_start = start;
    if (match_off) *match_off = pos - start;
    if (match_len) *match_len = static_cast<int>(qlen);
    return true;
}

} // namespace sigurdos::ui
