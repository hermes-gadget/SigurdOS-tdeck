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

// Exercises the exact search predicates the message-search screen runs
// (src/ui/message_search.h) rather than a re-implementation.

#include <gtest/gtest.h>
#include <cstring>
#include "message_search.h"

using namespace sigurdos::ui;

// ── message_search_find_ci ────────────────────────────────

TEST(MessageSearchFind, MatchesAtStart) {
    EXPECT_EQ(message_search_find_ci("hello world", "hello"), 0);
}

TEST(MessageSearchFind, MatchesInMiddle) {
    EXPECT_EQ(message_search_find_ci("say hello now", "hello"), 4);
}

TEST(MessageSearchFind, CaseInsensitiveBothDirections) {
    EXPECT_EQ(message_search_find_ci("Meeting at NOON", "noon"), 11);
    EXPECT_EQ(message_search_find_ci("meeting at noon", "NOON"), 11);
    EXPECT_EQ(message_search_find_ci("MeEtInG", "eeting"), 1);
}

TEST(MessageSearchFind, NoMatchReturnsNegative) {
    EXPECT_EQ(message_search_find_ci("hello world", "xyz"), -1);
}

TEST(MessageSearchFind, NeedleLongerThanRemainderNoOverrun) {
    // The needle nearly matches at the tail but runs past the end — must not
    // read past the terminator or report a false match.
    EXPECT_EQ(message_search_find_ci("abcde", "cdef"), -1);
    EXPECT_EQ(message_search_find_ci("abc", "abcd"), -1);
}

TEST(MessageSearchFind, EmptyAndNullInputs) {
    EXPECT_EQ(message_search_find_ci("hello", ""), -1);
    EXPECT_EQ(message_search_find_ci("hello", nullptr), -1);
    EXPECT_EQ(message_search_find_ci(nullptr, "hello"), -1);
    EXPECT_EQ(message_search_find_ci("", "hello"), -1);
}

TEST(MessageSearchFind, FirstOccurrenceWins) {
    EXPECT_EQ(message_search_find_ci("na na na batman", "na"), 0);
}

// ── message_search_matches ────────────────────────────────

TEST(MessageSearchMatches, MatchesBodyText) {
    EXPECT_TRUE(message_search_matches("meet at the dock", "dock"));
}

TEST(MessageSearchMatches, DoesNotSearchResultContext) {
    EXPECT_FALSE(message_search_matches("hi there", "Alice"));
    EXPECT_FALSE(message_search_matches("hi there", "general"));
}

TEST(MessageSearchMatches, NoBodyMatch) {
    EXPECT_FALSE(message_search_matches("hi there", "zzz"));
}

TEST(MessageSearchMatches, EmptyQueryNeverMatches) {
    EXPECT_FALSE(message_search_matches("anything", ""));
    EXPECT_FALSE(message_search_matches("anything", nullptr));
}

// ── message_search_snippet_window ─────────────────────────

TEST(MessageSearchSnippet, WindowStartsAtBeginningForEarlyMatch) {
    int win = -1, off = -1, len = -1;
    ASSERT_TRUE(message_search_snippet_window("hello world", "hello", 10,
                                              &win, &off, &len));
    EXPECT_EQ(win, 0);
    EXPECT_EQ(off, 0);
    EXPECT_EQ(len, 5);
}

TEST(MessageSearchSnippet, WindowScrollsToLateMatch) {
    const char* text = "the quick brown fox jumps over the lazy dog";
    int win = -1, off = -1, len = -1;
    ASSERT_TRUE(message_search_snippet_window(text, "lazy", 10, &win, &off, &len));
    // Match is at byte 35; with 10 bytes of lead the window starts at 25.
    EXPECT_EQ(win, 25);
    EXPECT_EQ(off, 10);
    EXPECT_EQ(len, 4);
    // The matched substring lands exactly where the offsets say.
    EXPECT_EQ(0, std::strncmp(text + win + off, "lazy", 4));
}

TEST(MessageSearchSnippet, NoMatchReturnsFalse) {
    int win = 99, off = 99, len = 99;
    EXPECT_FALSE(message_search_snippet_window("hello", "zzz", 10, &win, &off, &len));
}

TEST(MessageSearchSnippet, WindowDoesNotSplitMultibyteCodepoint) {
    // "café — meet" : 'é' is the 2-byte sequence 0xC3 0xA9 at bytes 3..4.
    // A match on "meet" with a lead that would land mid-'é' must snap back to
    // the codepoint's lead byte so the snippet stays valid UTF-8.
    const char* text = "caf\xC3\xA9 meet";  // bytes: c a f C3 A9 ' ' m e e t
    const int meet = message_search_find_ci(text, "meet");
    ASSERT_EQ(meet, 6);
    int win = -1, off = -1, len = -1;
    ASSERT_TRUE(message_search_snippet_window(text, "meet", 3, &win, &off, &len));
    // pos-lead = 3 which is the 0xC3 lead byte itself — already valid, kept.
    EXPECT_EQ(win, 3);
    // A window landing on the continuation byte (0xA9 at 4) must snap back to 3.
    int win2 = -1, off2 = -1, len2 = -1;
    ASSERT_TRUE(message_search_snippet_window(text, "meet", 2, &win2, &off2, &len2));
    EXPECT_EQ(win2, 3);
    EXPECT_NE(static_cast<unsigned char>(text[win2]) & 0xC0, 0x80);
}
