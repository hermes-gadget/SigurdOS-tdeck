// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <gtest/gtest.h>
#include "utils/utf8_util.h"
using namespace slopos;

TEST(Utf8Truncation, AsciiOnly) {
    const char* s = "Hello World";
    EXPECT_EQ(slopos::utf8_truncate_bytes(s, 100), strlen(s));  // fits
    EXPECT_EQ(slopos::utf8_truncate_bytes(s, 5), 5);            // "Hello" — clean cut at ASCII boundary
}

TEST(Utf8Truncation, AsciiExactlyAtLimit) {
    const char* s = "Hello";
    EXPECT_EQ(utf8_truncate_bytes(s, 5), 5);  // exactly fits, no truncation needed
}

TEST(Utf8Truncation, EmojiCutMidSequence) {
    // 🚀 = \xF0\x9F\x9A\x80, total 4 bytes
    // "AB🚀" = 6 bytes
    const char* s = "AB" "\xF0\x9F\x9A\x80";
    ASSERT_EQ(strlen(s), (size_t)6);
    // Truncate at 5 — should cut before 🚀
    EXPECT_EQ(utf8_truncate_bytes(s, 5), (size_t)2);  // "AB" only
}

TEST(Utf8Truncation, EmojiFitsExactly) {
    const char* s = "AB" "\xF0\x9F\x9A\x80";
    EXPECT_EQ(utf8_truncate_bytes(s, 6), (size_t)6);  // exactly fits all 6 bytes
}

TEST(Utf8Truncation, EmojiFitsWithRoom) {
    const char* s = "AB" "\xF0\x9F\x9A\x80";
    EXPECT_EQ(utf8_truncate_bytes(s, 10), (size_t)6);  // fits with room, returns actual len
}

TEST(Utf8Truncation, TwoByteCharCut) {
    // é = \xC3\xA9 (2-byte UTF-8)
    const char* s = "caf" "\xC3\xA9";
    ASSERT_EQ(strlen(s), (size_t)5);
    EXPECT_EQ(utf8_truncate_bytes(s, 4), (size_t)3);  // "caf" only, é excluded
}

TEST(Utf8Truncation, ThreeByteCharCut) {
    // € = \xE2\x82\xAC (3-byte UTF-8)
    const char* s = "5" "\xE2\x82\xAC";
    ASSERT_EQ(strlen(s), (size_t)4);
    EXPECT_EQ(utf8_truncate_bytes(s, 2), (size_t)1);  // "5" only, € excluded
}

TEST(Utf8Truncation, MixedContent) {
    // "Hello🚀World" = H(1) e(2) l(3) l(4) o(5) 🚀(6,7,8,9) W(10) o(11) r(12) l(13) d(14) = 14 bytes
    const char* s = "Hello" "\xF0\x9F\x9A\x80" "World";
    ASSERT_EQ(strlen(s), (size_t)14);
    // Truncate at 7 — cuts in middle of 🚀 (bytes 5-8)
    EXPECT_EQ(utf8_truncate_bytes(s, 7), (size_t)5);   // "Hello" only
    // Truncate at 9 — after 🚀 (5+4 = 9 bytes for "Hello🚀")
    EXPECT_EQ(utf8_truncate_bytes(s, 9), (size_t)9);   // "Hello🚀" preserved
    // Truncate at 14 — exactly fits
    EXPECT_EQ(utf8_truncate_bytes(s, 14), (size_t)14); // full string
}

TEST(Utf8Truncation, ZeroLimit) {
    const char* s = "hello";
    EXPECT_EQ(utf8_truncate_bytes(s, 0), (size_t)0);
}

TEST(Utf8Truncation, EmptyString) {
    EXPECT_EQ(utf8_truncate_bytes("", 10), (size_t)0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
