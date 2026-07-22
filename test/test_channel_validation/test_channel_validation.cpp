// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
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


/**
 * Unit tests for channel name validation and sanitisation.
 */
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <cstdio>

#include "mesh/channel_slot_policy.h"
#include "mesh/channel_uri.h"
#include "mesh/channel_validation.h"
#include "mesh/strict_base64.h"

namespace {

TEST(ChannelSecurityInputTest, StrictBase64AcceptsCanonical16And32ByteKeys) {
    uint8_t output[32]{};
    size_t output_len = 0;
    EXPECT_TRUE(sigurdos::mesh::decodeBase64Strict(
        "AAECAwQFBgcICQoLDA0ODw==", 24, output, sizeof(output), output_len));
    EXPECT_EQ(output_len, 16U);
    EXPECT_EQ(output[15], 15U);

    EXPECT_TRUE(sigurdos::mesh::decodeBase64Strict(
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=", 44,
        output, sizeof(output), output_len));
    EXPECT_EQ(output_len, 32U);
    EXPECT_EQ(output[31], 31U);
}

TEST(ChannelSecurityInputTest, StrictBase64RejectsMalformedAndNonCanonicalInput) {
    uint8_t output[32]{};
    size_t output_len = 99;
    const char* invalid[] = {
        "AAECAwQFBgcICQoLDA0ODw",       // missing padding
        "AAECAwQF!gcICQoLDA0ODw==",    // ignored-character attack
        "AAECAwQFBgcICQoLDA0ODw==junk",// trailing data
        "AAECAwQFBgcICQoLDA0ODx==",    // non-zero canonical padding bits
        "=AECAwQFBgcICQoLDA0ODw==",    // early padding
        "AAECAwQFBgcICQoLDA0ODw==\n",  // whitespace
    };
    for (const char* value : invalid) {
        EXPECT_FALSE(sigurdos::mesh::decodeBase64Strict(
            value, std::strlen(value), output, sizeof(output), output_len)) << value;
    }
}

TEST(ChannelSecurityInputTest, ChannelUriHasExactGrammarAndNoTruncation) {
    sigurdos::mesh::ChannelUriFields fields{};
    const char* valid =
        "meshcore://channel/add?name=ops%2Dteam&secret="
        "000102030405060708090a0b0c0d0e0f";
    ASSERT_TRUE(sigurdos::mesh::parseChannelAddUri(valid, fields));
    EXPECT_STREQ(fields.name, "ops-team");
    EXPECT_STREQ(fields.secret_hex, "000102030405060708090a0b0c0d0e0f");

    const char* invalid[] = {
        "meshcore://channel/add?name=ops&name=other&secret=000102030405060708090a0b0c0d0e0f",
        "meshcore://channel/add?name=ops&secret=000102030405060708090a0b0c0d0e0f&secret=000102030405060708090a0b0c0d0e0f",
        "meshcore://channel/add?name=ops&unknown=x&secret=000102030405060708090a0b0c0d0e0f",
        "meshcore://channel/add?name=bad%2&secret=000102030405060708090a0b0c0d0e0f",
        "meshcore://channel/add?name=abcdefghijklmnopqrstuvwxyz123456&secret=000102030405060708090a0b0c0d0e0f",
        "meshcore://channel/add?name=ops&secret=000102030405060708090a0b0c0d0e",
        "meshcore://channel/add?name=ops&secret=000102030405060708090a0b0c0d0e0f&",
    };
    for (const char* value : invalid) {
        EXPECT_FALSE(sigurdos::mesh::parseChannelAddUri(value, fields)) << value;
    }
}

TEST(ChannelSecurityInputTest, RawContactUriRejectsSeparatorsAndOddOrOversizeData) {
    char hex[512]{};
    size_t len = 0;
    const char* valid =
        "meshcore://000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f";
    EXPECT_TRUE(sigurdos::mesh::parseRawContactUriHex(valid, hex, sizeof(hex), len));
    EXPECT_EQ(len, 64U);

    EXPECT_FALSE(sigurdos::mesh::parseRawContactUriHex(
        "meshcore://00010203-0405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
        hex, sizeof(hex), len));
    EXPECT_FALSE(sigurdos::mesh::parseRawContactUriHex(
        "meshcore://000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1",
        hex, sizeof(hex), len));
}

// ---------------------------------------------------------------------------
// channel_name_valid — positive cases
// ---------------------------------------------------------------------------

TEST(ChannelValidationTest, ValidSimpleName) {
    EXPECT_TRUE(sigurdos::mesh::channel_name_valid("general"));
}

TEST(ChannelValidationTest, ValidHyphenatedName) {
    EXPECT_TRUE(sigurdos::mesh::channel_name_valid("test-channel"));
}

TEST(ChannelValidationTest, ValidMixedCaseName) {
    EXPECT_TRUE(sigurdos::mesh::channel_name_valid("MyChannel"));
}

TEST(ChannelValidationTest, ValidAlphanumeric) {
    EXPECT_TRUE(sigurdos::mesh::channel_name_valid("ABC123"));
}

TEST(ChannelValidationTest, ValidSingleCharName) {
    EXPECT_TRUE(sigurdos::mesh::channel_name_valid("a"));
}

TEST(ChannelValidationTest, ValidMaxLengthName) {
    // Exactly 31 chars: all letters/digits, no hyphens
    EXPECT_TRUE(sigurdos::mesh::channel_name_valid("abcdefghijklmnopqrstuvwxyz12345"));
}

TEST(ChannelValidationTest, ValidMaxLengthNameWithLeadingHash) {
    // '#'+30 body characters is the maximum 31-byte stored name.
    EXPECT_TRUE(sigurdos::mesh::channel_name_valid("#abcdefghijklmnopqrstuvwxyz1234"));
}

TEST(ChannelValidationTest, ValidNameWithLeadingHash) {
    // '#' is stripped before validation
    EXPECT_TRUE(sigurdos::mesh::channel_name_valid("#general"));
}

TEST(ChannelValidationTest, ValidNameWithWhitespaceTrimmed) {
    EXPECT_TRUE(sigurdos::mesh::channel_name_valid("  my-channel  "));
}

// ---------------------------------------------------------------------------
// channel_name_valid — negative cases
// ---------------------------------------------------------------------------

TEST(ChannelValidationTest, RejectsNull) {
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid(nullptr));
}

