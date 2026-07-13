// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Native tests for emoji font: verify font data loads correctly,
// emoji list matches glyph coverage, fallback registration works,
// and emoji data lookup/discord-style autocomplete works.

#include <gtest/gtest.h>
#include "fonts/emoji_font.h"
#include "fonts/emoji_data.h"
#include <cstring>

// ═══════════════════════════════════════════════════════════════
// Emoji font data tests
// ═══════════════════════════════════════════════════════════════

TEST(EmojiFontTest, BitmapDataNotNull) {
    int count = emoji_font_get_count();
    EXPECT_GT(count, 0) << "Emoji font should contain glyph data";
    EXPECT_GE(count, 200) << "Should have at least 200 common emoji";
}

TEST(EmojiFontTest, HasSmileyFace) {
    const char* expected = "\xF0\x9F\x98\x80";  // 😀
    bool found = false;
    int count = emoji_font_get_count();
    for (int i = 0; i < count; i++) {
        const char* e = emoji_font_get_by_index(i);
        if (e && strcmp(e, expected) == 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Smiley face (😀, U+1F600) should be in the emoji font";
}

TEST(EmojiFontTest, HasHeart) {
    const char* expected = "\xE2\x9D\xA4";  // ❤
    bool found = false;
    int count = emoji_font_get_count();
    for (int i = 0; i < count; i++) {
        const char* e = emoji_font_get_by_index(i);
        if (e && strcmp(e, expected) == 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Heart (❤, U+2764) should be in the emoji font";
}

TEST(EmojiFontTest, HasThumbsUp) {
    const char* expected = "\xF0\x9F\x91\x8D";  // 👍
    bool found = false;
    int count = emoji_font_get_count();
    for (int i = 0; i < count; i++) {
        const char* e = emoji_font_get_by_index(i);
        if (e && strcmp(e, expected) == 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Thumbs up (👍, U+1F44D) should be in the emoji font";
}

TEST(EmojiFontTest, IndexBoundsCheck) {
    EXPECT_EQ(emoji_font_get_by_index(-1), nullptr) << "Negative index should return nullptr";
    EXPECT_EQ(emoji_font_get_by_index(-999), nullptr) << "Large negative index should return nullptr";
    int count = emoji_font_get_count();
    EXPECT_GT(count, 0) << "Count must be positive for this test";
    EXPECT_EQ(emoji_font_get_by_index(count), nullptr) << "One past end should return nullptr";
    EXPECT_EQ(emoji_font_get_by_index(count + 100), nullptr) << "Far past end should return nullptr";
}

TEST(EmojiFontTest, AllIndicesValid) {
    int count = emoji_font_get_count();
    EXPECT_GT(count, 0);
    for (int i = 0; i < count; i++) {
        const char* e = emoji_font_get_by_index(i);
        ASSERT_NE(e, nullptr) << "Index " << i << " should not be nullptr";
        ASSERT_GT(strlen(e), 0u) << "Index " << i << " should have length > 0";
        EXPECT_GE(strlen(e), 2u) << "Index " << i << " should be valid UTF-8 (min 2 bytes)";
    }
}

TEST(EmojiFontTest, CountMatches) {
    int count = emoji_font_get_count();
    EXPECT_EQ(count, 365) << "Expected 365 emoji in the list";
}

TEST(EmojiFontTest, EmojiCategoryCoverage) {
    int count = emoji_font_get_count();
    int bmp_count = 0;
    int smp_count = 0;
    for (int i = 0; i < count; i++) {
        const char* e = emoji_font_get_by_index(i);
        if (e) {
            unsigned char first = (unsigned char)e[0];
            if (first >= 0xF0) smp_count++;
            else if (first >= 0xE0) bmp_count++;
        }
    }
    EXPECT_GE(smp_count, 150) << "Should have at least 150 SMP emoji (faces, gestures, objects)";
    EXPECT_GE(bmp_count, 30) << "Should have at least 30 BMP emoji (hearts, symbols)";
    EXPECT_LE(bmp_count, 200) << "BMP count should be reasonable (< 200)";
}

// ═══════════════════════════════════════════════════════════════
// Emoji data lookup tests (Discord-style :emoji: autocomplete)
// ═══════════════════════════════════════════════════════════════

TEST(EmojiDataTest, HasSmileLookup) {
    EmojiEntry out[4];
    int n = emoji_search("smile", out, 4);
    EXPECT_GE(n, 1) << "Should find at least :smile:";
    bool found_smile = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i].short_name, "smile") == 0) {
            found_smile = true;
            EXPECT_STREQ(out[i].utf8, "\xF0\x9F\x98\x84")
                << ":smile: should map to 😄";
            break;
        }
    }
    EXPECT_TRUE(found_smile) << ":smile: should be in the emoji database";
}

TEST(EmojiDataTest, HasHeartLookup) {
    EmojiEntry out[2];
    int n = emoji_search("heart", out, 2);
    EXPECT_GE(n, 1) << "Should find :heart:";
    bool found_heart = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i].short_name, "heart") == 0) {
            found_heart = true;
            EXPECT_STREQ(out[i].utf8, "\xE2\x9D\xA4")
                << ":heart: should map to ❤";
            break;
        }
    }
    EXPECT_TRUE(found_heart) << ":heart: should be in the emoji database";
}

TEST(EmojiDataTest, HasThumbsUpLookup) {
    EmojiEntry out[2];
    int n = emoji_search("+1", out, 2);
    EXPECT_GE(n, 1) << "Should find :+1:";
    EXPECT_STREQ(out[0].short_name, "+1") << "First match should be :+1:";
    EXPECT_STREQ(out[0].utf8, "\xF0\x9F\x91\x8D") << ":+1: should map to 👍";
}

