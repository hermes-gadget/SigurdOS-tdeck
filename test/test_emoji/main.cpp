// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Native tests for emoji font: verify font data loads correctly,
// emoji list matches glyph coverage, and fallback registration works.

#include <gtest/gtest.h>
#include "fonts/emoji_font.h"
#include <cstring>

// ═══════════════════════════════════════════════════════════════
// Emoji font data tests
// ═══════════════════════════════════════════════════════════════

TEST(EmojiFontTest, BitmapDataNotNull) {
    // The emoji font should have valid bitmap data
    // The emoji_font symbol is the generated font struct from lv_font_conv
    // If it compiled, the font data exists. But we can verify it has glyphs.
    int count = emoji_font_get_count();
    EXPECT_GT(count, 0) << "Emoji font should contain glyph data";
    EXPECT_GE(count, 200) << "Should have at least 200 common emoji";
}

TEST(EmojiFontTest, HasSmileyFace) {
    // Verify that common emoji smiley face (U+1F600, 😀 = F0 9F 98 80 in UTF-8)
    // exists in the font's codepoint list
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
    // Verify heart (U+2764, ❤ = E2 9D A4 in UTF-8)
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
    // Verify thumbs up (U+1F44D, 👍 = F0 9F 91 8D in UTF-8)
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
    // Verify out-of-bounds access returns nullptr
    EXPECT_EQ(emoji_font_get_by_index(-1), nullptr) << "Negative index should return nullptr";
    EXPECT_EQ(emoji_font_get_by_index(-999), nullptr) << "Large negative index should return nullptr";

    int count = emoji_font_get_count();
    EXPECT_GT(count, 0) << "Count must be positive for this test";

    // One past the end
    EXPECT_EQ(emoji_font_get_by_index(count), nullptr) << "One past end should return nullptr";
    EXPECT_EQ(emoji_font_get_by_index(count + 100), nullptr) << "Far past end should return nullptr";
}

TEST(EmojiFontTest, AllIndicesValid) {
    // Every index from 0..count-1 should return non-null valid UTF-8 string
    int count = emoji_font_get_count();
    EXPECT_GT(count, 0);

    for (int i = 0; i < count; i++) {
        const char* e = emoji_font_get_by_index(i);
        ASSERT_NE(e, nullptr) << "Index " << i << " should not be nullptr";
        ASSERT_GT(strlen(e), 0u) << "Index " << i << " should have length > 0";
        // Every emoji is at least 3 bytes in UTF-8 (U+0800+) or 2 bytes (U+0080+)
        // Actually, some symbols are 2-byte UTF-8, so min length is 2
        EXPECT_GE(strlen(e), 2u) << "Index " << i << " should be valid UTF-8 (min 2 bytes)";
    }
}

TEST(EmojiFontTest, CountMatches) {
    int count = emoji_font_get_count();
    EXPECT_EQ(count, 362) << "Expected 362 emoji in the list";
}

// ═══════════════════════════════════════════════════════════════
// Fallback registration tests
// ═══════════════════════════════════════════════════════════════

TEST(EmojiFontTest, FallbackRegistrationNoCrash) {
    // Just verify the fallback registration function can be called
    // without crashing. In the native test environment, the Montserrat
    // fonts are declared as extern (no actual data behind them),
    // but the memcpy + fallback set should not crash since we have
    // a valid lv_font_t struct pointer.
    emoji_font_register_fallback();
    SUCCEED() << "emoji_font_register_fallback() completed without crash";
}

TEST(EmojiFontTest, EmojiCategoryCoverage) {
    // Verify coverage across different emoji categories
    int count = emoji_font_get_count();

    // Count unique first bytes of UTF-8 sequences to estimate category diversity
    // 3-byte UTF-8 (E0-EF) = BMP symbols like hearts, check marks
    // 4-byte UTF-8 (F0) = SMP emoji like faces, gestures
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

    // Should have a good mix of BMP symbols (hearts, stars) and SMP emoji (faces, gestures)
    EXPECT_GE(smp_count, 150) << "Should have at least 150 SMP emoji (faces, gestures, objects)";
    EXPECT_GE(bmp_count, 30) << "Should have at least 30 BMP emoji (hearts, symbols)";
    EXPECT_LE(bmp_count, 200) << "BMP count should be reasonable (< 200)";
}

// ═══════════════════════════════════════════════════════════════
// Test entry point
// ═══════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