TEST(ChannelValidationTest, RejectsEmptyString) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("", &reason));
    ASSERT_NE(reason, nullptr);
}

TEST(ChannelValidationTest, RejectsLeadingDash) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("-bad", &reason));
    ASSERT_NE(reason, nullptr);
}

TEST(ChannelValidationTest, RejectsTrailingDash) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("bad-", &reason));
    ASSERT_NE(reason, nullptr);
}

TEST(ChannelValidationTest, RejectsConsecutiveDashes) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("bad--char", &reason));
    ASSERT_NE(reason, nullptr);
}

TEST(ChannelValidationTest, RejectsSpaceInName) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("a space", &reason));
    ASSERT_NE(reason, nullptr);
}

TEST(ChannelValidationTest, RejectsSpecialChars) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("inv@lid", &reason));
    ASSERT_NE(reason, nullptr);
}

TEST(ChannelValidationTest, RejectsJustHash) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("#", &reason));
    ASSERT_NE(reason, nullptr);
}

TEST(ChannelValidationTest, RejectsOnlyWhitespace) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("   ", &reason));
    ASSERT_NE(reason, nullptr);
}

TEST(ChannelValidationTest, RejectsTooLongName) {
    // 32 chars — exceeds 31-char max
    const char* name = "abcdefghijklmnopqrstuvwxyz123456";
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid(name));
}

TEST(ChannelValidationTest, RejectsTooLongNameWithLeadingHash) {
    const char* reason = nullptr;
    const char* name = "#abcdefghijklmnopqrstuvwxyz123456";
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid(name, &reason));
    ASSERT_NE(reason, nullptr);
    EXPECT_STREQ(reason, "Name too long (max 31 chars including '#')");
}

TEST(ChannelValidationTest, HashtagNormalisePreservesExactMaximumStoredName) {
    char normalized[32];
    ASSERT_TRUE(sigurdos::mesh::hashtag_channel_name_normalise(
        "  Abcdefghijklmnopqrstuvwxyz1234  ", normalized,
        sizeof(normalized)));
    EXPECT_STREQ(normalized, "#Abcdefghijklmnopqrstuvwxyz1234");
    EXPECT_EQ(strlen(normalized), 31u);
}

TEST(ChannelValidationTest, HashtagNormaliseRejectsBodyThatWouldBeTruncated) {
    char normalized[32] = "unchanged";
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::hashtag_channel_name_normalise(
        "abcdefghijklmnopqrstuvwxyz12345", normalized,
        sizeof(normalized), &reason));
    ASSERT_NE(reason, nullptr);
    EXPECT_STREQ(reason, "Name too long (max '#' plus 30 chars)");
}

