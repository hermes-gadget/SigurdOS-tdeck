// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include <cstring>
#include <set>
#include <string>

#include "fonts/emoji_font.h"

namespace {

TEST(EmojiFontIntegrity, CountMatchesGeneratedList) {
    EXPECT_EQ(emoji_font_get_count(), 365);
}

TEST(EmojiFontIntegrity, CorrectedAndPreviouslyOmittedGlyphsAreIndexed) {
    const std::set<std::string> required = {
        "\xF0\x9F\xA4\x93",  // U+1F913 nerd face
        "\xF0\x9F\xA4\xA3",  // U+1F923 rolling on the floor laughing
        "\xF0\x9F\xA7\x90",  // U+1F9D0 face with monocle
    };
    std::set<std::string> indexed;
    for (int i = 0; i < emoji_font_get_count(); i++) {
        indexed.insert(emoji_font_get_by_index(i));
    }

    for (const auto& glyph : required) {
        EXPECT_EQ(indexed.count(glyph), 1u);
    }
}

TEST(EmojiFontIntegrity, EveryIndexedEntryIsPresent) {
    const int count = emoji_font_get_count();
    ASSERT_GT(count, 0);

    for (int i = 0; i < count; i++) {
        const char* value = emoji_font_get_by_index(i);
        ASSERT_NE(value, nullptr) << "index " << i;
        EXPECT_GT(strlen(value), 0u) << "index " << i;
    }
}

TEST(EmojiFontIntegrity, IndexedEntriesAreUnique) {
    const int count = emoji_font_get_count();
    ASSERT_GT(count, 0);

    std::set<std::string> seen;
    for (int i = 0; i < count; i++) {
        const char* value = emoji_font_get_by_index(i);
        ASSERT_NE(value, nullptr) << "index " << i;
        EXPECT_TRUE(seen.insert(value).second) << "duplicate at index " << i;
    }
    EXPECT_EQ(seen.size(), static_cast<size_t>(count));
}

TEST(EmojiFontIntegrity, BoundaryIndicesAreValidOnlyInsideRange) {
    const int count = emoji_font_get_count();
    ASSERT_GT(count, 0);

    EXPECT_EQ(emoji_font_get_by_index(-1), nullptr);
    EXPECT_NE(emoji_font_get_by_index(0), nullptr);
    EXPECT_NE(emoji_font_get_by_index(count - 1), nullptr);
    EXPECT_EQ(emoji_font_get_by_index(count), nullptr);
}

} // anonymous namespace