TEST(EmojiDataTest, CorrectNamedFaceCodepoints) {
    EmojiEntry out[2];

    ASSERT_EQ(emoji_search("exploding_head", out, 2), 1);
    EXPECT_STREQ(out[0].utf8, "\xF0\x9F\xA4\xAF");  // U+1F92F

    ASSERT_EQ(emoji_search("face_vomiting", out, 2), 1);
    EXPECT_STREQ(out[0].utf8, "\xF0\x9F\xA4\xAE");  // U+1F92E

    ASSERT_EQ(emoji_search("face_with_monocle", out, 2), 1);
    EXPECT_STREQ(out[0].short_name, "face_with_monocle");
    EXPECT_STREQ(out[0].utf8, "\xF0\x9F\xA7\x90");  // U+1F9D0

    ASSERT_EQ(emoji_search("nerd_face", out, 2), 1);
    EXPECT_STREQ(out[0].short_name, "nerd_face");
    EXPECT_STREQ(out[0].utf8, "\xF0\x9F\xA4\x93");  // U+1F913
}

TEST(EmojiDataTest, PrefixSearchReturnsSorted) {
    EmojiEntry out[16];
    int n = emoji_search("sm", out, 16);
    EXPECT_GE(n, 5) << "Should find multiple 'sm*' emoji";
    for (int i = 1; i < n; i++) {
        EXPECT_LE(strcmp(out[i-1].short_name, out[i].short_name), 0)
            << "Results should be alphabetically sorted";
    }
}

TEST(EmojiDataTest, NoMatchReturnsZero) {
    EmojiEntry out[4];
    int n = emoji_search("zzzznonexistent", out, 4);
    EXPECT_EQ(n, 0) << "Non-existent prefix should return 0";
}

TEST(EmojiDataTest, EmptyPrefixReturnsZero) {
    EmojiEntry out[4];
    int n = emoji_search("", out, 4);
    EXPECT_EQ(n, 0) << "Empty prefix should return 0";
}

TEST(EmojiDataTest, NullPrefixReturnsZero) {
    EmojiEntry out[4];
    int n = emoji_search(nullptr, out, 4);
    EXPECT_EQ(n, 0) << "Null prefix should return 0";
}

TEST(EmojiDataTest, NullOutputReturnsZero) {
    int n = emoji_search("smile", nullptr, 4);
    EXPECT_EQ(n, 0) << "Null output buffer should return 0";
}

TEST(EmojiDataTest, NonPositiveMaxResultsReturnZero) {
    EmojiEntry out[4];
    EXPECT_EQ(emoji_search("smile", out, 0), 0)
        << "Zero max_results should return 0";
    EXPECT_EQ(emoji_search("smile", out, -1), 0)
        << "Negative max_results should return 0";
}

TEST(EmojiDataTest, MaxResultsBound) {
    EmojiEntry out[3];
    int n = emoji_search("a", out, 3);
    EXPECT_EQ(n, 3) << "Should be capped at max_results=3";
}

TEST(EmojiDataTest, RocketLookup) {
    EmojiEntry out[2];
    int n = emoji_search("rocket", out, 2);
    EXPECT_EQ(n, 1) << "Should find exactly :rocket:";
    EXPECT_STREQ(out[0].short_name, "rocket");
    EXPECT_STREQ(out[0].utf8, "\xF0\x9F\x9A\x80");
}

TEST(EmojiDataTest, FireLookup) {
    EmojiEntry out[4];
    int n = emoji_search("fire", out, 4);
    EXPECT_EQ(n, 2) << "Should find :fire: and :fire_engine:";
    EXPECT_STREQ(out[0].short_name, "fire");
    // 🔥 = F0 9F 94 A5
    EXPECT_STREQ(out[0].utf8, "\xF0\x9F\x94\xA5");
}

TEST(EmojiDataTest, ZapLookup) {
    EmojiEntry out[4];
    int n = emoji_search("zap", out, 4);
    EXPECT_GE(n, 1) << "Should find :zap:";
    EXPECT_STREQ(out[0].short_name, "zap");
}

TEST(EmojiDataTest, NameDataNotNull) {
    EXPECT_GT(EMOJI_DATA_COUNT, 200) << "Should have at least 200 named emoji";
    EXPECT_LE(EMOJI_DATA_COUNT, 400) << "Should have at most 400 named emoji";

    // Verify all entry names are non-empty and utf8 pointers are valid
    for (int i = 0; i < EMOJI_DATA_COUNT; i++) {
        EXPECT_NE(emoji_data[i].short_name, nullptr)
            << "Entry " << i << " short_name should not be null";
        EXPECT_GT(strlen(emoji_data[i].short_name), 0u)
            << "Entry " << i << " short_name should not be empty";
        EXPECT_NE(emoji_data[i].utf8, nullptr)
            << "Entry " << i << " utf8 should not be null";
        EXPECT_GT(strlen(emoji_data[i].utf8), 0u)
            << "Entry " << i << " utf8 should not be empty";
    }
}

TEST(EmojiDataTest, DataIsSorted) {
    for (int i = 1; i < EMOJI_DATA_COUNT; i++) {
        EXPECT_LE(strcmp(emoji_data[i-1].short_name, emoji_data[i].short_name), 0)
            << "emoji_data should be sorted alphabetically at index " << i;
    }
}

// ═══════════════════════════════════════════════════════════════
// Fallback registration tests
// ═══════════════════════════════════════════════════════════════

TEST(EmojiFontTest, FallbackRegistrationNoCrash) {
    emoji_font_register_fallback();
    SUCCEED() << "emoji_font_register_fallback() completed without crash";
}

// ═══════════════════════════════════════════════════════════════
// Test entry point
// ═══════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