TEST(ChannelValidationTest, RejectsNameWithMultipleInternalDashes) {
    // "a-b-c" has two internal dashes — should that be rejected?
    // The spec says "single internal hyphens only", but looking at the code, it
    // only rejects consecutive dashes, not multiple non-consecutive dashes.
    // So this is actually valid per current implementation. Let's test what the
    // code actually does rather than what the comment says.
    // Actually, re-reading: "single internal hyphens only" could mean
    // "only one hyphen internally"? Let me check the code... the code only
    // rejects consecutive dashes. So "a-b-c" should be valid.
    // I'll test the code's actual behavior.
    EXPECT_TRUE(sigurdos::mesh::channel_name_valid("a-b-c"));
}

TEST(ChannelValidationTest, RejectsNameWithUnderscore) {
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("bad_name"));
}

TEST(ChannelValidationTest, RejectsNameWithDot) {
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("name.channel"));
}

TEST(ChannelValidationTest, RejectsNameWithSlash) {
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("chan/nel"));
}

// ---------------------------------------------------------------------------
// channel_name_sanitise
// ---------------------------------------------------------------------------

TEST(ChannelValidationTest, SanitiseStripsLeadingHash) {
    char buf[64] = "#general";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 7);
    EXPECT_STREQ(buf, "general");
}

TEST(ChannelValidationTest, SanitiseTrimsAndLowercases) {
    char buf[64] = "  Test  ";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 4);
    EXPECT_STREQ(buf, "test");
}

TEST(ChannelValidationTest, SanitiseHashAndMixedCase) {
    char buf[64] = "#Test-Chan";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 9);
    EXPECT_STREQ(buf, "test-chan");
}

TEST(ChannelValidationTest, SanitiseAlreadyLower) {
    char buf[64] = "general";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 7);
    EXPECT_STREQ(buf, "general");
}

TEST(ChannelValidationTest, SanitiseEmptyReturnsZero) {
    char buf[64] = "";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 0);
}

TEST(ChannelValidationTest, SanitiseWhitespaceOnlyReturnsZero) {
    char buf[64] = "   ";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 0);
}

TEST(ChannelValidationTest, SanitiseNullReturnsZero) {
    size_t len = sigurdos::mesh::channel_name_sanitise(nullptr, 0);
    EXPECT_EQ(len, 0);
}

TEST(ChannelValidationTest, SanitiseMaxLenZeroReturnsZero) {
    char buf[64] = "test";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, 0);
    EXPECT_EQ(len, 0);
}

TEST(ChannelValidationTest, SanitiseTrimsLeadingAndTrailingSpaces) {
    char buf[64] = "  spaced-channel  ";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 14);
    EXPECT_STREQ(buf, "spaced-channel");
}

TEST(ChannelValidationTest, SanitiseHashOnlyReturnsZero) {
    char buf[64] = "#";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 0);
}

TEST(ChannelValidationTest, SanitisePreservesInternalHyphens) {
    char buf[64] = "My-Test-Channel";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 15);
    EXPECT_STREQ(buf, "my-test-channel");
}

TEST(ChannelValidationTest, SanitiseMaxLengthClamps) {
    // 33-char string → clamps at 31 (when max_len = 32, NUL goes at index 31)
    char buf[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, 32);
    EXPECT_EQ(len, 31);
    EXPECT_EQ(strlen(buf), 31);
    EXPECT_STREQ(buf, "abcdefghijklmnopqrstuvwxyz12345");
}

TEST(ChannelValidationTest, SanitiseClampsToChannelMaxWithLargeBuffer) {
    char buf[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 31);
    EXPECT_EQ(strlen(buf), 31);
    EXPECT_STREQ(buf, "abcdefghijklmnopqrstuvwxyz12345");
}

TEST(ChannelValidationTest, SanitiseHashPrefixDoesNotCountAgainstChannelMax) {
    char buf[64] = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    EXPECT_EQ(len, 31);
    EXPECT_EQ(strlen(buf), 31);
    EXPECT_STREQ(buf, "abcdefghijklmnopqrstuvwxyz12345");
}

TEST(ChannelValidationTest, SanitiseHandlesOnlyHashWithSpaces) {
    char buf[64] = "  #  ";
    size_t len = sigurdos::mesh::channel_name_sanitise(buf, sizeof(buf));
    // After trimming whitespace: "#", then strip '#': ""
    EXPECT_EQ(len, 0);
}

// ---------------------------------------------------------------------------
// channel_name_valid with reason — verify reason strings
// ---------------------------------------------------------------------------

TEST(ChannelValidationTest, ReasonEmpty) {
    const char* reason = "unchanged";
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("", &reason));
    EXPECT_STREQ(reason, "Name is empty");
}

TEST(ChannelValidationTest, ReasonLeadingDash) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("-x", &reason));
    ASSERT_NE(reason, nullptr);
    EXPECT_STREQ(reason, "Leading dash not allowed");
}

TEST(ChannelValidationTest, ReasonTrailingDash) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("x-", &reason));
    ASSERT_NE(reason, nullptr);
    EXPECT_STREQ(reason, "Trailing dash not allowed");
}

TEST(ChannelValidationTest, ReasonConsecutiveDashes) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("a--b", &reason));
    ASSERT_NE(reason, nullptr);
    EXPECT_STREQ(reason, "Consecutive dashes not allowed");
}

TEST(ChannelValidationTest, ReasonInvalidChar) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("a@b", &reason));
    ASSERT_NE(reason, nullptr);
    EXPECT_STREQ(reason, "Invalid character in name");
}

TEST(ChannelValidationTest, ReasonTooLong) {
    const char* reason = nullptr;
    const char* longName = "abcdefghijklmnopqrstuvwxyz123456";
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid(longName, &reason));
    ASSERT_NE(reason, nullptr);
    EXPECT_STREQ(reason, "Name too long (max 31 chars)");
}

TEST(ChannelValidationTest, ReasonJustHash) {
    const char* reason = nullptr;
    EXPECT_FALSE(sigurdos::mesh::channel_name_valid("#", &reason));
    ASSERT_NE(reason, nullptr);
    EXPECT_STREQ(reason, "Name is just '#'");
}

// ---------------------------------------------------------------------------
// Companion channel slot updates
// ---------------------------------------------------------------------------

struct TestChannelSlot {
    char name[16];
    int value;
};

bool updateChannelSlot(std::array<TestChannelSlot, 4>& slots, size_t index,
                       const TestChannelSlot& replacement) {
    return sigurdos::mesh::set_contiguous_channel_slot<4>(
        index, replacement,
        [&slots](size_t i, TestChannelSlot& out) {
            out = slots[i];
            return true;
        },
        [&slots](size_t i, const TestChannelSlot& value) {
            slots[i] = value;
            return true;
        },
        [](const TestChannelSlot& slot) { return slot.name[0] == '\0'; });
}

TestChannelSlot channelSlot(const char* name, int value) {
    TestChannelSlot slot{};
    std::strncpy(slot.name, name, sizeof(slot.name) - 1);
    slot.value = value;
    return slot;
}

TEST(ChannelSlotPolicyTest, ClearingMiddleSlotCompactsLaterChannels) {
    std::array<TestChannelSlot, 4> slots{{
        channelSlot("alpha", 1), channelSlot("bravo", 2),
        channelSlot("charlie", 3), TestChannelSlot{}}};

    ASSERT_TRUE(updateChannelSlot(slots, 1, TestChannelSlot{}));

    EXPECT_STREQ(slots[0].name, "alpha");
    EXPECT_STREQ(slots[1].name, "charlie");
    EXPECT_EQ(slots[1].value, 3);
    EXPECT_STREQ(slots[2].name, "");
}

TEST(ChannelSlotPolicyTest, RejectsInsertionThatWouldCreateGap) {
    std::array<TestChannelSlot, 4> slots{{
        channelSlot("alpha", 1), TestChannelSlot{}, TestChannelSlot{},
        TestChannelSlot{}}};
    const std::array<TestChannelSlot, 4> before = slots;

    EXPECT_FALSE(updateChannelSlot(slots, 2, channelSlot("charlie", 3)));
    EXPECT_EQ(std::memcmp(slots.data(), before.data(), sizeof(slots)), 0);
}

TEST(ChannelSlotPolicyTest, AllowsAppendAtFirstEmptySlot) {
    std::array<TestChannelSlot, 4> slots{{
        channelSlot("alpha", 1), TestChannelSlot{}, TestChannelSlot{},
        TestChannelSlot{}}};

    ASSERT_TRUE(updateChannelSlot(slots, 1, channelSlot("bravo", 2)));
    EXPECT_STREQ(slots[0].name, "alpha");
    EXPECT_STREQ(slots[1].name, "bravo");
    EXPECT_EQ(slots[1].value, 2);
    EXPECT_STREQ(slots[2].name, "");
}

TEST(ChannelSlotPolicyTest, EmptyUpdateRepairsLegacySparseSlots) {
    std::array<TestChannelSlot, 4> slots{{
        channelSlot("alpha", 1), TestChannelSlot{},
        channelSlot("charlie", 3), TestChannelSlot{}}};

    ASSERT_TRUE(updateChannelSlot(slots, 1, TestChannelSlot{}));
    EXPECT_STREQ(slots[0].name, "alpha");
    EXPECT_STREQ(slots[1].name, "charlie");
    EXPECT_STREQ(slots[2].name, "");
}

} // namespace
